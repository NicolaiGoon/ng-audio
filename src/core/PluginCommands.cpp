#include "CommandRegistry.h"

namespace ng
{
namespace
{
    using params::object;
    using params::prop;
    using params::schema;

    te::PluginManager& pluginManager (Session& session)
    {
        return session.getEdit().engine.getPluginManager();
    }

    juce::AudioPluginFormat* findFormat (Session& session, const juce::String& name)
    {
        for (auto* format : pluginManager (session).pluginFormatManager.getFormats())
            if (format->getName().equalsIgnoreCase (name))
                return format;

        return nullptr;
    }

    juce::var describePlugin (const juce::PluginDescription& d)
    {
        return object ({ { "id", d.createIdentifierString() },
                             { "name", d.name },
                             { "vendor", d.manufacturerName },
                             { "format", d.pluginFormatName },
                             { "category", d.category },
                             { "instrument", d.isInstrument },
                             { "outputs", d.numOutputChannels },
                             { "file", d.fileOrIdentifier } });
    }

    CommandResult listPlugins (Session& session, const juce::var& p)
    {
        auto kind = params::getString (p, "kind", "all");

        if (kind != "all" && kind != "instrument" && kind != "effect")
            return CommandResult::failure ("list_plugins: kind is all, instrument or effect, not '" + kind + "'");

        auto types = pluginManager (session).knownPluginList.getTypes();
        std::sort (types.begin(), types.end(), [] (auto& a, auto& b) { return a.name.compareNatural (b.name) < 0; });

        juce::Array<juce::var> list;

        for (auto& d : types)
            if (kind == "all" || d.isInstrument == (kind == "instrument"))
                list.add (describePlugin (d));

        return CommandResult::success (list);
    }

    CommandResult scanPlugins (Session& session, const juce::var& p)
    {
        auto formatName = params::getString (p, "format", "VST3");
        auto* format = findFormat (session, formatName);

        if (format == nullptr || ! format->canScanForPlugins())
            return CommandResult::failure ("scan_plugins: can't scan for " + formatName + " plugins here");

        auto& engine = session.getEdit().engine;
        auto& settings = engine.getPropertyStorage().getPropertiesFile();

        // The folders to scan: the ones given (remembered for next time), else the ones used
        // last, which the Plugins window shares, else the format's usual places.
        juce::FileSearchPath folders;

        if (p.hasProperty ("folders"))
        {
            if (! p["folders"].isArray())
                return CommandResult::failure ("scan_plugins: folders is a list of paths");

            for (auto& folder : *p["folders"].getArray())
            {
                auto path = folder.toString();

                if (! juce::File::isAbsolutePath (path) || ! juce::File (path).isDirectory())
                    return CommandResult::failure ("scan_plugins: '" + path + "' isn't a folder (give the full path)");

                folders.add (juce::File (path));
            }

            juce::PluginListComponent::setLastSearchPath (settings, *format, folders);
        }
        else
        {
            folders = juce::PluginListComponent::getLastSearchPath (settings, *format);
        }

        auto& list = pluginManager (session).knownPluginList;
        auto before = list.getNumTypes();

        // A plugin that crashes the scan is written to this file first, and skipped next time.
        juce::PluginDirectoryScanner scanner (list, *format, folders, true,
                                              engine.getTemporaryFileManager().getTempFile ("PluginScanDeadMansPedal"));
        juce::String current;

        while (scanner.scanNextFile (true, current))
        {
        }

        juce::Array<juce::var> searched, failed;

        for (int i = 0; i < folders.getNumPaths(); ++i)
            searched.add (folders.getRawString (i));

        for (auto& file : scanner.getFailedFiles())
            failed.add (file);

        return CommandResult::success (object ({ { "format", format->getName() },
                                                     { "folders", searched },
                                                     { "added", list.getNumTypes() - before },
                                                     { "total", list.getNumTypes() },
                                                     { "failed", failed } }));
    }
}

void registerPluginCommands (CommandRegistry& registry)
{
    registry.add ({ "list_plugins",
                    "Lists the plugins found by scanning (not the built-in ones), sorted by name, with the id "
                    "used to load them. The list is the app's, so it's the same in every project.",
                    schema ({ { "kind", prop ("string", "all (default), instrument or effect") } }),
                    false, listPlugins });

    registry.add ({ "scan_plugins",
                    "Scans folders for plugins and adds any new ones to the plugin list. Without folders it scans "
                    "the folders used last time (or the format's usual places, such as "
                    "C:\\Program Files\\Common Files\\VST3 on Windows). Folders given are remembered. "
                    "Plugins that fail to load are listed under failed. Not an undo step.",
                    schema ({ { "folders", prop ("array", "Absolute paths of the folders to scan") },
                              { "format", prop ("string", "Plugin format to scan for: VST3 (default)") } }),
                    false, scanPlugins });
}

} // namespace ng
