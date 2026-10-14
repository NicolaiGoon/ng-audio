#include "CommandRegistry.h"

namespace ng
{
namespace
{
    using params::prop;
    using params::schema;

    double rounded (double beats)
    {
        return std::round (beats * 1.0e6) / 1.0e6;
    }

    /** Puts an audio file on an audio channel as a clip, or on a new audio channel named after
        the file. The clip plays the whole file from start_beat; trim it with resize_clip. */
    CommandResult importAudio (const CommandRegistry& registry, Session& session, const juce::var& p)
    {
        auto path = params::getString (p, "file");

        if (path.isEmpty())
            return CommandResult::failure ("import_audio: give the audio file's path");

        auto file = juce::File::isAbsolutePath (path) ? juce::File (path)
                                                      : juce::File::getCurrentWorkingDirectory().getChildFile (path);

        if (! file.existsAsFile())
            return CommandResult::failure ("import_audio: no file at '" + file.getFullPathName() + "'");

        te::AudioFile audio (session.getEngine(), file);

        if (! audio.isValid() || audio.getLength() <= 0.0)
            return CommandResult::failure ("import_audio: '" + file.getFileName() + "' isn't an audio file Ng DAW can read ("
                                             + session.getAudioFileWildcard() + ")");

        auto startBeat = params::getDouble (p, "start_beat", 0.0);

        if (startBeat < 0.0)
            return CommandResult::failure ("import_audio: start_beat can't be before the start of the project");

        auto result = new juce::DynamicObject();
        juce::var value (result);
        te::AudioTrack* channel = nullptr;

        if (p.hasProperty ("channel"))
        {
            auto ref = params::getString (p, "channel");
            channel = session.findTrack (ref, Session::Role::channel);

            if (channel == nullptr)
                return CommandResult::failure ("import_audio: no channel called '" + ref + "'");

            if (! Session::isAudioChannel (*channel))
                return CommandResult::failure ("import_audio: '" + channel->getName() + "' is an instrument channel; "
                                               "audio goes on an audio channel (leave out channel to make one)");
        }
        else
        {
            auto name = params::getString (p, "name", file.getFileNameWithoutExtension());
            auto trackParams = new juce::DynamicObject();
            trackParams->setProperty ("name", name);
            trackParams->setProperty ("instrument", "audio");
            auto created = registry.find ("create_track")->run (session, juce::var (trackParams));

            if (! created.ok)
                return CommandResult::failure ("import_audio: " + created.error);

            channel = session.findTrack (created.value["channel"].toString(), Session::Role::channel);
            result->setProperty ("strip", created.value["strip"]);

            if (channel == nullptr)
                return CommandResult::failure ("import_audio: couldn't create an audio channel");
        }

        auto& tempo = session.getEdit().tempoSequence;
        auto start = tempo.toTime (te::BeatPosition::fromBeats (startBeat));
        te::ClipPosition position { { start, start + te::TimeDuration::fromSeconds (audio.getLength()) }, {} };
        auto clip = channel->insertWaveClip (file.getFileNameWithoutExtension(), file, position, false);

        if (clip == nullptr)
            return CommandResult::failure ("import_audio: couldn't add '" + file.getFileName() + "'");

        auto pos = clip->getPosition();
        auto clipStart = tempo.toBeats (pos.getStart()).inBeats();
        result->setProperty ("clip", clip->itemID.toString());
        result->setProperty ("channel", channel->itemID.toString());
        result->setProperty ("start_beat", rounded (clipStart));
        result->setProperty ("length_beats", rounded (tempo.toBeats (pos.getEnd()).inBeats() - clipStart));
        result->setProperty ("file", file.getFullPathName());
        return CommandResult::success (value);
    }

    bool isNumber (const juce::var& v)
    {
        return v.isDouble() || v.isInt() || v.isInt64();
    }

    /** Changes how an audio clip plays: its gain, fades, and whether it follows the tempo. */
    CommandResult setAudioClip (Session& session, const juce::var& p)
    {
        auto id = params::getString (p, "clip");
        auto wave = dynamic_cast<te::WaveAudioClip*> (session.findClip (id));

        if (wave == nullptr)
            return CommandResult::failure (session.findClip (id) != nullptr ? "set_audio_clip: that's a MIDI clip, not an audio clip"
                                                                            : "set_audio_clip: no clip with id '" + id + "'");

        for (auto key : { "gain_db", "fade_in_beats", "fade_out_beats", "source_bpm" })
            if (p.hasProperty (key) && ! isNumber (p[key]))
                return CommandResult::failure (juce::String ("set_audio_clip: ") + key + " must be a number");

        // Values checked up front, so a bad one changes nothing.
        if (p.hasProperty ("gain_db") && (static_cast<double> (p["gain_db"]) < -60.0 || static_cast<double> (p["gain_db"]) > 24.0))
            return CommandResult::failure ("set_audio_clip: gain_db must be from -60 to 24");

        if (static_cast<double> (p.getProperty ("fade_in_beats", 0.0)) < 0.0 || static_cast<double> (p.getProperty ("fade_out_beats", 0.0)) < 0.0)
            return CommandResult::failure ("set_audio_clip: fades can't be negative");

        auto& clip = *wave;
        auto& tempo = session.getEdit().tempoSequence;

        if (p.hasProperty ("follow_tempo"))
        {
            auto follow = static_cast<bool> (p["follow_tempo"]);

            if (follow)
            {
                // The file's own tempo: given, or what it plays at now, so turning this on
                // changes nothing until the project tempo does.
                auto pos = clip.getPosition();
                auto bpm = params::getDouble (p, "source_bpm", tempo.getBpmAt (pos.getStart()));

                if (bpm < 20.0 || bpm > 400.0)
                    return CommandResult::failure ("set_audio_clip: source_bpm must be from 20 to 400");

                auto startBeat = tempo.toBeats (pos.getStart());
                auto endBeat = tempo.toBeats (pos.getEnd());
                auto offsetBeats = pos.getOffset().inSeconds() * bpm / 60.0;

                clip.getLoopInfo().setBpm (bpm, clip.getAudioFile().getInfo());
                clip.setTimeStretchMode (te::TimeStretcher::soundtouchBetter);
                clip.setAutoTempo (true);

                // The clip keeps its place and length in beats, playing the same part of the file.
                clip.setPosition ({ { tempo.toTime (startBeat), tempo.toTime (endBeat) },
                                    te::TimeDuration::fromSeconds (offsetBeats / tempo.getBeatsPerSecondAt (pos.getStart())) });
            }
            else if (clip.getAutoTempo())
            {
                // Back to the file's own speed: the same part of the file, so the length in time changes.
                auto bpm = clip.getLoopInfo().getBpm (clip.getAudioFile().getInfo());
                auto pos = clip.getPosition();
                auto ratio = tempo.getBpmAt (pos.getStart()) / bpm;
                clip.setAutoTempo (false);
                clip.setPosition ({ { pos.getStart(), pos.getStart() + pos.getLength() * ratio }, pos.getOffset() * ratio });

                if (clip.getPosition().getLength() > clip.getMaximumLength())
                    clip.setLength (clip.getMaximumLength(), true);
            }
        }
        else if (p.hasProperty ("source_bpm"))
        {
            return CommandResult::failure ("set_audio_clip: source_bpm goes with follow_tempo: true");
        }

        if (p.hasProperty ("gain_db"))
            clip.setGainDB ((float) static_cast<double> (p["gain_db"]));

        // Fades in beats from each end; together they can't be longer than the clip.
        auto pos = clip.getPosition();
        auto startBeat = tempo.toBeats (pos.getStart()).inBeats();
        auto endBeat = tempo.toBeats (pos.getEnd()).inBeats();
        auto fadeIn = p.hasProperty ("fade_in_beats") ? static_cast<double> (p["fade_in_beats"])
                                                      : tempo.toBeats (pos.getStart() + clip.getFadeIn()).inBeats() - startBeat;
        auto fadeOut = p.hasProperty ("fade_out_beats") ? static_cast<double> (p["fade_out_beats"])
                                                        : endBeat - tempo.toBeats (pos.getEnd() - clip.getFadeOut()).inBeats();

        if (fadeIn < 0.0 || fadeOut < 0.0)
            return CommandResult::failure ("set_audio_clip: fades can't be negative");

        if (fadeIn + fadeOut > endBeat - startBeat + 1.0e-6)
            return CommandResult::failure ("set_audio_clip: the fades add up to more than the clip's "
                                           + juce::String (endBeat - startBeat, 3) + " beats");

        if (p.hasProperty ("fade_in_beats"))
            clip.setFadeIn (tempo.toTime (te::BeatPosition::fromBeats (startBeat + fadeIn)) - pos.getStart());

        if (p.hasProperty ("fade_out_beats"))
            clip.setFadeOut (pos.getEnd() - tempo.toTime (te::BeatPosition::fromBeats (endBeat - fadeOut)));

        auto result = new juce::DynamicObject();
        result->setProperty ("clip", id);
        result->setProperty ("start_beat", std::round (startBeat * 1.0e6) / 1.0e6);
        result->setProperty ("length_beats", std::round ((endBeat - startBeat) * 1.0e6) / 1.0e6);
        session.describeAudioClip (clip, *result);
        return CommandResult::success (juce::var (result));
    }

    CommandResult setAudioInput (Session& session, const juce::var& p)
    {
        auto ref = params::getString (p, "channel");
        auto channel = session.findTrack (ref, Session::Role::channel);

        if (channel == nullptr)
            return CommandResult::failure ("set_audio_input: no channel called '" + ref + "'");

        if (! Session::isAudioChannel (*channel))
            return CommandResult::failure ("set_audio_input: '" + channel->getName() + "' is an instrument channel; "
                                           "audio inputs go to audio channels");

        if (! p.hasProperty ("input") && ! p.hasProperty ("monitor"))
            return CommandResult::failure ("set_audio_input: give an input, a monitor mode, or both");

        if (p.hasProperty ("input"))
        {
            auto input = params::getString (p, "input");
            auto inputs = session.getAudioInputNames();

            if (input == "none")
                input = {};

            if (input.isNotEmpty() && ! inputs.contains (input))
                return CommandResult::failure ("set_audio_input: no audio input called '" + input + "' ("
                                                 + (inputs.isEmpty() ? juce::String ("the audio device has none")
                                                                     : "there's " + inputs.joinIntoString (", ")) + ")");

            // With no input there's nothing to record.
            if (input.isEmpty())
                session.setAudioArmed (*channel, false);

            session.setAudioInput (*channel, input);
        }

        if (p.hasProperty ("monitor"))
        {
            auto mode = params::getString (p, "monitor");

            if (mode != "auto" && mode != "on" && mode != "off")
                return CommandResult::failure ("set_audio_input: monitor is \"auto\", \"on\" or \"off\"");

            session.setMonitorMode (*channel, mode);
        }

        session.routeAudioInputs();

        auto result = new juce::DynamicObject();
        result->setProperty ("channel", channel->itemID.toString());
        result->setProperty ("input", Session::getAudioInput (*channel));
        result->setProperty ("monitor", Session::getMonitorMode (*channel));
        result->setProperty ("armed", session.isAudioArmed (*channel));
        return CommandResult::success (juce::var (result));
    }
}

void registerAudioCommands (CommandRegistry& registry)
{
    registry.add ({ "import_audio",
                    "Puts an audio file (WAV, AIFF, FLAC, Ogg or MP3) on the timeline as a clip that plays the whole file. "
                    "It goes on the audio channel you name, or on a new audio channel (and mixer strip) named after the file. "
                    "Move and trim it like any clip.",
                    schema ({ { "file", prop ("string", "Path to the audio file") },
                              { "channel", prop ("string", "An audio channel's id or name; leave out for a new audio channel") },
                              { "start_beat", prop ("number", "Where the clip starts, in beats (default 0)") },
                              { "name", prop ("string", "Name for the new channel (default: the file's name)") } },
                            { "file" }),
                    true, [&registry] (Session& session, const juce::var& p) { return importAudio (registry, session, p); } });

    registry.add ({ "set_audio_clip",
                    "Changes how an audio clip plays: its gain, its fade in and fade out (in beats from each end), and "
                    "whether it follows the tempo. A clip that follows the tempo is time-stretched to keep its length in "
                    "beats when the tempo changes; source_bpm is the file's own tempo (default: the project tempo now, so "
                    "nothing changes until the tempo does). Only the values given change.",
                    schema ({ { "clip", prop ("string", "The audio clip's id (from get_state)") },
                              { "gain_db", prop ("number", "Clip gain in dB, -60 to 24 (0 is as recorded)") },
                              { "fade_in_beats", prop ("number", "Fade-in length in beats (0 for none)") },
                              { "fade_out_beats", prop ("number", "Fade-out length in beats (0 for none)") },
                              { "follow_tempo", prop ("boolean", "Time-stretch the clip to follow the project tempo") },
                              { "source_bpm", prop ("number", "With follow_tempo: the file's own tempo") } },
                            { "clip" }),
                    true, setAudioClip });

    registry.add ({ "set_audio_input",
                    "Sets the audio input an audio channel records from, and how it's monitored: \"auto\" (default) "
                    "plays the input through the channel while it's armed, \"on\" always, \"off\" never. get_state "
                    "lists the inputs as audio_inputs. Arm the channel with arm_channel and record with record.",
                    schema ({ { "channel", prop ("string", "Audio channel id or name") },
                              { "input", prop ("string", "An input from audio_inputs in get_state, such as \"Input 1 + 2\", or \"none\"") },
                              { "monitor", prop ("string", "\"auto\", \"on\" or \"off\"") } },
                            { "channel" }),
                    true, setAudioInput });
}

} // namespace ng
