#pragma once

#include "core/CommandDispatcher.h"

namespace ng
{

/**
    Shows plugin editors in their own windows. Tracktion asks for a window when a plugin's
    showWindowExplicitly() is called (e.g. by show_plugin_window) and closes it when the
    plugin goes away, so a window never outlives its plugin.

    Built-in effects have no editor of their own, so they get sliders that change them
    through the dispatcher (set with setDispatcher; until then they get no window).

    Pass one to the Engine; without it (as in the tests) plugins have no windows.
*/
class PluginWindowBehaviour  : public te::UIBehaviour
{
public:
    void setDispatcher (CommandDispatcher* d)    { dispatcher = d; }

    std::unique_ptr<juce::Component> createPluginWindow (te::PluginWindowState&) override;
    void recreatePluginWindowContentAsync (te::Plugin&) override;

private:
    CommandDispatcher* dispatcher = nullptr;
};

} // namespace ng
