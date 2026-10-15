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

    /** An instrument a channel can hold: built in ("4osc", "sampler", or "none" for nothing), or a
        scanned plugin, named by its list_plugins id or by its name. "audio" makes an audio channel,
        which plays audio clips instead (create_track only). */
    struct InstrumentChoice
    {
        juce::String builtIn;
        std::optional<juce::PluginDescription> plugin;
    };

    std::variant<InstrumentChoice, juce::String> findInstrument (Session& session, const juce::String& command,
                                                                 const juce::String& name)
    {
        if (name == "4osc" || name == "sampler" || name == "none" || name == "audio")
            return InstrumentChoice { name, {} };

        auto& known = session.getEdit().engine.getPluginManager().knownPluginList;
        std::unique_ptr<juce::PluginDescription> found = known.getTypeForIdentifierString (name);

        if (found == nullptr)
        {
            juce::Array<juce::PluginDescription> byName;

            for (auto& d : known.getTypes())
                if (d.name.equalsIgnoreCase (name))
                    byName.add (d);

            if (byName.size() > 1)
                return command + ": there are " + juce::String (byName.size()) + " plugins called '" + name
                         + "'; use its id from list_plugins";

            if (byName.size() == 1)
                found = std::make_unique<juce::PluginDescription> (byName.getFirst());
        }

        if (found == nullptr)
            return command + ": unknown instrument '" + name + "' (use 4osc, sampler, none, or a plugin from list_plugins)";

        if (! found->isInstrument)
            return command + ": '" + found->name + "' is an effect, not an instrument";

        return InstrumentChoice { {}, *found };
    }

    /** Replaces whatever instrument the channel has. Returns an error if a plugin won't load. */
    juce::String setInstrument (te::Edit& edit, te::AudioTrack& channel, const InstrumentChoice& choice)
    {
        while (auto old = Session::getInstrument (channel))
            old->deleteFromParent();

        te::Plugin::Ptr instrument;

        if (choice.plugin)
            instrument = edit.getPluginCache().createNewPlugin (te::ExternalPlugin::xmlTypeName, *choice.plugin);
        else if (choice.builtIn == "4osc")
            instrument = edit.getPluginCache().createNewPlugin (te::FourOscPlugin::xmlTypeName, {});
        else if (choice.builtIn == "sampler")
            instrument = edit.getPluginCache().createNewPlugin (te::SamplerPlugin::xmlTypeName, {});

        if (instrument == nullptr)
            return choice.builtIn == "none" || choice.builtIn == "audio" ? juce::String() : "couldn't create the instrument";

        if (instrument->isMissing())
        {
            auto* external = dynamic_cast<te::ExternalPlugin*> (instrument.get());
            auto reason = external != nullptr ? external->getLoadError() : juce::String();
            return "couldn't load '" + instrument->getName() + "'" + (reason.isNotEmpty() ? ": " + reason : juce::String());
        }

        channel.pluginList.insertPlugin (instrument, 0, nullptr);
        return {};
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
        auto instrument = params::getString (p, "instrument", "4osc");
        auto name = params::getString (p, "name", (instrument == "audio" ? "Audio " : "Instrument ")
                                                    + juce::String (session.getChannels().size() + 1));
        auto colour = pickColour (session);

        if (p.hasProperty ("colour"))
        {
            auto parsed = parseColour (p["colour"].toString());

            if (! parsed)
                return CommandResult::failure ("create_track: '" + p["colour"].toString() + "' isn't a hex colour like #3c8ce0");

            colour = *parsed;
        }

        auto choice = findInstrument (session, "create_track", instrument);

        if (auto error = std::get_if<juce::String> (&choice))
            return CommandResult::failure (*error);

        // The channel: holds the instrument and its clips.
        auto channel = edit.insertNewAudioTrack (te::TrackInsertPoint::getEndOfTracks (edit), nullptr);

        if (channel == nullptr)
            return CommandResult::failure ("create_track: couldn't create a track");

        session.setRole (*channel, Session::Role::channel);
        channel->setName (name);

        if (instrument == "audio")
            session.setAudioChannel (*channel);
        channel->setColour (colour);

        if (auto error = setInstrument (edit, *channel, std::get<InstrumentChoice> (choice)); error.isNotEmpty())
            return CommandResult::failure ("create_track: " + error);

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

        auto result = idPair ("channel", channel->itemID.toString(), "strip", strip->itemID.toString());

        if (p.hasProperty ("tag"))
        {
            auto tagged = tagChannel (session, *channel, p["tag"].toString(), "create_track");

            if (! tagged.ok)
                return tagged;

            if (tagged.value.hasProperty ("bus"))
                result.getDynamicObject()->setProperty ("bus", tagged.value["bus"]);
        }

        return CommandResult::success (result);
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

        if (Session::isAudioChannel (*channel))
            return CommandResult::failure ("add_midi_clip: '" + channel->getName() + "' is an audio channel; add audio to it with import_audio");

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

        // A multi-output channel takes its output strips with it.
        if (Session::getRole (*track) == Session::Role::channel)
            toDelete.addArray (session.getOutputStrips (*track));

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

        // Sends to a deleted bus would have nowhere to go.
        for (auto t : toDelete)
            if (auto bus = Session::getBusNumber (*t); bus >= 0)
                for (auto s : session.getStrips())
                    if (! toDelete.contains (s))
                        for (auto send : Session::getSends (*s))
                            if (send->getBusNumber() == bus)
                                send->deleteFromParent();

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

        if (Session::isAudioChannel (*channel))
            return CommandResult::failure ("set_instrument: '" + channel->getName() + "' is an audio channel, which has no instrument");

        if (instrument == "audio")
            return CommandResult::failure ("set_instrument: an instrument channel can't become an audio channel; "
                                           "make one with create_track and instrument \"audio\"");

        if (Session::getMultiOutputRack (*channel) != nullptr)
            return CommandResult::failure ("set_instrument: '" + channel->getName() + "' has multiple outputs on; "
                                           "turn them off with set_multi_output first");

        auto choice = findInstrument (session, "set_instrument", instrument);

        if (auto error = std::get_if<juce::String> (&choice))
            return CommandResult::failure (*error);

        if (auto error = setInstrument (session.getEdit(), *channel, std::get<InstrumentChoice> (choice)); error.isNotEmpty())
            return CommandResult::failure ("set_instrument: " + error);

        return CommandResult::success (idPair ("channel", channel->itemID.toString(),
                                               "instrument", Session::getInstrumentId (Session::getInstrument (*channel))));
    }

    CommandResult showPluginWindow (Session& session, const juce::var& p)
    {
        auto ref = params::getString (p, "channel");
        auto channel = session.findTrack (ref, Session::Role::channel);

        if (channel == nullptr)
            return CommandResult::failure ("show_plugin_window: no channel called '" + ref + "'");

        auto instrument = Session::getInstrument (*channel);

        if (instrument == nullptr || (dynamic_cast<te::ExternalPlugin*> (instrument) == nullptr
                                      && dynamic_cast<te::SamplerPlugin*> (instrument) == nullptr))
            return CommandResult::failure ("show_plugin_window: '" + channel->getName() + "' has no plugin instrument or sampler");

        if (instrument->isMissing())
            return CommandResult::failure ("show_plugin_window: '" + instrument->getName() + "' isn't loaded");

        instrument->showWindowExplicitly();
        return CommandResult::success (idPair ("channel", channel->itemID.toString(),
                                               "shown", instrument->windowState->isWindowShowing()));
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
    const juce::Identifier stepsProp ("ngSteps"), stepPitchProp ("ngStepPitch"), patternClipProp ("ngPattern"),
                          patternBeatsProp ("ngPatternBeats");
    constexpr int maxStepCount = 64;
    constexpr double beatsPerStep = 0.25;

    /** Brings a channel's pattern clips in line with its steps. A pattern can be placed on the
        timeline several times (duplicate_clip); every copy plays the same steps, so each one is
        refilled where it stands. A copy that was resized (resize_clip) keeps its own length, as in FL
        Studio: the steps play once from its start, cut off if it's shorter, with silence after
        them if it's longer. The rest follow the steps' length.
        With no copies left, the pattern goes back at the start. */
    void rebuildPatternClip (Session& session, te::AudioTrack& channel)
    {
        auto& edit = session.getEdit();
        auto* um = &session.getUndoManager();
        auto steps = Session::getSteps (channel);

        juce::Array<te::MidiClip*> instances;

        for (auto clip : channel.getClips())
            if (Session::isPatternClip (*clip))
                if (auto midi = dynamic_cast<te::MidiClip*> (clip))
                    instances.add (midi);

        if (! steps.containsChar ('x'))
        {
            for (auto clip : instances)
                clip->removeFromParent();

            return;
        }

        auto& tempo = edit.tempoSequence;
        auto patternBeats = steps.length() * beatsPerStep;
        auto lengthBeats = te::BeatDuration::fromBeats (patternBeats);

        if (instances.isEmpty())
        {
            auto clip = channel.insertMIDIClip ("Pattern", { tempo.toTime (te::BeatPosition()), tempo.toTime (te::BeatPosition() + lengthBeats) }, nullptr);

            if (clip == nullptr)
                return;

            clip->state.setProperty (patternClipProp, true, um);
            instances.add (clip.get());
        }

        auto pitch = Session::getStepPitch (channel);

        for (auto clip : instances)
        {
            auto startBeat = tempo.toBeats (clip->getPosition().getStart());
            auto clipBeats = clip->state.hasProperty (patternBeatsProp) ? static_cast<double> (clip->state[patternBeatsProp])
                                                                         : patternBeats;
            clip->setPosition ({ { clip->getPosition().getStart(), tempo.toTime (startBeat + te::BeatDuration::fromBeats (clipBeats)) },
                                 te::TimeDuration() });
            clip->getSequence().clear (um);

            for (int i = 0; i < steps.length(); ++i)
                if (auto beat = i * beatsPerStep; steps[i] == 'x' && beat < clipBeats - 1.0e-6)
                    clip->getSequence().addNote (pitch, te::BeatPosition::fromBeats (beat),
                                                 te::BeatDuration::fromBeats (beatsPerStep), 100, 0, um);
        }
    }

    CommandResult setSteps (Session& session, const juce::var& p)
    {
        auto ref = params::getString (p, "channel");
        auto channel = session.findTrack (ref, Session::Role::channel);

        if (channel == nullptr)
            return CommandResult::failure ("set_steps: no channel called '" + ref + "'");

        if (Session::isAudioChannel (*channel))
            return CommandResult::failure ("set_steps: '" + channel->getName() + "' is an audio channel, which has no step pattern");

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

    CommandResult setPosition (Session& session, const juce::var& p)
    {
        if (! p.hasProperty ("beat"))
            return CommandResult::failure ("set_position: give the beat to move the playhead to");

        auto beat = static_cast<double> (p["beat"]);

        if (! (beat >= 0.0 && beat < 100000.0))
            return CommandResult::failure ("set_position: beat must be 0 or more");

        auto& edit = session.getEdit();
        edit.getTransport().setPosition (edit.tempoSequence.toTime (te::BeatPosition::fromBeats (beat)));
        auto result = new juce::DynamicObject();
        result->setProperty ("beat", beat);
        return CommandResult::success (juce::var (result));
    }

    CommandResult setMetronome (Session& session, const juce::var& p)
    {
        auto& edit = session.getEdit();

        if (p.hasProperty ("enabled"))
            edit.clickTrackEnabled = static_cast<bool> (p["enabled"]);
        else
            edit.clickTrackEnabled = ! edit.clickTrackEnabled.get();

        // Tracktion clicks only while recording by default; ours clicks whenever it's on.
        edit.clickTrackRecordingOnly = false;
        edit.clickTrackEmphasiseBars = true;

        auto obj = new juce::DynamicObject();
        obj->setProperty ("enabled", edit.clickTrackEnabled.get());
        return CommandResult::success (juce::var (obj));
    }

    CommandResult stop (Session& session, const juce::var&)
    {
        auto& transport = session.getEdit().getTransport();

        if (! transport.isRecording())
        {
            transport.stop (false, false);
            return CommandResult::success();
        }

        // What was recorded lands as clips when recording stops: one undo step, tidied
        // up like any other command so the first Undo removes it.
        auto& um = session.getUndoManager();
        um.beginNewTransaction ("Record");
        transport.stop (false, false);
        session.sortTrackContents();
        um.beginNewTransaction();
        return CommandResult::success();
    }

    CommandResult armChannel (Session& session, const juce::var& p)
    {
        auto ref = params::getString (p, "channel");
        auto armed = static_cast<bool> (p.getProperty ("armed", true));
        auto channel = session.findTrack (ref, Session::Role::channel);

        if (armed && channel == nullptr)
            return CommandResult::failure ("arm_channel: no channel called '" + ref + "'");

        // Audio channels arm for their audio input, any number at once.
        if (channel != nullptr && Session::isAudioChannel (*channel))
        {
            if (armed && Session::getAudioInput (*channel).isEmpty())
                return CommandResult::failure ("arm_channel: '" + channel->getName() + "' has no audio input; "
                                               "pick one with set_audio_input first");

            session.setAudioArmed (*channel, armed);
        }
        else if (armed)
        {
            session.setArmedChannel (channel);
        }
        else if (channel == nullptr || session.getArmedChannel() == channel)
        {
            session.setArmedChannel (nullptr);

            if (channel == nullptr)
                for (auto c : session.getArmedAudioChannels())
                    session.setAudioArmed (*c, false);
        }

        auto obj = new juce::DynamicObject();
        auto now = session.getArmedChannel();
        juce::Array<juce::var> audio;

        for (auto c : session.getArmedAudioChannels())
            audio.add (c->itemID.toString());

        obj->setProperty ("armed_channel", now != nullptr ? juce::var (now->itemID.toString()) : juce::var());
        obj->setProperty ("armed_audio", audio);
        obj->setProperty ("midi_inputs", session.getMidiInputNames());
        return CommandResult::success (juce::var (obj));
    }

    CommandResult record (Session& session, const juce::var&)
    {
        auto armedAudio = session.getArmedAudioChannels();

        if (session.getArmedChannel() == nullptr && armedAudio.isEmpty())
            return CommandResult::failure ("record: arm a channel first (arm_channel), so the recording has somewhere to go");

        // Every armed audio channel needs its input to be there.
        auto inputs = session.getAudioInputNames();

        for (auto c : armedAudio)
            if (! inputs.contains (Session::getAudioInput (*c)))
                return CommandResult::failure ("record: '" + c->getName() + "' records from '" + Session::getAudioInput (*c)
                                                 + "', which the audio device doesn't have"
                                                 + (inputs.isEmpty() ? juce::String ("; pick an audio device with inputs in Audio settings")
                                                                     : "; pick one of " + inputs.joinIntoString (", ")));

        auto& transport = session.getEdit().getTransport();

        if (transport.isRecording())
            return CommandResult::failure ("record: already recording");

        session.routeMidiInputs();
        session.routeAudioInputs();
        session.updateLoopRange();
        transport.record (false, true);
        return CommandResult::success();
    }

    //==============================================================================
    /** A clip on any channel, by its id. */
    struct FoundClip
    {
        te::Clip* clip = nullptr;
        te::AudioTrack* channel = nullptr;
    };

    FoundClip findClip (Session& session, const juce::String& id)
    {
        for (auto channel : session.getChannels())
            for (auto clip : channel->getClips())
                if (clip->itemID.toString() == id)
                    return { clip, channel };

        return {};
    }

    double rounded (double beats)
    {
        return std::round (beats * 1.0e6) / 1.0e6;
    }

    juce::var describeClip (Session& session, te::Clip& clip)
    {
        auto& tempo = session.getEdit().tempoSequence;
        auto start = tempo.toBeats (clip.getPosition().getStart()).inBeats();
        auto end = tempo.toBeats (clip.getPosition().getEnd()).inBeats();

        auto obj = new juce::DynamicObject();
        obj->setProperty ("clip", clip.itemID.toString());

        if (auto track = clip.getTrack())
            obj->setProperty ("channel", track->itemID.toString());

        obj->setProperty ("start_beat", rounded (start));
        obj->setProperty ("length_beats", rounded (end - start));
        return juce::var (obj);
    }

    /** Looks up the "clip" param, or explains what's wrong. */
    std::optional<CommandResult> requireClip (Session& session, const juce::String& command,
                                              const juce::var& p, FoundClip& found)
    {
        auto id = params::getString (p, "clip");

        if (id.isEmpty())
            return CommandResult::failure (command + ": give the clip's id (from get_state)");

        found = findClip (session, id);

        if (found.clip == nullptr)
            return CommandResult::failure (command + ": no clip with id '" + id + "'");

        return std::nullopt;
    }

    constexpr double minClipBeats = 1.0 / 16.0;

    CommandResult moveClip (Session& session, const juce::var& p)
    {
        FoundClip found;

        if (auto error = requireClip (session, "move_clip", p, found))
            return *error;

        auto& tempo = session.getEdit().tempoSequence;
        auto startBeat = params::getDouble (p, "start_beat", tempo.toBeats (found.clip->getPosition().getStart()).inBeats());

        if (startBeat < 0.0)
            return CommandResult::failure ("move_clip: start_beat can't be before the start of the project");

        if (p.hasProperty ("channel"))
        {
            auto channelRef = params::getString (p, "channel");
            auto target = session.findTrack (channelRef, Session::Role::channel);

            if (target == nullptr)
                return CommandResult::failure ("move_clip: no channel called '" + channelRef + "'");

            if (target != found.channel)
            {
                if (Session::isPatternClip (*found.clip))
                    return CommandResult::failure ("move_clip: a step pattern belongs to its channel; it can only move in time");

                if (Session::isAudioChannel (*target) != Session::isAudioChannel (*found.channel))
                    return CommandResult::failure (Session::isAudioChannel (*target)
                                                     ? "move_clip: '" + target->getName() + "' is an audio channel; MIDI clips go on instrument channels"
                                                     : "move_clip: '" + target->getName() + "' is an instrument channel; audio clips go on audio channels");

                if (! found.clip->moveTo (*target))
                    return CommandResult::failure ("move_clip: couldn't move the clip to '" + channelRef + "'");
            }
        }

        found.clip->setStart (tempo.toTime (te::BeatPosition::fromBeats (startBeat)), false, true);
        return CommandResult::success (describeClip (session, *found.clip));
    }

    CommandResult resizeClip (Session& session, const juce::var& p)
    {
        FoundClip found;

        if (auto error = requireClip (session, "resize_clip", p, found))
            return *error;

        if (! p.hasProperty ("start_beat") && ! p.hasProperty ("end_beat"))
            return CommandResult::failure ("resize_clip: give start_beat, end_beat or both");

        auto& tempo = session.getEdit().tempoSequence;
        auto& clip = *found.clip;
        auto start = tempo.toBeats (clip.getPosition().getStart()).inBeats();
        auto end = tempo.toBeats (clip.getPosition().getEnd()).inBeats();

        // A step pattern copy gets its own length; its steps play once from its start.
        if (Session::isPatternClip (clip))
        {
            auto newStart = params::getDouble (p, "start_beat", start);
            auto newEnd = params::getDouble (p, "end_beat", end);

            if (newStart < 0.0 || newEnd - newStart < beatsPerStep - 1.0e-6)
                return CommandResult::failure ("resize_clip: a step pattern can't be shorter than one step or start before the project");

            auto* channel = dynamic_cast<te::AudioTrack*> (clip.getTrack());

            if (channel == nullptr)
                return CommandResult::failure ("resize_clip: the pattern isn't on a channel");

            auto* um = &session.getUndoManager();
            auto stepsBeats = Session::getSteps (*channel).length() * beatsPerStep;

            if (std::abs ((newEnd - newStart) - stepsBeats) < 1.0e-6)
                clip.state.removeProperty (patternBeatsProp, um);
            else
                clip.state.setProperty (patternBeatsProp, newEnd - newStart, um);

            clip.setStart (tempo.toTime (te::BeatPosition::fromBeats (newStart)), false, true);
            rebuildPatternClip (session, *channel);
            return CommandResult::success (describeClip (session, clip));
        }

        // The start can come back as far as the clip's content goes, but no further.
        auto contentStart = start - clip.getOffsetInBeats().inBeats();
        auto newStart = std::max (contentStart, params::getDouble (p, "start_beat", start));
        auto newEnd = params::getDouble (p, "end_beat", end);

        // An audio clip can't run past the end of its file.
        if (auto wave = dynamic_cast<te::WaveAudioClip*> (&clip); wave != nullptr && ! wave->isLooping())
            newEnd = std::min (newEnd, session.getSourceEndBeat (*wave));

        if (newStart < 0.0 || newEnd - newStart < minClipBeats)
            return CommandResult::failure ("resize_clip: the clip would be shorter than a 64th note or start before the project");

        // Edges move; the notes stay where they are in the song.
        clip.setEnd (tempo.toTime (te::BeatPosition::fromBeats (newEnd)), true);
        clip.setStart (tempo.toTime (te::BeatPosition::fromBeats (newStart)), true, false);
        return CommandResult::success (describeClip (session, clip));
    }

    CommandResult splitClip (Session& session, const juce::var& p)
    {
        FoundClip found;

        if (auto error = requireClip (session, "split_clip", p, found))
            return *error;

        if (Session::isPatternClip (*found.clip))
            return CommandResult::failure ("split_clip: a step pattern can't be split; change its steps instead");

        if (! p.hasProperty ("beat"))
            return CommandResult::failure ("split_clip: give the beat to split at");

        auto& tempo = session.getEdit().tempoSequence;
        auto beat = params::getDouble (p, "beat", 0.0);
        auto start = tempo.toBeats (found.clip->getPosition().getStart()).inBeats();
        auto end = tempo.toBeats (found.clip->getPosition().getEnd()).inBeats();

        if (beat - start < minClipBeats || end - beat < minClipBeats)
            return CommandResult::failure ("split_clip: beat must be inside the clip (" + juce::String (rounded (start)) + " to "
                                             + juce::String (rounded (end)) + ")");

        auto second = found.channel->splitClip (*found.clip, tempo.toTime (te::BeatPosition::fromBeats (beat)));

        if (second == nullptr)
            return CommandResult::failure ("split_clip: couldn't split the clip");

        return CommandResult::success (juce::Array<juce::var> { describeClip (session, *found.clip), describeClip (session, *second) });
    }

    CommandResult duplicateClip (Session& session, const juce::var& p)
    {
        FoundClip found;

        if (auto error = requireClip (session, "duplicate_clip", p, found))
            return *error;

        auto& tempo = session.getEdit().tempoSequence;
        auto startBeat = params::getDouble (p, "start_beat", tempo.toBeats (found.clip->getPosition().getEnd()).inBeats());

        if (startBeat < 0.0)
            return CommandResult::failure ("duplicate_clip: start_beat can't be before the start of the project");

        auto copy = te::duplicateClip (*found.clip);

        if (copy == nullptr)
            return CommandResult::failure ("duplicate_clip: couldn't copy the clip");

        copy->setStart (tempo.toTime (te::BeatPosition::fromBeats (startBeat)), false, true);
        return CommandResult::success (describeClip (session, *copy));
    }

    CommandResult deleteClip (Session& session, const juce::var& p)
    {
        FoundClip found;

        if (auto error = requireClip (session, "delete_clip", p, found))
            return *error;

        auto result = describeClip (session, *found.clip);
        found.clip->removeFromParent();
        return CommandResult::success (result);
    }

    //==============================================================================
    juce::var notesToVar (const juce::Array<ClipNote>& notes)
    {
        juce::Array<juce::var> list;

        for (auto& n : notes)
        {
            auto obj = new juce::DynamicObject();
            obj->setProperty ("pitch", n.pitch);
            obj->setProperty ("start", rounded (n.start));
            obj->setProperty ("length", rounded (n.length));
            obj->setProperty ("velocity", n.velocity);
            list.add (juce::var (obj));
        }

        return list;
    }

    /** Looks up the "clip" param as a MIDI clip whose notes can be edited, or explains what's wrong. */
    std::optional<CommandResult> requireNoteClip (Session& session, const juce::String& command,
                                                  const juce::var& p, te::MidiClip*& midi)
    {
        FoundClip found;

        if (auto error = requireClip (session, command, p, found))
            return error;

        midi = dynamic_cast<te::MidiClip*> (found.clip);

        if (midi == nullptr)
            return CommandResult::failure (command + ": that clip has no notes to edit");

        return std::nullopt;
    }

    CommandResult getNotes (Session& session, const juce::var& p)
    {
        te::MidiClip* midi = nullptr;

        if (auto error = requireNoteClip (session, "get_notes", p, midi))
            return *error;

        auto result = describeClip (session, *midi);
        result.getDynamicObject()->setProperty ("pattern", Session::isPatternClip (*midi));
        result.getDynamicObject()->setProperty ("notes", notesToVar (Session::getNotes (*midi)));
        return CommandResult::success (result);
    }

    CommandResult setNotes (Session& session, const juce::var& p)
    {
        te::MidiClip* midi = nullptr;

        if (auto error = requireNoteClip (session, "set_notes", p, midi))
            return *error;

        if (Session::isPatternClip (*midi))
            return CommandResult::failure ("set_notes: a step pattern's notes follow its steps; change them with set_steps");

        auto list = p["notes"];

        if (! list.isArray())
            return CommandResult::failure ("set_notes: give notes as an array of {pitch, start, length, velocity} (it can be empty)");

        // Notes are timed from the clip's start; Tracktion times them from where its content starts.
        auto offset = midi->getOffsetInBeats().inBeats();
        juce::Array<ClipNote> notes;

        for (auto& n : *list.getArray())
        {
            ClipNote note;
            note.pitch = static_cast<int> (n.getProperty ("pitch", -1));
            note.start = static_cast<double> (n.getProperty ("start", 0.0));
            note.length = static_cast<double> (n.getProperty ("length", 1.0));
            note.velocity = static_cast<int> (n.getProperty ("velocity", 100));

            if (! n.isObject() || ! juce::isPositiveAndBelow (note.pitch, 128) || ! (note.length >= minClipBeats / 4.0)
                  || ! (note.start + offset >= -1.0e-9) || ! juce::isPositiveAndBelow (note.velocity - 1, 127))
                return CommandResult::failure ("set_notes: invalid note " + juce::JSON::toString (n, true)
                                                 + " (pitch 0-127, length above 0, velocity 1-127, not before the clip's content)");

            notes.add (note);
        }

        auto* um = &session.getUndoManager();
        auto& sequence = midi->getSequence();
        sequence.clear (um);

        for (auto& n : notes)
            sequence.addNote (n.pitch, te::BeatPosition::fromBeats (std::max (0.0, n.start + offset)),
                              te::BeatDuration::fromBeats (n.length), n.velocity, 0, um);

        auto result = describeClip (session, *midi);
        result.getDynamicObject()->setProperty ("notes", notesToVar (Session::getNotes (*midi)));
        return CommandResult::success (result);
    }

    CommandResult quantizeNotes (Session& session, const juce::var& p)
    {
        te::MidiClip* midi = nullptr;

        if (auto error = requireNoteClip (session, "quantize_notes", p, midi))
            return *error;

        if (Session::isPatternClip (*midi))
            return CommandResult::failure ("quantize_notes: a step pattern is always on its steps");

        auto grid = params::getDouble (p, "grid", 0.25);

        if (! (grid >= 1.0 / 64.0 && grid <= 16.0))
            return CommandResult::failure ("quantize_notes: grid must be from 1/64 to 16 beats (0.25 is a 16th note)");

        auto strength = params::getDouble (p, "strength", 1.0);

        if (! (strength > 0.0 && strength <= 1.0))
            return CommandResult::failure ("quantize_notes: strength must be above 0 and at most 1");

        auto notes = Session::getNotes (*midi);
        juce::Array<bool> chosen;
        chosen.insertMultiple (0, ! p.hasProperty ("notes"), notes.size());

        if (p.hasProperty ("notes"))
        {
            if (! p["notes"].isArray())
                return CommandResult::failure ("quantize_notes: notes must be an array of indexes, as listed by get_notes");

            for (auto& i : *p["notes"].getArray())
            {
                if (! isNumber (i) || ! juce::isPositiveAndBelow (static_cast<int> (i), notes.size()))
                    return CommandResult::failure ("quantize_notes: no note " + i.toString() + " (the clip has "
                                                     + juce::String (notes.size()) + ")");

                chosen.set (static_cast<int> (i), true);
            }
        }

        auto ends = static_cast<bool> (p.getProperty ("ends", false));
        auto offset = midi->getOffsetInBeats().inBeats();
        auto towards = [grid, strength] (double beat) { return beat + (std::round (beat / grid) * grid - beat) * strength; };
        int changed = 0;

        for (int i = 0; i < notes.size(); ++i)
        {
            if (! chosen[i])
                continue;

            auto& n = notes.getReference (i);
            auto end = n.start + n.length;
            auto newStart = std::max (-offset, towards (n.start));
            auto newEnd = ends ? std::max (newStart + grid * strength, towards (end)) : newStart + n.length;

            if (std::abs (newStart - n.start) > 1.0e-9 || std::abs (newEnd - end) > 1.0e-9)
                ++changed;

            n.start = newStart;
            n.length = newEnd - newStart;
        }

        auto* um = &session.getUndoManager();
        auto& sequence = midi->getSequence();
        sequence.clear (um);

        for (auto& n : notes)
            sequence.addNote (n.pitch, te::BeatPosition::fromBeats (n.start + offset),
                              te::BeatDuration::fromBeats (n.length), n.velocity, 0, um);

        // Where the quantized notes ended up in the new order, e.g. to keep them selected.
        auto after = Session::getNotes (*midi);
        juce::Array<bool> taken;
        taken.insertMultiple (0, false, after.size());
        juce::Array<juce::var> quantized;

        for (int i = 0; i < notes.size(); ++i)
        {
            if (! chosen[i])
                continue;

            for (int j = 0; j < after.size(); ++j)
            {
                auto& a = after.getReference (j);
                auto& n = notes.getReference (i);

                if (! taken[j] && a.pitch == n.pitch && std::abs (a.start - n.start) < 1.0e-6 && std::abs (a.length - n.length) < 1.0e-6)
                {
                    taken.set (j, true);
                    quantized.add (j);
                    break;
                }
            }
        }

        auto result = describeClip (session, *midi);
        result.getDynamicObject()->setProperty ("moved", changed);
        result.getDynamicObject()->setProperty ("quantized", quantized);
        result.getDynamicObject()->setProperty ("notes", notesToVar (after));
        return CommandResult::success (result);
    }

    juce::var findById (const juce::var& list, const juce::String& id)
    {
        if (auto* array = list.getArray())
            for (auto& item : *array)
                if (item["id"].toString() == id)
                    return item;

        return {};
    }

    juce::var pick (const juce::var& from, juce::StringArray keys)
    {
        auto obj = new juce::DynamicObject();

        for (auto& key : keys)
            if (from.hasProperty (juce::Identifier (key)))
                obj->setProperty (key, from[juce::Identifier (key)]);

        return juce::var (obj);
    }

    juce::var summarise (const juce::var& state)
    {
        auto root = pick (state, { "project", "tempo_bpm", "time_signature" });
        root.getDynamicObject()->setProperty ("transport", pick (state["transport"], { "playing", "position", "position_beats", "looping",
                                                                                     "loop_start_beat", "loop_end_beat", "recording" }));
        juce::Array<juce::var> channels, strips;

        for (auto& c : *state["channels"].getArray())
        {
            auto obj = pick (c, { "id", "name", "kind", "instrument", "output", "tag", "armed" });
            juce::Array<juce::var> clips;

            for (auto& clip : *c["clips"].getArray())
                clips.add (pick (clip, { "id", "type", "start_beat", "length_beats", "notes" }));

            obj.getDynamicObject()->setProperty ("clips", clips);
            channels.add (obj);
        }

        for (auto& s : *state["strips"].getArray())
        {
            auto obj = pick (s, { "id", "name", "output", "bus", "tag", "volume_db", "mute", "solo" });
            juce::Array<juce::var> effects;

            for (auto& e : *s["effects"].getArray())
                effects.add (e["type"]);

            obj.getDynamicObject()->setProperty ("effects", effects);
            strips.add (obj);
        }

        root.getDynamicObject()->setProperty ("channels", channels);
        root.getDynamicObject()->setProperty ("strips", strips);
        return root;
    }

    CommandResult getState (Session& session, const juce::var& p)
    {
        auto state = session.toVar();
        auto channelRef = params::getString (p, "channel");
        auto stripRef = params::getString (p, "strip");
        auto clipRef = params::getString (p, "clip");
        auto view = params::getString (p, "view", "full");

        if (channelRef.isNotEmpty())
        {
            auto channel = session.findTrack (channelRef, Session::Role::channel);

            if (channel == nullptr)
                return CommandResult::failure ("get_state: no channel called '" + channelRef + "'");

            auto result = findById (state["channels"], channel->itemID.toString()).clone();

            if (auto dest = session.getDestinationStrip (*channel))
                result.getDynamicObject()->setProperty ("strip", findById (state["strips"], dest->itemID.toString()));

            return CommandResult::success (result);
        }

        if (stripRef.isNotEmpty())
        {
            if (stripRef == "master")
                return CommandResult::success (state["master"]);

            auto strip = session.findTrack (stripRef, Session::Role::strip);

            if (strip == nullptr)
                return CommandResult::failure ("get_state: no mixer strip called '" + stripRef + "'");

            return CommandResult::success (findById (state["strips"], strip->itemID.toString()));
        }

        if (clipRef.isNotEmpty())
        {
            auto clip = session.findClip (clipRef);

            if (clip == nullptr)
                return CommandResult::failure ("get_state: no clip with id '" + clipRef + "'");

            auto channel = dynamic_cast<te::AudioTrack*> (clip->getTrack());
            auto result = findById (findById (state["channels"], channel != nullptr ? channel->itemID.toString() : juce::String())["clips"],
                                    clip->itemID.toString()).clone();

            if (! result.isObject())
                return CommandResult::failure ("get_state: no clip with id '" + clipRef + "'");

            result.getDynamicObject()->setProperty ("channel", channel->itemID.toString());

            if (auto midi = dynamic_cast<te::MidiClip*> (clip))
                result.getDynamicObject()->setProperty ("notes", notesToVar (Session::getNotes (*midi)));

            return CommandResult::success (result);
        }

        if (view == "full")
            return CommandResult::success (state);

        if (view == "summary")
            return CommandResult::success (summarise (state));

        if (view == "mixer")
            return CommandResult::success (pick (state, { "strips", "master" }));

        if (view == "transport")
            return CommandResult::success (pick (state, { "transport", "tempo_bpm", "time_signature", "project" }));

        return CommandResult::failure ("get_state: view is \"full\", \"summary\", \"mixer\" or \"transport\"");
    }

    //==============================================================================
    juce::File fileFromParam (const juce::var& path)
    {
        auto text = path.toString().trim();
        return text.isEmpty() ? juce::File() : juce::File::getCurrentWorkingDirectory().getChildFile (text);
    }

    juce::var describeProject (Session& session)
    {
        auto obj = new juce::DynamicObject();
        obj->setProperty ("name", session.getProjectName());
        obj->setProperty ("path", session.getFile().getFullPathName());
        return juce::var (obj);
    }

    std::optional<CommandResult> refuseToDiscard (Session& session, const juce::var& p)
    {
        if (session.hasUnsavedChanges() && ! static_cast<bool> (p["discard_changes"]))
            return CommandResult::failure ("The project has unsaved changes. Save it first (save_project), "
                                           "or pass discard_changes: true to lose them");

        return std::nullopt;
    }

    CommandResult newProject (Session& session, const juce::var& p)
    {
        if (auto refusal = refuseToDiscard (session, p))
            return *refusal;

        session.newProject();
        return CommandResult::success (describeProject (session));
    }

    CommandResult openProject (Session& session, const juce::var& p)
    {
        auto file = fileFromParam (p["path"]);

        if (file == juce::File())
            return CommandResult::failure ("open_project: give the path of the project file");

        if (auto refusal = refuseToDiscard (session, p))
            return *refusal;

        auto error = session.openProject (file);

        if (error.isNotEmpty())
            return CommandResult::failure ("open_project: " + error);

        return CommandResult::success (describeProject (session));
    }

    CommandResult saveProject (Session& session, const juce::var& p)
    {
        auto error = session.saveProject (fileFromParam (p["path"]));

        if (error.isNotEmpty())
            return CommandResult::failure ("save_project: " + error);

        return CommandResult::success (describeProject (session));
    }
}

//==============================================================================
void registerProjectCommands (CommandRegistry& registry)
{
    registry.add ({ "create_track",
                    "Adds an instrument channel and a mixer strip with the same name and colour, linked to it.",
                    schema ({ { "name", prop ("string", "Name for the channel and its strip") },
                              { "colour", prop ("string", "Hex colour such as #3c8ce0; picked automatically if omitted") },
                              { "instrument", prop ("string", "\"4osc\" (built-in synth, default), \"sampler\" (plays samples: load them with add_sample or load_drum_kit), \"none\", "
                                                              "\"audio\" (an audio channel, for audio clips: add them with import_audio), or a plugin's id or name from list_plugins") },
                              { "tag", prop ("string", "What it is, like \"drums\", \"bass\" or \"vox\": its strip then plays into that tag's bus, "
                                                       "which is created if needed (see tag_channel). The result's bus is the bus's id") } }),
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
                    "still feed that strip; anything that fed a deleted strip is sent to master, and sends to a deleted bus are removed.",
                    schema ({ { "track", prop ("string", "Channel or mixer strip id or name") } }, { "track" }),
                    true, deleteTrack });

    registry.add ({ "show_plugin_window",
                    "Opens the editor window of a channel's plugin instrument or sampler (or brings it to the front). "
                    "Not an undo step; shown is false when there's no screen to show it on.",
                    schema ({ { "channel", prop ("string", "Channel id or name") } }, { "channel" }),
                    false, showPluginWindow });

    registry.add ({ "set_instrument",
                    "Replaces a channel's instrument: the built-in \"4osc\" synth, the \"sampler\", \"none\", or a scanned plugin instrument. "
                    "Fails if the plugin won't load, leaving the old instrument in place.",
                    schema ({ { "channel", prop ("string", "Channel id or name") },
                              { "instrument", prop ("string", "\"4osc\" (built-in synth), \"sampler\", \"none\", or a plugin's id or name from list_plugins") } },
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

    registry.add ({ "move_clip",
                    "Moves a clip to a new start beat, and optionally to another channel. A step pattern can only move in time.",
                    schema ({ { "clip", prop ("string", "Clip id, from get_state") },
                              { "start_beat", prop ("number", "New start, in beats (default: where it is)") },
                              { "channel", prop ("string", "Channel id or name to move it to") } },
                            { "clip" }),
                    true, moveClip });

    registry.add ({ "resize_clip",
                    "Moves a clip's start and/or end; its notes stay where they are in the song. "
                    "A step pattern copy can be resized too: its steps play once from its start, cut off or followed by silence.",
                    schema ({ { "clip", prop ("string", "Clip id, from get_state") },
                              { "start_beat", prop ("number", "New start, in beats") },
                              { "end_beat", prop ("number", "New end, in beats") } },
                            { "clip" }),
                    true, resizeClip });

    registry.add ({ "split_clip",
                    "Splits a clip in two at a beat inside it. Returns both halves.",
                    schema ({ { "clip", prop ("string", "Clip id, from get_state") },
                              { "beat", prop ("number", "Where to split, in beats from the start of the project") } },
                            { "clip", "beat" }),
                    true, splitClip });

    registry.add ({ "duplicate_clip",
                    "Copies a clip, by default to just after itself. A copied step pattern keeps following the channel's steps.",
                    schema ({ { "clip", prop ("string", "Clip id, from get_state") },
                              { "start_beat", prop ("number", "Where the copy starts, in beats (default: the clip's end)") } },
                            { "clip" }),
                    true, duplicateClip });

    registry.add ({ "delete_clip",
                    "Deletes a clip.",
                    schema ({ { "clip", prop ("string", "Clip id, from get_state") } }, { "clip" }),
                    true, deleteClip });

    registry.add ({ "get_notes",
                    "Lists a MIDI clip's notes. Start and length are in beats from the clip's start.",
                    schema ({ { "clip", prop ("string", "Clip id, from get_state") } }, { "clip" }),
                    false, getNotes });

    registry.add ({ "set_notes",
                    "Replaces all of a MIDI clip's notes, e.g. after drawing in the piano roll. Start and length are "
                    "in beats from the clip's start; notes past the clip's end are kept but don't play.",
                    schema ({ { "clip", prop ("string", "Clip id, from get_state") },
                              { "notes", prop ("array", "Notes as {pitch, start, length, velocity}; velocity defaults to 100") } },
                            { "clip", "notes" }),
                    true, setNotes });

    registry.add ({ "quantize_notes",
                    "Moves a MIDI clip's notes onto a grid. Note lengths stay the same unless ends is true.",
                    schema ({ { "clip", prop ("string", "Clip id, from get_state") },
                              { "grid", prop ("number", "Grid in beats: 1 is a quarter note, 0.25 a 16th (default)") },
                              { "notes", prop ("array", "Indexes of the notes to quantize, as listed by get_notes (default: all)") },
                              { "strength", prop ("number", "How far towards the grid, above 0 up to 1 (default 1)") },
                              { "ends", prop ("boolean", "Also snap where notes end (default false)") } },
                            { "clip" }),
                    true, quantizeNotes });

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

    registry.add ({ "stop", "Stops playback. When recording, the recorded notes become clips on the armed "
                            "channel, as one undo step.", schema ({}), false, stop });

    registry.add ({ "arm_channel",
                    "Arms a channel for recording. An instrument channel arms for MIDI input: MIDI keyboards (and the "
                    "on-screen keyboard) play it, and arming one disarms the other instrument channels. An audio channel "
                    "arms for its audio input (set_audio_input), and any number can be armed together. "
                    "Leave out channel with armed false to disarm everything. Not an undo step.",
                    schema ({ { "channel", prop ("string", "Channel id or name") },
                              { "armed", prop ("boolean", "false to disarm (default true)") } }),
                    false, armChannel });

    registry.add ({ "record",
                    "Starts recording from the playhead: MIDI into the armed instrument channel and audio into each armed "
                    "audio channel. stop finishes the recording; what was recorded becomes clips, as one undo step.",
                    schema ({}), false, record });

    registry.add ({ "set_metronome",
                    "Turns the metronome (a click on every beat, louder on the bar) on or off. Like the loop, "
                    "this isn't an undo step.",
                    schema ({ { "enabled", prop ("boolean", "On or off (toggles if omitted)") } }),
                    false, setMetronome });

    registry.add ({ "set_position", "Moves the playhead. Like play and stop, this isn't an undo step.",
                    schema ({ { "beat", prop ("number", "Where to move the playhead, in beats from the start") } }, { "beat" }),
                    false, setPosition });

    registry.add ({ "new_project",
                    "Closes the open project and starts an empty one. Refuses if there are unsaved changes, "
                    "unless discard_changes is true. Not an undo step: it clears the undo history.",
                    schema ({ { "discard_changes", prop ("boolean", "Lose unsaved changes without asking") } }),
                    false, newProject });

    registry.add ({ "open_project",
                    "Opens a project file (" + juce::String (projectFileExtension) + "), replacing the open project. "
                    "Refuses if there are unsaved changes, unless discard_changes is true. Clears the undo history.",
                    schema ({ { "path", prop ("string", "Path of the project file") },
                              { "discard_changes", prop ("boolean", "Lose unsaved changes without asking") } },
                            { "path" }),
                    false, openProject });

    registry.add ({ "save_project",
                    "Saves the project. Without a path it saves to the project's own file (Save); with a path it "
                    "saves there and that becomes the project's file (Save As).",
                    schema ({ { "path", prop ("string", "Where to save, e.g. ~/Music/beat" + juce::String (projectFileExtension)) } }),
                    false, saveProject });

    registry.add ({ "get_state",
                    "Returns the project's channels, mixer strips, routing and transport as data. That can be long, so it can "
                    "return less: one channel (with the strip it plays into), one mixer strip, one clip (with its notes), or a "
                    "view: \"summary\" (names, ids, routing and clips without the detail), \"mixer\" or \"transport\".",
                    schema ({ { "channel", prop ("string", "Just this channel (id or name)") },
                              { "strip", prop ("string", "Just this mixer strip (id or name), or \"master\"") },
                              { "clip", prop ("string", "Just this clip (id), with its notes if it's a MIDI clip") },
                              { "view", prop ("string", "\"full\" (default), \"summary\", \"mixer\" or \"transport\"") } }),
                    false, getState });

    registerPluginCommands (registry);
    registerEffectCommands (registry);
    registerBusCommands (registry);
    registerAutomationCommands (registry);
    registerOutputCommands (registry);
    registerSamplerCommands (registry);
    registerLibraryCommands (registry);
    registerAudioCommands (registry);
    registerExportCommands (registry);
}

} // namespace ng
