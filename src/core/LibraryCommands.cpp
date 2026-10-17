#include "CommandRegistry.h"

namespace ng
{
namespace
{
    using params::prop;
    using params::schema;

    // Where the library's settings live in the engine's settings file, shared by every project.
    const char* const sampleFoldersKey = "ngSampleFolders";
    const char* const presetFolderKey = "ngPresetFolder";

    const juce::Identifier presetTag ("NGPRESET"), presetPlugin ("plugin"), presetPluginName ("pluginName"),
                           presetKind ("kind"), presetName ("name");

    constexpr const char* presetExtension = ".ngpreset";
    constexpr int defaultSampleLimit = 100, maxSampleLimit = 1000;

    juce::var object (std::initializer_list<std::pair<const char*, juce::var>> properties)
    {
        auto obj = new juce::DynamicObject();

        for (auto& [name, value] : properties)
            obj->setProperty (name, value);

        return juce::var (obj);
    }

    bool isNumber (const juce::var& v)
    {
        return v.isDouble() || v.isInt() || v.isInt64();
    }

    juce::PropertiesFile& settings (Session& session)
    {
        return session.getEngine().getPropertyStorage().getPropertiesFile();
    }

    juce::StringArray getSampleFolders (Session& session)
    {
        return juce::StringArray::fromLines (settings (session).getValue (sampleFoldersKey));
    }

    juce::File getPresetFolder (Session& session)
    {
        auto saved = settings (session).getValue (presetFolderKey);

        if (saved.isNotEmpty())
            return juce::File (saved);

        return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Ng DAW").getChildFile ("Presets");
    }

    juce::var describeLibrary (Session& session)
    {
        juce::Array<juce::var> folders;

        for (auto& f : getSampleFolders (session))
            if (f.isNotEmpty())
                folders.add (f);

        return object ({ { "sample_folders", folders }, { "preset_folder", getPresetFolder (session).getFullPathName() } });
    }

    //==============================================================================
    CommandResult getLibrary (Session& session, const juce::var&)
    {
        // Whoever asks is likely to search next (the browser does); list the folders meanwhile.
        session.getSampleIndex().warmUp (getSampleFolders (session), session.getAudioFileWildcard());
        return CommandResult::success (describeLibrary (session));
    }

    CommandResult setLibrary (Session& session, const juce::var& p)
    {
        if (p.hasProperty ("sample_folders"))
        {
            if (! p["sample_folders"].isArray())
                return CommandResult::failure ("set_library: sample_folders is a list of folder paths");

            juce::StringArray folders;

            for (auto& v : *p["sample_folders"].getArray())
            {
                auto path = v.toString();

                if (! juce::File::isAbsolutePath (path) || ! juce::File (path).isDirectory())
                    return CommandResult::failure ("set_library: '" + path + "' isn't a folder (give the full path)");

                folders.addIfNotAlreadyThere (juce::File (path).getFullPathName());
            }

            settings (session).setValue (sampleFoldersKey, folders.joinIntoString ("\n"));
            session.getSampleIndex().clear();
            session.getSampleIndex().warmUp (folders, session.getAudioFileWildcard());
        }

        if (p.hasProperty ("preset_folder"))
        {
            auto path = p["preset_folder"].toString();

            if (! juce::File::isAbsolutePath (path))
                return CommandResult::failure ("set_library: preset_folder is the full path to a folder");

            if (! juce::File (path).createDirectory())
                return CommandResult::failure ("set_library: couldn't create '" + path + "'");

            settings (session).setValue (presetFolderKey, juce::File (path).getFullPathName());
        }

        settings (session).saveIfNeeded();
        return CommandResult::success (describeLibrary (session));
    }

    CommandResult findSamples (Session& session, const juce::var& p)
    {
        juce::StringArray folders;

        if (p.hasProperty ("folder"))
        {
            auto path = params::getString (p, "folder");

            if (! juce::File::isAbsolutePath (path) || ! juce::File (path).isDirectory())
                return CommandResult::failure ("find_samples: '" + path + "' isn't a folder (give the full path)");

            folders.add (path);
        }
        else
        {
            folders = getSampleFolders (session);
            folders.removeEmptyStrings();

            if (folders.isEmpty())
                return CommandResult::failure ("find_samples: there are no sample folders yet; add some with set_library");
        }

        auto limit = defaultSampleLimit;

        if (p.hasProperty ("limit"))
        {
            if (! isNumber (p["limit"]) || static_cast<int> (p["limit"]) < 1)
                return CommandResult::failure ("find_samples: limit is a number from 1");

            limit = juce::jmin (maxSampleLimit, static_cast<int> (p["limit"]));
        }

        // Every word of the query has to appear in the file's path below the folder, so
        // "kick 808" finds "808 Kit/Kick Hard.wav".
        auto words = juce::StringArray::fromTokens (params::getString (p, "query").toLowerCase(), true);
        words.removeEmptyStrings();

        auto wildcard = session.getAudioFileWildcard();
        juce::Array<juce::var> found;
        auto truncated = false;

        for (auto& folderPath : folders)
        {
            juce::File folder (folderPath);
            auto files = session.getSampleIndex().getFiles (folder, wildcard);

            for (auto& [file, relative] : *files)
            {
                if (! std::all_of (words.begin(), words.end(), [&] (const juce::String& w) { return relative.contains (w); }))
                    continue;

                if (found.size() >= limit)
                {
                    truncated = true;
                    break;
                }

                found.add (object ({ { "name", file.getFileNameWithoutExtension() }, { "path", file.getFullPathName() },
                                     { "folder", folder.getFullPathName() } }));
            }
        }

        return CommandResult::success (object ({ { "samples", found }, { "truncated", truncated } }));
    }

    //==============================================================================
    /** A plugin's name made safe to use as a folder name. */
    juce::String folderNameFor (const juce::String& name)
    {
        auto safe = juce::File::createLegalFileName (name).trim();
        return safe.isNotEmpty() ? safe : juce::String ("Plugin");
    }

    struct Preset
    {
        juce::File file;
        juce::String name, plugin, pluginName, kind;
        juce::ValueTree state;
    };

    std::optional<Preset> readPreset (const juce::File& file, bool withState)
    {
        auto xml = juce::parseXML (file);

        if (xml == nullptr || ! xml->hasTagName (presetTag.toString()) || xml->getNumChildElements() != 1)
            return std::nullopt;

        Preset preset { file, xml->getStringAttribute (presetName.toString(), file.getFileNameWithoutExtension()),
                        xml->getStringAttribute (presetPlugin.toString()), xml->getStringAttribute (presetPluginName.toString()),
                        xml->getStringAttribute (presetKind.toString()), {} };

        if (withState)
            preset.state = juce::ValueTree::fromXml (*xml->getFirstChildElement());

        return preset;
    }

    juce::var describePreset (const Preset& preset)
    {
        return object ({ { "name", preset.name }, { "plugin", preset.plugin }, { "plugin_name", preset.pluginName },
                         { "kind", preset.kind }, { "path", preset.file.getFullPathName() } });
    }

    juce::Array<Preset> getPresets (Session& session)
    {
        juce::Array<Preset> presets;

        for (const auto& entry : juce::RangedDirectoryIterator (getPresetFolder (session), true,
                                                                juce::String ("*") + presetExtension, juce::File::findFiles))
            if (auto preset = readPreset (entry.getFile(), false))
                presets.add (*preset);

        std::sort (presets.begin(), presets.end(), [] (const Preset& a, const Preset& b)
        {
            auto byPlugin = a.pluginName.compareNatural (b.pluginName);
            return byPlugin != 0 ? byPlugin < 0 : a.name.compareNatural (b.name) < 0;
        });

        return presets;
    }

    /** What save_preset saves and load_preset replaces: a channel's instrument, or an effect. */
    struct PresetTarget
    {
        te::AudioTrack* channel = nullptr;
        EffectRef effect;
        juce::String error;
    };

    PresetTarget findPresetTarget (Session& session, const juce::String& command, const juce::var& p, bool effectRequired)
    {
        PresetTarget t;

        if (p.hasProperty ("channel") == p.hasProperty ("strip"))
        {
            t.error = command + ": give a channel (for its instrument) or a strip" + (effectRequired ? " and effect" : "");
            return t;
        }

        if (p.hasProperty ("channel"))
        {
            auto ref = params::getString (p, "channel");
            t.channel = session.findTrack (ref, Session::Role::channel);

            if (t.channel == nullptr)
                t.error = command + ": no channel called '" + ref + "'";
            else if (Session::isAudioChannel (*t.channel))
                t.error = command + ": '" + t.channel->getName() + "' is an audio channel, which has no instrument";

            return t;
        }

        if (effectRequired || p.hasProperty ("effect"))
        {
            t.effect = findEffect (session, command, p);
            t.error = t.effect.error;
        }
        else
        {
            t.effect.list = findList (session, command, p, t.error);
        }

        return t;
    }

    CommandResult savePreset (Session& session, const juce::var& p)
    {
        auto name = params::getString (p, "name").trim();

        if (name.isEmpty())
            return CommandResult::failure ("save_preset: give the preset a name");

        auto t = findPresetTarget (session, "save_preset", p, true);

        if (t.error.isNotEmpty())
            return CommandResult::failure (t.error);

        te::Plugin* plugin = t.channel != nullptr ? Session::getInstrument (*t.channel) : t.effect.effect;

        if (plugin == nullptr)
            return CommandResult::failure ("save_preset: '" + t.channel->getName() + "' has no instrument");

        if (plugin->isMissing())
            return CommandResult::failure ("save_preset: '" + plugin->getName() + "' isn't loaded, so its settings can't be saved");

        // A plugin keeps its settings to itself until asked to write them into its state.
        plugin->edit.flushPluginStateIfNeeded (*plugin);
        auto state = plugin->state.createCopy();
        state.removeProperty (te::IDs::id, nullptr);

        juce::XmlElement xml (presetTag.toString());
        xml.setAttribute (presetName.toString(), name);
        xml.setAttribute (presetPlugin.toString(), Session::getPluginId (*plugin));
        xml.setAttribute (presetPluginName.toString(), plugin->getName());
        xml.setAttribute (presetKind.toString(), t.channel != nullptr ? "instrument" : "effect");

        if (auto child = state.createXml())
            xml.addChildElement (child.release());

        auto folder = getPresetFolder (session).getChildFile (folderNameFor (plugin->getName()));
        auto file = folder.getChildFile (juce::File::createLegalFileName (name) + presetExtension);

        if (! folder.createDirectory() || ! xml.writeTo (file))
            return CommandResult::failure ("save_preset: couldn't write " + file.getFullPathName());

        return CommandResult::success (describePreset (*readPreset (file, false)));
    }

    CommandResult listPresets (Session& session, const juce::var& p)
    {
        auto plugin = params::getString (p, "plugin");
        juce::Array<juce::var> list;

        for (auto& preset : getPresets (session))
            if (plugin.isEmpty() || preset.plugin.equalsIgnoreCase (plugin) || preset.pluginName.equalsIgnoreCase (plugin))
                list.add (describePreset (preset));

        return CommandResult::success (list);
    }

    /** A preset by its file's path, or by its name in the preset folder when only one has it. */
    std::optional<Preset> findPreset (Session& session, const juce::String& command, const juce::String& ref, juce::String& error)
    {
        if (juce::File::isAbsolutePath (ref))
        {
            if (auto preset = readPreset (juce::File (ref), true))
                return preset;

            error = command + ": '" + ref + "' isn't a preset file";
            return std::nullopt;
        }

        juce::Array<Preset> matches;

        for (auto& preset : getPresets (session))
            if (preset.name.equalsIgnoreCase (ref))
                matches.add (preset);

        if (matches.size() == 1)
            return readPreset (matches.getFirst().file, true);

        error = matches.isEmpty() ? command + ": no preset called '" + ref + "' (list_presets lists them)"
                                  : command + ": there are " + juce::String (matches.size()) + " presets called '" + ref
                                      + "'; use its path from list_presets";
        return std::nullopt;
    }

    /** A new plugin from a preset's saved state, with an id of its own in this project. */
    te::Plugin::Ptr createFromPreset (Session& session, const Preset& preset, juce::String& error)
    {
        auto& edit = session.getEdit();
        auto state = preset.state.createCopy();
        edit.createNewItemID().writeID (state, nullptr);

        auto plugin = edit.getPluginCache().createNewPlugin (state);

        if (plugin == nullptr)
            error = "couldn't create '" + preset.pluginName + "'";
        else if (plugin->isMissing())
            error = "'" + preset.pluginName + "' isn't installed on this computer (or didn't load); scan for it in Plugins";

        return error.isEmpty() ? plugin : nullptr;
    }

    CommandResult loadPreset (Session& session, const juce::var& p)
    {
        juce::String error;
        auto preset = findPreset (session, "load_preset", params::getString (p, "preset"), error);

        if (! preset)
            return CommandResult::failure (error);

        auto t = findPresetTarget (session, "load_preset", p, false);

        if (t.error.isNotEmpty())
            return CommandResult::failure (t.error);

        auto isInstrument = preset->kind == "instrument";

        if (isInstrument != (t.channel != nullptr))
            return CommandResult::failure ("load_preset: '" + preset->name + "' is " + (isInstrument ? "an instrument" : "an effect")
                                             + " preset, so it goes on " + (isInstrument ? "a channel" : "a mixer strip"));

        if (t.channel != nullptr && Session::getMultiOutputRack (*t.channel) != nullptr)
            return CommandResult::failure ("load_preset: '" + t.channel->getName() + "' has multiple outputs on; "
                                           "turn them off with set_multi_output first");

        auto plugin = createFromPreset (session, *preset, error);

        if (plugin == nullptr)
            return CommandResult::failure ("load_preset: " + error);

        if (t.channel != nullptr)
        {
            while (auto old = Session::getInstrument (*t.channel))
                old->deleteFromParent();

            t.channel->pluginList.insertPlugin (plugin, 0, nullptr);

            return CommandResult::success (object ({ { "channel", t.channel->itemID.toString() },
                                                     { "instrument", Session::getInstrumentId (plugin.get()) },
                                                     { "preset", preset->name } }));
        }

        // An effect preset replaces the effect named, or goes in at index (default: last).
        auto& list = *t.effect.list;
        auto effects = Session::getEffects (list);
        auto index = effects.size();

        if (t.effect.effect != nullptr)
        {
            index = effects.indexOf (t.effect.effect);
            t.effect.effect->deleteFromParent();
        }
        else if (p.hasProperty ("index"))
        {
            if (! isNumber (p["index"]) || static_cast<int> (p["index"]) < 0)
                return CommandResult::failure ("load_preset: index is a position from 0");

            index = juce::jmin (effects.size(), static_cast<int> (p["index"]));
        }

        Session::storeEffectSettings (*plugin);
        list.insertPlugin (plugin, Session::getEffectInsertIndex (list, index), nullptr);

        if (! list.contains (plugin.get()))
            return CommandResult::failure ("load_preset: the strip can't take more effects");

        return CommandResult::success (object ({ { "strip", p["strip"] },
                                                 { "index", Session::getEffects (list).indexOf (plugin.get()) },
                                                 { "effect", Session::describeEffect (*plugin) },
                                                 { "preset", preset->name } }));
    }
}

void registerLibraryCommands (CommandRegistry& registry)
{
    registry.add ({ "get_library",
                    "The browser's folders: the sample folders it shows (and find_samples searches) and the folder "
                    "presets are saved in. These are the app's settings, the same in every project.",
                    schema ({}), false, getLibrary });

    registry.add ({ "set_library",
                    "Sets the browser's sample folders (the whole list) and/or the folder presets are saved in. "
                    "Not an undo step.",
                    schema ({ { "sample_folders", prop ("array", "Full paths of the folders holding samples") },
                              { "preset_folder", prop ("string", "Full path of the folder to save presets in") } }),
                    false, setLibrary });

    registry.add ({ "find_samples",
                    "Finds audio files in the sample folders (or one folder) whose path has every word of the "
                    "query, e.g. \"kick 808\". Load them with add_sample or load_drum_kit.",
                    schema ({ { "query", prop ("string", "Words to look for in the file's name and folders (default: all files)") },
                              { "folder", prop ("string", "Full path of a folder to search instead of the sample folders") },
                              { "limit", prop ("integer", "Most files to list (default 100, up to 1000)") } }),
                    false, findSamples });

    registry.add ({ "save_preset",
                    "Saves the settings of a channel's instrument, or of an effect, as a named preset in the preset "
                    "folder, to load on any channel or strip in any project. A sampler's preset keeps its samples.",
                    schema ({ { "name", prop ("string", "Preset name") },
                              { "channel", prop ("string", "Channel id or name, to save its instrument") },
                              { "strip", prop ("string", "Mixer strip id or name (or \"master\"), with effect") },
                              { "effect", prop ("string", "The effect's id, position from 0, or name") } },
                            { "name" }),
                    false, savePreset });

    registry.add ({ "list_presets",
                    "Lists the presets in the preset folder, by plugin, with the path load_preset takes.",
                    schema ({ { "plugin", prop ("string", "Only this plugin's presets (its id or name)") } }),
                    false, listPresets });

    registry.add ({ "load_preset",
                    "Loads a preset: an instrument preset replaces a channel's instrument, and an effect preset replaces "
                    "an effect on a strip or adds one (at index, default last).",
                    schema ({ { "preset", prop ("string", "The preset's path, or its name if only one preset has it") },
                              { "channel", prop ("string", "Channel id or name, for an instrument preset") },
                              { "strip", prop ("string", "Mixer strip id or name (or \"master\"), for an effect preset") },
                              { "effect", prop ("string", "The effect to replace: its id, position from 0, or name") },
                              { "index", prop ("integer", "Where to add the effect, from 0 (default: last)") } },
                            { "preset" }),
                    true, loadPreset });
}

} // namespace ng
