#include "PluginWindow.h"

namespace ng
{
namespace
{
   #if JUCE_LINUX
    constexpr bool addToDesktopStraightAway = false;
   #else
    constexpr bool addToDesktopStraightAway = true;
   #endif

    /** A window holding one plugin's editor, sized to it. Closing it just hides the editor. */
    class PluginWindow  : public juce::DocumentWindow
    {
    public:
        explicit PluginWindow (te::Plugin& p)
            : DocumentWindow (p.getName(), juce::Colours::black, DocumentWindow::closeButton, addToDesktopStraightAway),
              plugin (p)
        {
            setUsingNativeTitleBar (true);
            getConstrainer()->setMinimumOnscreenAmounts (0x10000, 50, 30, 50);
            recreateEditor();
            setTopLeftPosition (plugin.windowState->choosePositionForPluginWindow());

           #if JUCE_LINUX
            // Linux window managers place a window themselves unless it's added after it has a size.
            addToDesktop();
           #endif

            remembersPosition = true;
        }

        ~PluginWindow() override
        {
            remembersPosition = false;
            plugin.edit.flushPluginStateIfNeeded (plugin);
            setEditor (nullptr);
        }

        bool hasEditor() const      { return editor != nullptr; }

        void recreateEditor()
        {
            setEditor (nullptr);
            setEditor (plugin.createEditor());
        }

        void recreateEditorAsync()
        {
            setEditor (nullptr);

            juce::Timer::callAfterDelay (50, [safeThis = juce::Component::SafePointer<PluginWindow> (this)]
            {
                if (safeThis != nullptr)
                    safeThis->recreateEditor();
            });
        }

    private:
        void setEditor (std::unique_ptr<te::Plugin::EditorComponent> newEditor)
        {
            setConstrainer (nullptr);
            editor = std::move (newEditor);

            if (editor != nullptr)
                setContentNonOwned (editor.get(), true);
            else
                clearContentComponent();

            auto canResize = editor != nullptr && editor->allowWindowResizing();
            setResizable (canResize, false);

            if (canResize)
                setConstrainer (editor->getBoundsConstrainer());
        }

        void moved() override
        {
            if (remembersPosition)
            {
                plugin.windowState->lastWindowBounds = getBounds();
                plugin.edit.pluginChanged (plugin);
            }
        }

        void closeButtonPressed() override      { plugin.windowState->closeWindowExplicitly(); }
        float getDesktopScaleFactor() const override    { return 1.0f; }

        te::Plugin& plugin;
        std::unique_ptr<te::Plugin::EditorComponent> editor;
        bool remembersPosition = false;
    };
}

std::unique_ptr<juce::Component> PluginWindowBehaviour::createPluginWindow (te::PluginWindowState& state)
{
    auto* pluginState = dynamic_cast<te::Plugin::WindowState*> (&state);

    if (pluginState == nullptr || pluginState->plugin.isMissing())
        return {};

    auto window = std::make_unique<PluginWindow> (pluginState->plugin);

    if (! window->hasEditor())
        return {};

    window->setVisible (true);
    window->toFront (true);
    return window;
}

void PluginWindowBehaviour::recreatePluginWindowContentAsync (te::Plugin& plugin)
{
    if (auto* window = dynamic_cast<PluginWindow*> (plugin.windowState->pluginWindow.get()))
        return window->recreateEditorAsync();

    UIBehaviour::recreatePluginWindowContentAsync (plugin);
}

} // namespace ng
