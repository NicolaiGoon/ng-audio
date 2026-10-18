#include "CommandRegistry.h"

namespace ng
{
namespace
{
    using params::isNumber;
    using params::roundBeats;
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

    registerClipCommands (registry);
    registerTransportCommands (registry);
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
