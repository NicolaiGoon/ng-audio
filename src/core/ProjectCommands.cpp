#include "CommandRegistry.h"

namespace ng
{
namespace
{
    using params::prop;
    using params::schema;

    juce::Colour pickColour (Session& session)
    {
        static const juce::uint32 palette[] = { 0xffe0603c, 0xffe8b23a, 0xff8cc84b, 0xff3cb4a0,
                                                0xff3c8ce0, 0xff8c64dc, 0xffd85aa8, 0xff9aa0a6 };
        auto index = static_cast<size_t> (session.getChannels().size()) % std::size (palette);
        return juce::Colour (palette[index]);
    }

    juce::var idPair (const juce::String& k1, const juce::String& v1,
                      const juce::String& k2, const juce::var& v2)
    {
        auto obj = new juce::DynamicObject();
        obj->setProperty (k1, v1);
        obj->setProperty (k2, v2);
        return juce::var (obj);
    }

    //==============================================================================
    CommandResult createTrack (Session& session, const juce::var& p)
    {
        auto& edit = session.getEdit();
        auto name = params::getString (p, "name", "Instrument " + juce::String (session.getChannels().size() + 1));
        auto instrument = params::getString (p, "instrument", "4osc");
        auto colour = p.hasProperty ("colour") ? juce::Colour::fromString (p["colour"].toString().removeCharacters ("#").paddedLeft ('f', 8))
                                               : pickColour (session);

        if (instrument != "4osc" && instrument != "none")
            return CommandResult::failure ("create_track: unknown instrument '" + instrument + "' (use \"4osc\" or \"none\")");

        // The channel: holds the instrument and its clips.
        auto channel = edit.insertNewAudioTrack (te::TrackInsertPoint::getEndOfTracks (edit), nullptr);

        if (channel == nullptr)
            return CommandResult::failure ("create_track: couldn't create a track");

        session.setRole (*channel, Session::Role::channel);
        channel->setName (name);
        channel->setColour (colour);

        if (instrument == "4osc")
            if (auto synth = edit.getPluginCache().createNewPlugin (te::FourOscPlugin::xmlTypeName, {}))
                channel->pluginList.insertPlugin (synth, 0, nullptr);

        // Its mixer strip: same name and colour, linked by default, feeding master.
        auto strip = edit.insertNewAudioTrack (te::TrackInsertPoint::getEndOfTracks (edit), nullptr);

        if (strip == nullptr)
            return CommandResult::failure ("create_track: couldn't create a mixer strip");

        session.setRole (*strip, Session::Role::strip);
        strip->setName (name);
        strip->setColour (colour);
        strip->getOutput().setOutputToDefaultDevice (false);

        channel->getOutput().setOutputToTrack (strip.get());
        session.setOwnStrip (*channel, strip.get());

        return CommandResult::success (idPair ("channel", channel->itemID.toString(),
                                               "strip", strip->itemID.toString()));
    }

    CommandResult route (Session& session, const juce::var& p)
    {
        auto channelRef = params::getString (p, "channel");
        auto stripRef = params::getString (p, "strip");

        auto channel = session.findTrack (channelRef, Session::Role::channel);

        if (channel == nullptr)
            return CommandResult::failure ("route: no channel called '" + channelRef + "'");

        if (stripRef == "master")
        {
            channel->getOutput().setOutputToDefaultDevice (false);
        }
        else
        {
            auto strip = session.findTrack (stripRef, Session::Role::strip);

            if (strip == nullptr)
                return CommandResult::failure ("route: no mixer strip called '" + stripRef + "'");

            channel->getOutput().setOutputToTrack (strip);
        }

        auto obj = new juce::DynamicObject();
        obj->setProperty ("channel", channel->itemID.toString());

        if (auto dest = session.getDestinationStrip (*channel))
            obj->setProperty ("output", dest->itemID.toString());
        else
            obj->setProperty ("output", "master");

        obj->setProperty ("linked", session.isLinked (*channel));
        return CommandResult::success (juce::var (obj));
    }

    CommandResult addMidiClip (Session& session, const juce::var& p)
    {
        auto& edit = session.getEdit();
        auto channelRef = params::getString (p, "channel");
        auto channel = session.findTrack (channelRef, Session::Role::channel);

        if (channel == nullptr)
            return CommandResult::failure ("add_midi_clip: no channel called '" + channelRef + "'");

        auto startBeat = te::BeatPosition::fromBeats (params::getDouble (p, "start_beat", 0.0));
        auto lengthBeats = te::BeatDuration::fromBeats (params::getDouble (p, "length_beats", 4.0));

        if (lengthBeats <= te::BeatDuration())
            return CommandResult::failure ("add_midi_clip: length_beats must be positive");

        auto& tempo = edit.tempoSequence;
        te::TimeRange range (tempo.toTime (startBeat), tempo.toTime (startBeat + lengthBeats));

        auto clip = channel->insertMIDIClip (range, nullptr);

        if (clip == nullptr)
            return CommandResult::failure ("add_midi_clip: couldn't create the clip");

        if (auto notes = p["notes"].getArray())
        {
            for (auto& n : *notes)
            {
                auto pitch = static_cast<int> (n.getProperty ("pitch", 60));
                auto start = static_cast<double> (n.getProperty ("start", 0.0));
                auto length = static_cast<double> (n.getProperty ("length", 1.0));
                auto velocity = static_cast<int> (n.getProperty ("velocity", 100));

                if (! juce::isPositiveAndBelow (pitch, 128) || length <= 0.0)
                    return CommandResult::failure ("add_midi_clip: invalid note " + juce::JSON::toString (n, true));

                clip->getSequence().addNote (pitch, te::BeatPosition::fromBeats (start),
                                             te::BeatDuration::fromBeats (length),
                                             juce::jlimit (1, 127, velocity), 0, &session.getUndoManager());
            }
        }

        return CommandResult::success (idPair ("clip", clip->itemID.toString(),
                                               "notes", clip->getSequence().getNotes().size()));
    }

    CommandResult play (Session& session, const juce::var& p)
    {
        auto& edit = session.getEdit();
        auto& transport = edit.getTransport();

        if (static_cast<bool> (p.getProperty ("loop", true)) && edit.getLength() > te::TimeDuration())
        {
            transport.setLoopRange ({ te::TimePosition(), edit.getLength() });
            transport.looping = true;
        }

        transport.play (false);
        return CommandResult::success();
    }

    CommandResult stop (Session& session, const juce::var&)
    {
        session.getEdit().getTransport().stop (false, false);
        return CommandResult::success();
    }

    CommandResult getState (Session& session, const juce::var&)
    {
        return CommandResult::success (session.toVar());
    }
}

//==============================================================================
void registerProjectCommands (CommandRegistry& registry)
{
    registry.add ({ "create_track",
                    "Adds an instrument channel and a mixer strip with the same name and colour, linked to it.",
                    schema ({ { "name", prop ("string", "Name for the channel and its strip") },
                              { "colour", prop ("string", "Hex colour such as #3c8ce0; picked automatically if omitted") },
                              { "instrument", prop ("string", "\"4osc\" (built-in synth, default) or \"none\"") } }),
                    true, createTrack });

    registry.add ({ "route",
                    "Sends a channel to a mixer strip, or to \"master\". Routing away from its own strip detaches the channel; "
                    "several channels can share one strip.",
                    schema ({ { "channel", prop ("string", "Channel id or name") },
                              { "strip", prop ("string", "Mixer strip id or name, or \"master\"") } },
                            { "channel", "strip" }),
                    true, route });

    registry.add ({ "add_midi_clip",
                    "Adds a MIDI clip with notes to a channel. Note start and length are in beats from the clip start.",
                    schema ({ { "channel", prop ("string", "Channel id or name") },
                              { "start_beat", prop ("number", "Where the clip starts, in beats (default 0)") },
                              { "length_beats", prop ("number", "Clip length in beats (default 4)") },
                              { "notes", prop ("array", "Notes as {pitch, start, length, velocity}") } },
                            { "channel" }),
                    true, addMidiClip });

    registry.add ({ "play", "Starts playback, looping the whole project unless loop is false.",
                    schema ({ { "loop", prop ("boolean", "Loop the project (default true)") } }),
                    false, play });

    registry.add ({ "stop", "Stops playback.", schema ({}), false, stop });

    registry.add ({ "get_state", "Returns the project's channels, mixer strips, routing and transport as data.",
                    schema ({}), false, getState });
}

} // namespace ng
