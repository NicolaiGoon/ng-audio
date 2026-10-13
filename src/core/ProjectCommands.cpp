#include "CommandRegistry.h"

namespace ng
{
namespace
{
    using params::prop;
    using params::schema;

    juce::Colour pickColour (Session& session)
    {
        auto palette = Session::getPalette();
        return palette[session.getChannels().size() % palette.size()];
    }

    /** The built-in instruments a channel can hold. Third-party plugins come with plugin scanning. */
    const juce::StringArray& builtInInstruments()
    {
        static const juce::StringArray names { "4osc", "none" };
        return names;
    }

    /** Replaces whatever instrument the channel has with a built-in one ("none" just removes it). */
    void setInstrument (te::Edit& edit, te::AudioTrack& channel, const juce::String& instrument)
    {
        for (auto p : channel.pluginList.getPlugins())
            if (p->isSynth())
                p->deleteFromParent();

        if (instrument == "4osc")
            if (auto synth = edit.getPluginCache().createNewPlugin (te::FourOscPlugin::xmlTypeName, {}))
                channel.pluginList.insertPlugin (synth, 0, nullptr);
    }

    juce::String instrumentError (const juce::String& command, const juce::String& instrument)
    {
        return command + ": unknown instrument '" + instrument + "' (use one of: "
                 + builtInInstruments().joinIntoString (", ") + ")";
    }

    juce::var trackRef (Session& session, const te::AudioTrack& t)
    {
        auto obj = new juce::DynamicObject();
        obj->setProperty ("id", t.itemID.toString());
        obj->setProperty ("role", toString (Session::getRole (t)));
        obj->setProperty ("name", t.getName());
        obj->setProperty ("colour", "#" + t.getColour().toDisplayString (false));

        if (auto partner = session.getLinkedPartner (t))
            obj->setProperty ("linked_to", partner->itemID.toString());

        return juce::var (obj);
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
        auto colour = pickColour (session);

        if (p.hasProperty ("colour"))
        {
            auto parsed = parseColour (p["colour"].toString());

            if (! parsed)
                return CommandResult::failure ("create_track: '" + p["colour"].toString() + "' isn't a hex colour like #3c8ce0");

            colour = *parsed;
        }

        if (! builtInInstruments().contains (instrument))
            return CommandResult::failure (instrumentError ("create_track", instrument));

        // The channel: holds the instrument and its clips.
        auto channel = edit.insertNewAudioTrack (te::TrackInsertPoint::getEndOfTracks (edit), nullptr);

        if (channel == nullptr)
            return CommandResult::failure ("create_track: couldn't create a track");

        session.setRole (*channel, Session::Role::channel);
        channel->setName (name);
        channel->setColour (colour);

        setInstrument (edit, *channel, instrument);

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

    CommandResult renameTrack (Session& session, const juce::var& p)
    {
        auto ref = params::getString (p, "track");
        auto name = params::getString (p, "name").trim();
        auto track = session.findAnyTrack (ref);

        if (track == nullptr)
            return CommandResult::failure ("rename_track: no channel or mixer strip called '" + ref + "'");

        if (name.isEmpty())
            return CommandResult::failure ("rename_track: name can't be empty");

        track->setName (name);

        if (auto partner = session.getLinkedPartner (*track))
            partner->setName (name);

        return CommandResult::success (trackRef (session, *track));
    }

    CommandResult setTrackColour (Session& session, const juce::var& p)
    {
        auto ref = params::getString (p, "track");
        auto track = session.findAnyTrack (ref);

        if (track == nullptr)
            return CommandResult::failure ("set_track_colour: no channel or mixer strip called '" + ref + "'");

        auto colour = parseColour (params::getString (p, "colour"));

        if (! colour)
            return CommandResult::failure ("set_track_colour: '" + params::getString (p, "colour") + "' isn't a hex colour like #3c8ce0");

        track->setColour (*colour);

        if (auto partner = session.getLinkedPartner (*track))
            partner->setColour (*colour);

        return CommandResult::success (trackRef (session, *track));
    }

    CommandResult deleteTrack (Session& session, const juce::var& p)
    {
        auto& edit = session.getEdit();
        auto ref = params::getString (p, "track");
        auto track = session.findAnyTrack (ref);

        if (track == nullptr)
            return CommandResult::failure ("delete_track: no channel or mixer strip called '" + ref + "'");

        juce::Array<te::AudioTrack*> toDelete { track };

        // A channel takes its own strip with it, unless other channels still feed that strip.
        if (Session::getRole (*track) == Session::Role::channel)
        {
            if (auto own = session.getOwnStrip (*track))
            {
                bool shared = false;

                for (auto c : session.getChannels())
                    if (c != track && session.getDestinationStrip (*c) == own)
                        shared = true;

                if (! shared)
                    toDelete.add (own);
            }
        }

        juce::Array<juce::var> deleted, rerouted;

        for (auto t : toDelete)
        {
            // Anything that fed this track now goes straight to master, and channels
            // whose own strip this was have nothing left to link to.
            for (auto other : te::getAudioTracks (edit))
            {
                if (toDelete.contains (other))
                    continue;

                if (other->getOutput().getDestinationTrack() == t)
                {
                    other->getOutput().setOutputToDefaultDevice (false);
                    rerouted.add (other->itemID.toString());
                }

                if (Session::getRole (*other) == Session::Role::channel && session.getOwnStrip (*other) == t)
                    session.setOwnStrip (*other, nullptr);
            }

            deleted.add (t->itemID.toString());
        }

        for (auto t : toDelete)
            edit.deleteTrack (t);

        session.writeTrackOutputs();

        auto obj = new juce::DynamicObject();
        obj->setProperty ("deleted", deleted);
        obj->setProperty ("rerouted_to_master", rerouted);
        return CommandResult::success (juce::var (obj));
    }

    CommandResult setInstrumentCommand (Session& session, const juce::var& p)
    {
        auto ref = params::getString (p, "channel");
        auto instrument = params::getString (p, "instrument");
        auto channel = session.findTrack (ref, Session::Role::channel);

        if (channel == nullptr)
            return CommandResult::failure ("set_instrument: no channel called '" + ref + "'");

        if (! builtInInstruments().contains (instrument))
            return CommandResult::failure (instrumentError ("set_instrument", instrument));

        setInstrument (session.getEdit(), *channel, instrument);
        return CommandResult::success (idPair ("channel", channel->itemID.toString(), "instrument", instrument));
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

    registry.add ({ "rename_track",
                    "Renames a channel or mixer strip. While a channel is linked to its strip, both get the new name.",
                    schema ({ { "track", prop ("string", "Channel or mixer strip id or name") },
                              { "name", prop ("string", "The new name") } },
                            { "track", "name" }),
                    true, renameTrack });

    registry.add ({ "set_track_colour",
                    "Recolours a channel or mixer strip. While a channel is linked to its strip, both change.",
                    schema ({ { "track", prop ("string", "Channel or mixer strip id or name") },
                              { "colour", prop ("string", "Hex colour such as #3c8ce0") } },
                            { "track", "colour" }),
                    true, setTrackColour });

    registry.add ({ "delete_track",
                    "Deletes a channel or mixer strip. A channel takes its own strip with it unless other channels "
                    "still feed that strip; anything that fed a deleted strip is sent to master.",
                    schema ({ { "track", prop ("string", "Channel or mixer strip id or name") } }, { "track" }),
                    true, deleteTrack });

    registry.add ({ "set_instrument",
                    "Replaces a channel's instrument with a built-in one: \"4osc\" (synth) or \"none\".",
                    schema ({ { "channel", prop ("string", "Channel id or name") },
                              { "instrument", prop ("string", "\"4osc\" or \"none\"") } },
                            { "channel", "instrument" }),
                    true, setInstrumentCommand });

    registry.add ({ "play", "Starts playback, looping the whole project unless loop is false.",
                    schema ({ { "loop", prop ("boolean", "Loop the project (default true)") } }),
                    false, play });

    registry.add ({ "stop", "Stops playback.", schema ({}), false, stop });

    registry.add ({ "get_state", "Returns the project's channels, mixer strips, routing and transport as data.",
                    schema ({}), false, getState });
}

} // namespace ng
