#include "CommandRegistry.h"

#include <tracktion_engine/timestretch/tracktion_TempoDetect.h>

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

    /** An audio channel that already plays this file and has room for it at startBeat, so
        dropping the same sample again doesn't make another channel. */
    te::AudioTrack* findChannelPlaying (Session& session, const juce::File& file, double startBeat, double seconds)
    {
        auto& tempo = session.getEdit().tempoSequence;
        auto start = tempo.toTime (te::BeatPosition::fromBeats (startBeat));
        te::TimeRange range { start, start + te::TimeDuration::fromSeconds (seconds) };

        for (auto channel : session.getChannels())
        {
            if (! Session::isAudioChannel (*channel))
                continue;

            bool playsFile = false, free = true;

            for (auto clip : channel->getClips())
            {
                if (auto wave = dynamic_cast<te::WaveAudioClip*> (clip))
                    playsFile = playsFile || wave->getOriginalFile() == file;

                free = free && ! clip->getPosition().time.overlaps (range);
            }

            if (playsFile && free)
                return channel;
        }

        return nullptr;
    }

    /** Puts an audio file on an audio channel as a clip. Without a channel it goes on the audio
        channel already playing that file, if it has room there, or else on a new audio channel
        named after the file. The clip plays the whole file from start_beat; trim it with resize_clip. */
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
            if (! p.hasProperty ("name"))
                channel = findChannelPlaying (session, file, startBeat, audio.getLength());
        }

        if (channel == nullptr)
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

        Session::useRealTimeStretching (*clip);

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

    // Resampling and stretching go from a quarter to four times the speed: two octaves either way.
    constexpr double minSpeed = 0.25, maxSpeed = 4.0, maxSemitones = 24.0;

    double speedToSemitones (double speed)    { return 12.0 * std::log2 (speed); }
    double semitonesToSpeed (double semis)    { return std::pow (2.0, semis / 12.0); }

    /** Plays a clip that doesn't follow the tempo at a new speed. It keeps its start and plays
        the same part of its file, so its length in time changes. */
    void setSpeedKeepingStart (te::WaveAudioClip& clip, double newSpeed)
    {
        auto pos = clip.getPosition();
        auto factor = clip.getSpeedRatio() / newSpeed;
        clip.setSpeedRatio (newSpeed);
        clip.setPosition ({ { pos.getStart(), pos.getStart() + pos.getLength() * factor }, pos.getOffset() * factor });
    }

    /** Stretches a clip that follows the tempo by calling its file a different tempo: it keeps
        its start and plays the same part of the file over more or fewer beats. */
    void setSourceBpmKeepingStart (Session& session, te::WaveAudioClip& clip, double newBpm)
    {
        auto& tempo = session.getEdit().tempoSequence;
        auto pos = clip.getPosition();
        auto oldBpm = clip.getLoopInfo().getBpm (clip.getAudioFile().getInfo());
        auto factor = oldBpm / newBpm;
        auto startBeat = tempo.toBeats (pos.getStart());
        auto lengthBeats = (tempo.toBeats (pos.getEnd()) - startBeat).inBeats() * factor;
        auto offsetBeats = clip.getOffsetInBeats().inBeats() * factor;

        clip.getLoopInfo().setBpm (newBpm, clip.getAudioFile().getInfo());
        clip.setPosition ({ { pos.getStart(), tempo.toTime (startBeat + te::BeatDuration::fromBeats (lengthBeats)) },
                            te::TimeDuration::fromSeconds (offsetBeats / tempo.getBeatsPerSecondAt (pos.getStart())) });
    }

    /** The loudest sample in a file, as a gain (1 is full scale). */
    float getPeak (Session& session, const juce::File& file)
    {
        std::unique_ptr<juce::AudioFormatReader> reader (session.getEngine().getAudioFileFormatManager().readFormatManager
                                                           .createReaderFor (file));

        if (reader == nullptr || reader->lengthInSamples <= 0)
            return 0.0f;

        juce::Range<float> levels[2];
        reader->readMaxLevels (0, reader->lengthInSamples, levels, (int) std::min (2u, reader->numChannels));
        auto peak = 0.0f;

        for (int ch = 0; ch < (int) std::min (2u, reader->numChannels); ++ch)
            peak = std::max ({ peak, std::abs (levels[ch].getStart()), std::abs (levels[ch].getEnd()) });

        return peak;
    }

    /** Changes how an audio clip plays, like the sample settings in FL Studio or the sample editor
        in Cubase: its gain, fades, pitch, speed, whether it follows the tempo and how it's stretched. */
    CommandResult setAudioClip (Session& session, const juce::var& p)
    {
        auto id = params::getString (p, "clip");
        auto wave = dynamic_cast<te::WaveAudioClip*> (session.findClip (id));

        if (wave == nullptr)
            return CommandResult::failure (session.findClip (id) != nullptr ? "set_audio_clip: that's a MIDI clip, not an audio clip"
                                                                            : "set_audio_clip: no clip with id '" + id + "'");

        for (auto key : { "gain_db", "fade_in_beats", "fade_out_beats", "source_bpm", "pitch_semitones", "stretch_to_beats" })
            if (p.hasProperty (key) && ! isNumber (p[key]))
                return CommandResult::failure (juce::String ("set_audio_clip: ") + key + " must be a number");

        // Values checked up front, so a bad one changes nothing.
        if (p.hasProperty ("gain_db") && (static_cast<double> (p["gain_db"]) < -60.0 || static_cast<double> (p["gain_db"]) > 24.0))
            return CommandResult::failure ("set_audio_clip: gain_db must be from -60 to 24");

        if (p.hasProperty ("gain_db") && static_cast<bool> (p.getProperty ("normalize", false)))
            return CommandResult::failure ("set_audio_clip: give gain_db or normalize, not both");

        if (static_cast<double> (p.getProperty ("fade_in_beats", 0.0)) < 0.0 || static_cast<double> (p.getProperty ("fade_out_beats", 0.0)) < 0.0)
            return CommandResult::failure ("set_audio_clip: fades can't be negative");

        if (p.hasProperty ("pitch_semitones") && std::abs (static_cast<double> (p["pitch_semitones"])) > maxSemitones)
            return CommandResult::failure ("set_audio_clip: pitch_semitones must be from -24 to 24");

        if (p.hasProperty ("stretch_to_beats") && static_cast<double> (p["stretch_to_beats"]) <= 0.0)
            return CommandResult::failure ("set_audio_clip: stretch_to_beats must be more than 0");

        auto& clip = *wave;
        auto& tempo = session.getEdit().tempoSequence;
        auto* um = &session.getEdit().getUndoManager();
        Session::useRealTimeStretching (clip);
        auto mode = Session::getStretchMode (clip);

        if (p.hasProperty ("stretch_mode"))
        {
            mode = params::getString (p, "stretch_mode");

            if (mode != "stretch" && mode != "beats" && mode != "resample")
                return CommandResult::failure ("set_audio_clip: stretch_mode is \"stretch\", \"beats\" or \"resample\"");
        }

        auto followTempo = p.hasProperty ("follow_tempo") ? static_cast<bool> (p["follow_tempo"]) : clip.getAutoTempo();

        if (mode == "resample" && followTempo)
            return CommandResult::failure ("set_audio_clip: resample changes pitch with speed, so it can't follow the tempo; "
                                           "use stretch_mode \"stretch\" or \"beats\", or turn follow_tempo off");

        if (p.hasProperty ("source_bpm") && ! followTempo)
            return CommandResult::failure ("set_audio_clip: source_bpm goes with follow_tempo: true");

        if (p.hasProperty ("source_bpm") && (static_cast<double> (p["source_bpm"]) < 20.0 || static_cast<double> (p["source_bpm"]) > 400.0))
            return CommandResult::failure ("set_audio_clip: source_bpm must be from 20 to 400");

        // The stretch mode. Switching keeps the clip's pitch, and its length unless it's resampled.
        if (mode != Session::getStretchMode (clip))
        {
            auto pitch = Session::getPitchChange (clip);

            if (mode == "resample")
            {
                // Resampled, the pitch is the speed, so the clip gets longer or shorter.
                clip.setPitchChange (0.0f);
                clip.setTimeStretchMode (te::TimeStretcher::disabled);
                clip.state.setProperty ("ngResample", true, um);
                setSpeedKeepingStart (clip, clip.getSpeedRatio() * semitonesToSpeed (pitch));
            }
            else
            {
                clip.state.removeProperty ("ngResample", um);
                clip.setTimeStretchMode (mode == "beats" ? te::TimeStretcher::rubberbandPercussive
                                                         : te::TimeStretcher::rubberbandMelodic);
                clip.setPitchChange ((float) pitch);
            }
        }
        else if (mode != "resample" && clip.getTimeStretchMode() != te::TimeStretcher::rubberbandPercussive
                   && clip.getTimeStretchMode() != te::TimeStretcher::rubberbandMelodic)
        {
            // Older clips (and new recordings) name no stretcher, or SoundTouch: they get Rubber Band.
            clip.setTimeStretchMode (te::TimeStretcher::rubberbandMelodic);
        }

        if (p.hasProperty ("follow_tempo"))
        {
            if (followTempo && ! clip.getAutoTempo())
            {
                // The file's own tempo: given, or what it plays at now, so turning this on
                // changes nothing until the project tempo does.
                auto pos = clip.getPosition();
                auto speed = clip.getSpeedRatio();
                auto bpm = params::getDouble (p, "source_bpm", tempo.getBpmAt (pos.getStart()) / speed);

                if (bpm < 20.0 || bpm > 400.0)
                    return CommandResult::failure ("set_audio_clip: the clip plays at " + juce::String (bpm, 1)
                                                     + " bpm, so it can't follow the tempo (20 to 400 bpm)");

                auto startBeat = tempo.toBeats (pos.getStart());
                auto endBeat = tempo.toBeats (pos.getEnd());
                auto offsetBeats = pos.getOffset().inSeconds() * speed * bpm / 60.0;
                auto playsToTheEnd = std::abs (session.getSourceEndBeat (clip) - endBeat.inBeats()) < 1.0e-3;

                clip.setSpeedRatio (1.0);
                clip.getLoopInfo().setBpm (bpm, clip.getAudioFile().getInfo());
                clip.setAutoTempo (true);

                // The clip keeps its place and length in beats, playing the same part of the file;
                // one that played to the end of its file still does, at its new tempo.
                clip.setPosition ({ { tempo.toTime (startBeat), tempo.toTime (endBeat) },
                                    te::TimeDuration::fromSeconds (offsetBeats / tempo.getBeatsPerSecondAt (pos.getStart())) });

                if (playsToTheEnd)
                    clip.setPosition ({ { clip.getPosition().getStart(),
                                          tempo.toTime (te::BeatPosition::fromBeats (session.getSourceEndBeat (clip))) },
                                        clip.getPosition().getOffset() });
            }
            else if (! followTempo && clip.getAutoTempo())
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
            else if (followTempo && p.hasProperty ("source_bpm"))
            {
                setSourceBpmKeepingStart (session, clip, static_cast<double> (p["source_bpm"]));
            }
        }
        else if (p.hasProperty ("source_bpm"))
        {
            setSourceBpmKeepingStart (session, clip, static_cast<double> (p["source_bpm"]));
        }

        // Stretching to a length: the same part of the file over more or fewer beats.
        if (p.hasProperty ("stretch_to_beats"))
        {
            auto pos = clip.getPosition();
            auto startBeat = tempo.toBeats (pos.getStart());
            auto oldBeats = (tempo.toBeats (pos.getEnd()) - startBeat).inBeats();
            auto newBeats = static_cast<double> (p["stretch_to_beats"]);

            if (clip.getAutoTempo())
            {
                auto bpm = clip.getLoopInfo().getBpm (clip.getAudioFile().getInfo()) * oldBeats / newBeats;

                if (bpm < 20.0 || bpm > 400.0)
                    return CommandResult::failure ("set_audio_clip: stretched to " + juce::String (newBeats, 2)
                                                     + " beats the file would be " + juce::String (bpm, 1) + " bpm (20 to 400 works)");

                setSourceBpmKeepingStart (session, clip, bpm);
            }
            else
            {
                auto newLength = tempo.toTime (startBeat + te::BeatDuration::fromBeats (newBeats)) - pos.getStart();
                auto speed = clip.getSpeedRatio() * pos.getLength().inSeconds() / newLength.inSeconds();

                if (speed < minSpeed * 0.999 || speed > maxSpeed * 1.001)
                    return CommandResult::failure ("set_audio_clip: that's " + juce::String (speed, 2)
                                                     + " times the speed; stretching goes from 0.25 to 4 times");

                setSpeedKeepingStart (clip, speed);
            }
        }

        // Pitch: in resample mode it's the speed; otherwise Rubber Band shifts it on its own.
        if (p.hasProperty ("pitch_semitones"))
        {
            auto semis = static_cast<double> (p["pitch_semitones"]);

            if (mode == "resample")
                setSpeedKeepingStart (clip, semitonesToSpeed (semis));
            else
                clip.setPitchChange ((float) semis);
        }

        if (mode == "resample" && std::abs (speedToSemitones (clip.getSpeedRatio())) > maxSemitones + 0.001)
            return CommandResult::failure ("set_audio_clip: resampled, that would be more than two octaves off its pitch");

        if (p.hasProperty ("reverse") && static_cast<bool> (p["reverse"]) != clip.getIsReversed())
            clip.setIsReversed (static_cast<bool> (p["reverse"]));

        if (p.hasProperty ("gain_db"))
            clip.setGainDB ((float) static_cast<double> (p["gain_db"]));

        if (static_cast<bool> (p.getProperty ("normalize", false)))
        {
            auto peak = getPeak (session, clip.getOriginalFile());

            if (peak <= 0.0f)
                return CommandResult::failure ("set_audio_clip: the file is silent (or missing), so there's nothing to normalize");

            clip.setGainDB (juce::jlimit (-60.0f, 24.0f, -juce::Decibels::gainToDecibels (peak)));
        }

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
        {
            // A clip stretched shorter takes its fades with it.
            if (p.hasProperty ("fade_in_beats") || p.hasProperty ("fade_out_beats"))
                return CommandResult::failure ("set_audio_clip: the fades add up to more than the clip's "
                                                 + juce::String (endBeat - startBeat, 3) + " beats");

            auto scale = (endBeat - startBeat) / (fadeIn + fadeOut);
            fadeIn *= scale;
            fadeOut *= scale;
            clip.setFadeIn (tempo.toTime (te::BeatPosition::fromBeats (startBeat + fadeIn)) - pos.getStart());
            clip.setFadeOut (pos.getEnd() - tempo.toTime (te::BeatPosition::fromBeats (endBeat - fadeOut)));
        }

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

    /** Guesses a file's tempo. SoundTouch's beat tracker gives a first guess; a loop (a short
        file) is usually a whole number of bars, so a tempo that makes it exactly that wins when
        it's close. Returns 0 when there's no sensible guess. */
    double detectBpm (Session& session, const juce::File& file, double lengthSeconds)
    {
        std::unique_ptr<juce::AudioFormatReader> reader (session.getEngine().getAudioFileFormatManager().readFormatManager
                                                           .createReaderFor (file));

        if (reader == nullptr || reader->lengthInSamples <= 0)
            return 0.0;

        // A minute and a half is plenty to find the beat, and keeps long files quick.
        auto numChannels = (int) std::min (2u, reader->numChannels);
        auto numSamples = std::min (reader->lengthInSamples, (juce::int64) (90.0 * reader->sampleRate));
        te::TempoDetect detector (numChannels, reader->sampleRate);
        juce::AudioBuffer<float> buffer (numChannels, 65536);

        for (juce::int64 done = 0; done < numSamples;)
        {
            auto n = (int) std::min ((juce::int64) buffer.getNumSamples(), numSamples - done);
            reader->read (&buffer, 0, n, done, true, numChannels > 1);
            detector.processSection (buffer, n);
            done += n;
        }

        double bpm = detector.finishAndDetect();
        auto sensible = te::TempoDetect::getSensibleRange();

        // Whole bars of 4/4 (or a single beat or two) at a tempo from 70 to 180.
        auto loopBpm = 0.0;

        if (lengthSeconds > 0.0 && lengthSeconds < 40.0)
        {
            for (int beats : { 1, 2, 4, 8, 12, 16, 24, 32, 48, 64, 96, 128 })
            {
                auto candidate = 60.0 * beats / lengthSeconds;

                if (candidate < 70.0 || candidate > 180.0)
                    continue;

                auto reference = bpm > 0.0 ? bpm : 120.0;

                if (loopBpm == 0.0 || std::abs (candidate - reference) < std::abs (loopBpm - reference))
                    loopBpm = candidate;
            }
        }

        if (bpm <= sensible.getStart() || bpm >= sensible.getEnd())
            return loopBpm;

        // The tracker sometimes finds half or double the tempo; the loop's tempo settles which.
        if (loopBpm > 0.0)
            for (auto factor : { 1.0, 2.0, 0.5 })
                if (std::abs (bpm * factor - loopBpm) / loopBpm < 0.04)
                    return std::round (loopBpm * 100.0) / 100.0;

        // Most music is at a whole number of beats per minute.
        auto whole = std::round (bpm);
        return std::abs (bpm - whole) < 0.25 ? whole : std::round (bpm * 10.0) / 10.0;
    }

    /** Reads what's in an audio file (a clip's or any other): its length, format, loudest peak
        and tempo. Changes nothing. */
    CommandResult analyzeAudio (Session& session, const juce::var& p)
    {
        juce::File file;

        if (p.hasProperty ("clip"))
        {
            auto id = params::getString (p, "clip");
            auto wave = dynamic_cast<te::WaveAudioClip*> (session.findClip (id));

            if (wave == nullptr)
                return CommandResult::failure ("analyze_audio: no audio clip with id '" + id + "'");

            file = wave->getOriginalFile();
        }
        else
        {
            auto path = params::getString (p, "file");

            if (path.isEmpty())
                return CommandResult::failure ("analyze_audio: give a clip or a file");

            file = juce::File::isAbsolutePath (path) ? juce::File (path)
                                                     : juce::File::getCurrentWorkingDirectory().getChildFile (path);
        }

        te::AudioFile audio (session.getEngine(), file);

        if (! file.existsAsFile() || ! audio.isValid())
            return CommandResult::failure ("analyze_audio: can't read '" + file.getFullPathName() + "'");

        auto info = audio.getInfo();
        auto length = audio.getLength();
        auto result = new juce::DynamicObject();
        result->setProperty ("file", file.getFullPathName());
        result->setProperty ("length_seconds", std::round (length * 1000.0) / 1000.0);
        result->setProperty ("sample_rate", info.sampleRate);
        result->setProperty ("channels", info.numChannels);

        auto peak = getPeak (session, file);
        result->setProperty ("peak_db", peak > 0.0f ? std::round (juce::Decibels::gainToDecibels (peak) * 100.0f) / 100.0f : -100.0f);

        auto bpm = detectBpm (session, file, length);

        if (bpm > 0.0)
        {
            result->setProperty ("bpm", bpm);
            result->setProperty ("beats", std::round (length * bpm / 60.0 * 100.0) / 100.0);
        }

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
                    "It goes on the audio channel you name; left out, it goes on the audio channel already playing that file if it has room "
                    "there, or else on a new audio channel (and mixer strip) named after the file. "
                    "Move and trim it like any clip.",
                    schema ({ { "file", prop ("string", "Path to the audio file") },
                              { "channel", prop ("string", "An audio channel's id or name; leave out for the channel already playing the file, or a new one") },
                              { "start_beat", prop ("number", "Where the clip starts, in beats (default 0)") },
                              { "name", prop ("string", "Name for a new channel (default: the file's name); giving one always makes a new channel") } },
                            { "file" }),
                    true, [&registry] (Session& session, const juce::var& p) { return importAudio (registry, session, p); } });

    registry.add ({ "set_audio_clip",
                    "Changes how an audio clip plays, like a sample editor: gain (or normalize), fade in and fade out "
                    "(in beats from each end), pitch, length, reverse, and whether it follows the tempo. A clip that follows "
                    "the tempo is time-stretched to keep its length in beats when the tempo changes; source_bpm is the "
                    "file's own tempo (default: what it plays at now, so nothing changes until the tempo does; "
                    "analyze_audio detects it). stretch_mode picks how speed and pitch change: \"stretch\" (default; "
                    "Rubber Band's highest quality engine, for most sounds), \"beats\" (keeps drum hits crisp) or "
                    "\"resample\" (speed and pitch change together, as in FL Studio). Only the values given change.",
                    schema ({ { "clip", prop ("string", "The audio clip's id (from get_state)") },
                              { "gain_db", prop ("number", "Clip gain in dB, -60 to 24 (0 is as recorded)") },
                              { "normalize", prop ("boolean", "True sets the gain so the file's loudest peak reaches 0 dB") },
                              { "fade_in_beats", prop ("number", "Fade-in length in beats (0 for none)") },
                              { "fade_out_beats", prop ("number", "Fade-out length in beats (0 for none)") },
                              { "pitch_semitones", prop ("number", "Pitch change in semitones, -24 to 24; fractions are cents (0.5 is 50 cents). "
                                                                   "In resample mode this changes the speed and length too") },
                              { "stretch_to_beats", prop ("number", "Stretches (or squeezes) the clip to this many beats, from its start, "
                                                                    "playing the same part of its file") },
                              { "stretch_mode", prop ("string", "\"stretch\", \"beats\" or \"resample\"") },
                              { "reverse", prop ("boolean", "Plays the clip backwards") },
                              { "follow_tempo", prop ("boolean", "Time-stretch the clip to follow the project tempo") },
                              { "source_bpm", prop ("number", "With follow_tempo: the file's own tempo") } },
                            { "clip" }),
                    true, setAudioClip });

    registry.add ({ "analyze_audio",
                    "Reads an audio clip's file, or any audio file: its length, sample rate, channels, loudest peak "
                    "(peak_db) and detected tempo (bpm, and how many beats long it is at that tempo; left out when no "
                    "tempo is found). A short loop's tempo is matched to a whole number of bars. Changes nothing.",
                    schema ({ { "clip", prop ("string", "An audio clip's id") },
                              { "file", prop ("string", "Or the path to an audio file") } }),
                    false, analyzeAudio });

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
