#pragma once

#include "core/CommandDispatcher.h"

namespace ng
{

/**
    Shows plugin editors in their own windows. Tracktion asks for a window when a plugin's
    showWindowExplicitly() is called (e.g. by show_plugin_window) and closes it when the
    plugin goes away, so a window never outlives its plugin.

    Pass one to the Engine; without it (as in the tests) plugins have no windows.
*/
class PluginWindowBehaviour  : public te::UIBehaviour
{
public:
    std::unique_ptr<juce::Component> createPluginWindow (te::PluginWindowState&) override;
    void recreatePluginWindowContentAsync (te::Plugin&) override;
};

} // namespace ng
