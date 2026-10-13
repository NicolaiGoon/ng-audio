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

    CommandResult setMixerStrip (Session& session, const juce::var& p)
    {
        auto ref = params::getString (p, "strip");
        auto isMaster = ref == "master";
        te::AudioTrack* strip = nullptr;
        te::VolumeAndPanPlugin* volume = nullptr;

        if (isMaster)
        {
            volume = session.getEdit().getMasterVolumePlugin().get();
        }
        else
        {
            strip = session.findTrack (ref, Session::Role::strip);

            if (strip == nullptr)
                return CommandResult::failure ("set_mixer_strip: no mixer strip called '" + ref + "'");

            volume = strip->getVolumePlugin();
        }

        if (volume == nullptr)
            return CommandResult::failure ("set_mixer_strip: '" + ref + "' has no fader");

        // Check everything before changing anything, so a bad value leaves the strip as it was.
        auto volumeDb = p["volume_db"];
        auto pan = p["pan"];

        if (! volumeDb.isVoid() && ! (volumeDb.isDouble() || volumeDb.isInt() || volumeDb.isInt64()))
            return CommandResult::failure ("set_mixer_strip: volume_db must be a number");

        if (! pan.isVoid() && ! (pan.isDouble() || pan.isInt() || pan.isInt64()))
            return CommandResult::failure ("set_mixer_strip: pan must be a number");

        if (! volumeDb.isVoid() && (static_cast<double> (volumeDb) < -100.0 || static_cast<double> (volumeDb) > 6.0))
            return CommandResult::failure ("set_mixer_strip: volume_db must be between -100 and 6");

        if (! pan.isVoid() && std::abs (static_cast<double> (pan)) > 1.0)
            return CommandResult::failure ("set_mixer_strip: pan must be between -1 (left) and 1 (right)");

        if (isMaster && (p.hasProperty ("mute") || p.hasProperty ("solo")))
            return CommandResult::failure ("set_mixer_strip: the master strip has no mute or solo");

        // Tracktion's setVolumeDb()/setPan() write the saved value without undo, so write the
        // saved value (undoably) and then bring the live parameter in line with it.
        // A value still at its default is stored first, outside the undo history, so undo
        // returns to that value rather than removing it (which Tracktion would write back,
        // clearing the redo history).
        if (volume->volume.isUsingDefault())  volume->volume.setValue (volume->volume.get(), nullptr);
        if (volume->pan.isUsingDefault())     volume->pan.setValue (volume->pan.get(), nullptr);

        if (! volumeDb.isVoid())
        {
            volume->volume = te::decibelsToVolumeFaderPosition (static_cast<float> (static_cast<double> (volumeDb)));
            volume->volParam->updateFromAttachedValue();
        }

        if (! pan.isVoid())
        {
            volume->pan = static_cast<float> (static_cast<double> (pan));
            volume->panParam->updateFromAttachedValue();
        }

        if (strip != nullptr)
        {
            if (p.hasProperty ("mute"))  strip->setMute (static_cast<bool> (p["mute"]));
            if (p.hasProperty ("solo"))  strip->setSolo (static_cast<bool> (p["solo"]));
            session.getEdit().updateMuteSoloStatuses();
        }

        auto obj = new juce::DynamicObject();
        obj->setProperty ("strip", isMaster ? juce::String ("master") : strip->itemID.toString());
        obj->setProperty ("volume_db", volume->getVolumeDb());
        obj->setProperty ("pan", volume->getPan());

        if (strip != nullptr)
        {
            obj->setProperty ("mute", strip->isMuted (false));
            obj->setProperty ("solo", strip->isSolo (false));
        }

        return CommandResult::success (juce::var (obj));
    }

    //==============================================================================
    // Step patterns: each channel has a row of steps (16th notes) that plays one pitch.
    // The steps are stored on the channel and turned into a MIDI clip at the start of the
    // project, marked as the channel's pattern clip and rebuilt whenever the steps change.
    const juce::Identifier stepsProp ("ngSteps"), stepPitchProp ("ngStepPitch"), patternClipProp ("ngPattern");
    constexpr int maxStepCount = 64;
    constexpr double beatsPerStep = 0.25;

    void rebuildPatternClip (Session& session, te::AudioTrack& channel)
    {
        auto& edit = session.getEdit();
        auto* um = &session.getUndoManager();

        for (auto clip : channel.getClips())
            if (clip->state[patternClipProp])
                clip->removeFromParent();

        auto steps = Session::getSteps (channel);

        if (! steps.containsChar ('x'))
            return;

        auto& tempo = edit.tempoSequence;
        auto lengthBeats = te::BeatDuration::fromBeats (steps.length() * beatsPerStep);
        auto clip = channel.insertMIDIClip ("Pattern", { tempo.toTime (te::BeatPosition()), tempo.toTime (te::BeatPosition() + lengthBeats) }, nullptr);

        if (clip == nullptr)
            return;

        clip->state.setProperty (patternClipProp, true, um);
        auto pitch = Session::getStepPitch (channel);

        for (int i = 0; i < steps.length(); ++i)
            if (steps[i] == 'x')
                clip->getSequence().addNote (pitch, te::BeatPosition::fromBeats (i * beatsPerStep),
                                             te::BeatDuration::fromBeats (beatsPerStep), 100, 0, um);
    }

    CommandResult setSteps (Session& session, const juce::var& p)
    {
        auto ref = params::getString (p, "channel");
        auto channel = session.findTrack (ref, Session::Role::channel);

        if (channel == nullptr)
            return CommandResult::failure ("set_steps: no channel called '" + ref + "'");

        auto steps = Session::getSteps (*channel);
        auto pitch = Session::getStepPitch (*channel);

        // The whole row, as a string ("x..." or "1000...") or an array of booleans.
        if (p.hasProperty ("steps"))
        {
            auto value = p["steps"];
            juce::String newSteps;

            if (auto arr = value.getArray())
            {
                for (auto& v : *arr)
                    newSteps << (static_cast<bool> (v) ? "x" : ".");
            }
            else
            {
                auto text = value.toString().removeCharacters (" |");

                if (! text.containsOnly ("x.10X-"))
                    return CommandResult::failure ("set_steps: steps must be a string of 'x' and '.' (or 1 and 0), or an array of booleans");

                for (auto c : text)
                    newSteps << ((c == 'x' || c == 'X' || c == '1') ? "x" : ".");
            }

            if (newSteps.isEmpty() || newSteps.length() > maxStepCount)
                return CommandResult::failure ("set_steps: a pattern has 1 to " + juce::String (maxStepCount) + " steps");

            steps = newSteps;
        }

        // Or change the length, keeping the steps that still fit.
        if (p.hasProperty ("length"))
        {
            auto length = static_cast<int> (p["length"]);

            if (length < 1 || length > maxStepCount)
                return CommandResult::failure ("set_steps: length must be 1 to " + juce::String (maxStepCount));

            steps = steps.substring (0, length).paddedRight ('.', length);
        }

        // Or switch single steps: {"step": 4, "on": true}.
        if (p.hasProperty ("step"))
        {
            auto index = static_cast<int> (p["step"]);

            if (! juce::isPositiveAndBelow (index, steps.length()))
                return CommandResult::failure ("set_steps: step must be 0 to " + juce::String (steps.length() - 1));

            auto on = p.hasProperty ("on") ? static_cast<bool> (p["on"]) : steps[index] != 'x';
            steps = steps.substring (0, index) + (on ? "x" : ".") + steps.substring (index + 1);
        }

        if (p.hasProperty ("pitch"))
        {
            pitch = static_cast<int> (p["pitch"]);

            if (! juce::isPositiveAndBelow (pitch, 128))
                return CommandResult::failure ("set_steps: pitch must be a MIDI note from 0 to 127");
        }

        channel->state.setProperty (stepsProp, steps, &session.getUndoManager());
        channel->state.setProperty (stepPitchProp, pitch, &session.getUndoManager());
        rebuildPatternClip (session, *channel);

        auto obj = new juce::DynamicObject();
        obj->setProperty ("channel", channel->itemID.toString());
        obj->setProperty ("steps", steps);
        obj->setProperty ("pitch", pitch);
        return CommandResult::success (juce::var (obj));
    }

    CommandResult play (Session& session, const juce::var& p)
    {
        auto& transport = session.getEdit().getTransport();

        if (p.hasProperty ("loop"))
            transport.looping = static_cast<bool> (p["loop"]);

        session.updateLoopRange();
        transport.play (false);
        return CommandResult::success();
    }

    bool isNumber (const juce::var& v)
    {
        return v.isDouble() || v.isInt() || v.isInt64();
    }

    CommandResult setTempo (Session& session, const juce::var& p)
    {
        auto bpm = p["bpm"];

        if (! isNumber (bpm) || static_cast<double> (bpm) < 20.0 || static_cast<double> (bpm) > 300.0)
            return CommandResult::failure ("set_tempo: bpm must be a number from 20 to 300");

        // Clips are placed in beats, so they keep their place in the music at the new tempo.
        session.getEdit().tempoSequence.getTempo (0)->setBpm (static_cast<double> (bpm));
        session.updateLoopRange();
        auto obj = new juce::DynamicObject();
        obj->setProperty ("bpm", session.getEdit().tempoSequence.getBpmAt (te::TimePosition()));
        return CommandResult::success (juce::var (obj));
    }

    CommandResult setTimeSignature (Session& session, const juce::var& p)
    {
        auto numerator = p["numerator"];
        auto denominator = p["denominator"];

        if (! isNumber (numerator) || static_cast<int> (numerator) < 1 || static_cast<int> (numerator) > 32
              || static_cast<double> (numerator) != static_cast<int> (numerator))
            return CommandResult::failure ("set_time_signature: numerator must be a whole number from 1 to 32");

        if (! isNumber (denominator) || ! juce::Array<double> { 1, 2, 4, 8, 16, 32 }.contains (static_cast<double> (denominator)))
            return CommandResult::failure ("set_time_signature: denominator must be 1, 2, 4, 8, 16 or 32");

        auto& timeSig = *session.getEdit().tempoSequence.getTimeSig (0);
        timeSig.numerator = static_cast<int> (numerator);
        timeSig.denominator = static_cast<int> (denominator);
        session.updateLoopRange();

        auto obj = new juce::DynamicObject();
        obj->setProperty ("numerator", timeSig.numerator.get());
        obj->setProperty ("denominator", timeSig.denominator.get());
        return CommandResult::success (juce::var (obj));
    }

    CommandResult setLoop (Session& session, const juce::var& p)
    {
        auto hasStart = p.hasProperty ("start_beat"), hasEnd = p.hasProperty ("end_beat");

        if (hasStart != hasEnd)
            return CommandResult::failure ("set_loop: give both start_beat and end_beat, or neither");

        if (hasStart)
        {
            if (! isNumber (p["start_beat"]) || ! isNumber (p["end_beat"]))
                return CommandResult::failure ("set_loop: start_beat and end_beat must be numbers");

            auto start = static_cast<double> (p["start_beat"]), end = static_cast<double> (p["end_beat"]);

            if (start < 0.0 || end <= start)
                return CommandResult::failure ("set_loop: the loop must start at or after beat 0 and end after it starts");
        }

        if (p.hasProperty ("enabled"))
            session.getEdit().getTransport().looping = static_cast<bool> (p["enabled"]);

        if (hasStart)
            session.setCustomLoop (juce::Range<double> (p["start_beat"], p["end_beat"]));
        else if (static_cast<bool> (p.getProperty ("whole_project", false)))
            session.setCustomLoop (std::nullopt);

        auto beats = session.getLoopBeats();
        auto obj = new juce::DynamicObject();
        obj->setProperty ("enabled", session.getEdit().getTransport().looping.get());
        obj->setProperty ("start_beat", beats.getStart());
        obj->setProperty ("end_beat", beats.getEnd());
        obj->setProperty ("whole_project", ! session.hasCustomLoop());
        return CommandResult::success (juce::var (obj));
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

    registry.add ({ "set_mixer_strip",
                    "Sets a mixer strip's fader, pan, mute and solo. Only the values given change. "
                    "Fader and pan changes are undoable; mute and solo, as in most DAWs, are not. "
                    "Use \"master\" for the master strip (fader and pan only).",
                    schema ({ { "strip", prop ("string", "Mixer strip id or name, or \"master\"") },
                              { "volume_db", prop ("number", "Fader level in dB, -100 to 6 (0 is unity)") },
                              { "pan", prop ("number", "Pan from -1 (left) to 1 (right), 0 is centre") },
                              { "mute", prop ("boolean", "Mute the strip") },
                              { "solo", prop ("boolean", "Solo the strip; other strips go silent while any strip is soloed") } },
                            { "strip" }),
                    true, setMixerStrip });

    registry.add ({ "set_steps",
                    "Sets a channel's step pattern: a row of 16th-note steps from the start of the project that "
                    "plays one pitch and loops with the project. Give the whole row, a new length, or one step.",
                    schema ({ { "channel", prop ("string", "Channel id or name") },
                              { "steps", prop ("string", "The row as 'x' (on) and '.' (off), e.g. \"x...x...x...x...\"; "
                                                         "an array of booleans also works") },
                              { "length", prop ("integer", "Number of steps, 1 to 64 (default 16)") },
                              { "step", prop ("integer", "One step to change, from 0") },
                              { "on", prop ("boolean", "With step: turn it on or off (toggles if omitted)") },
                              { "pitch", prop ("integer", "MIDI note the steps play (default 60, middle C)") } },
                            { "channel" }),
                    true, setSteps });

    registry.add ({ "set_tempo",
                    "Sets the project tempo. Clips keep their positions in beats.",
                    schema ({ { "bpm", prop ("number", "Beats per minute, 20 to 300") } }, { "bpm" }),
                    true, setTempo });

    registry.add ({ "set_time_signature",
                    "Sets the project time signature, e.g. 3/4 or 6/8.",
                    schema ({ { "numerator", prop ("integer", "Beats per bar, 1 to 32") },
                              { "denominator", prop ("integer", "Note value of a beat: 1, 2, 4, 8, 16 or 32") } },
                            { "numerator", "denominator" }),
                    true, setTimeSignature });

    registry.add ({ "set_loop",
                    "Turns looping on or off and sets the loop region in beats. Without a region the loop covers "
                    "the whole project, rounded up to whole bars. Like the play position, this isn't an undo step.",
                    schema ({ { "enabled", prop ("boolean", "Loop playback") },
                              { "start_beat", prop ("number", "Loop start in beats (give with end_beat)") },
                              { "end_beat", prop ("number", "Loop end in beats (give with start_beat)") },
                              { "whole_project", prop ("boolean", "true to go back to looping the whole project") } }),
                    false, setLoop });

    registry.add ({ "play", "Starts playback from the current position. Loops when looping is on (see set_loop).",
                    schema ({ { "loop", prop ("boolean", "Turn looping on or off before playing") } }),
                    false, play });

    registry.add ({ "stop", "Stops playback.", schema ({}), false, stop });

    registry.add ({ "get_state", "Returns the project's channels, mixer strips, routing and transport as data.",
                    schema ({}), false, getState });
}

} // namespace ng
