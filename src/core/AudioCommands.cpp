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
}

} // namespace ng
