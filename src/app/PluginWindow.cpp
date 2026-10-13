#include "PluginWindow.h"
#include "Widgets.h"

namespace ng
{
namespace
{
   #if JUCE_LINUX
    constexpr bool addToDesktopStraightAway = false;
   #else
    constexpr bool addToDesktopStraightAway = true;
   #endif

    //==============================================================================
    /** Sliders for a built-in effect's settings, which have no editor of their own. Moving
        one runs set_effect, so it's undoable like any other change; a drag is one step. */
    class GenericEffectEditor  : public te::Plugin::EditorComponent,
                                 private juce::Timer
    {
    public:
        GenericEffectEditor (te::Plugin& p, CommandDispatcher& d)
            : plugin (p), dispatcher (d),
              strip (p.getOwnerTrack() != nullptr ? p.getOwnerTrack()->itemID.toString() : juce::String ("master"))
        {
            enabled.setButtonText ("On");
            enabled.setTooltip ("Turn the effect off to bypass it");
            enabled.onClick = [this] { send ("enabled", enabled.getToggleState()); };
            addAndMakeVisible (enabled);

            if (auto delay = dynamic_cast<te::DelayPlugin*> (&plugin))
            {
                auto& row = addRow ("length", "Length", 1.0, 2000.0, 1.0);
                row.slider->setTextValueSuffix (" ms");
                row.read = [delay] { return (double) delay->lengthMs.get(); };
                row.text = nullptr;
            }

            for (auto param : plugin.getAutomatableParameters())
            {
                auto range = param->getValueRange();
                auto& row = addRow (param->paramID, param->getParameterName(), range.getStart(), range.getEnd(), 0.0);
                te::AutomatableParameter::Ptr keep (param);
                row.read = [keep] { return (double) keep->getCurrentValue(); };
                row.text = [keep] (double v) { return keep->valueToString ((float) v); };
            }

            refresh();
            setSize (420, 40 + rows.size() * rowHeight + 8);
            startTimerHz (10);
        }

        bool allowWindowResizing() override                         { return false; }
        juce::ComponentBoundsConstrainer* getBoundsConstrainer() override   { return nullptr; }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colour (0xff1e2227));
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (10, 8);
            enabled.setBounds (r.removeFromTop (24).removeFromLeft (80));
            r.removeFromTop (8);

            for (auto* row : rows)
            {
                auto line = r.removeFromTop (rowHeight);
                row->label.setBounds (line.removeFromLeft (120));
                row->slider->setBounds (line.reduced (0, 2));
            }
        }

    private:
        static constexpr int rowHeight = 30;

        /** A slider whose value box shows the effect's own text for the value (e.g. "-6.0 dB"). */
        struct ValueSlider  : public juce::Slider
        {
            juce::String getTextFromValue (double v) override
            {
                return text != nullptr ? text (v) : juce::Slider::getTextFromValue (v);
            }

            std::function<juce::String (double)> text;
        };

        struct Row
        {
            juce::String id;
            juce::Label label;
            std::unique_ptr<ValueSlider> slider;
            std::function<double()> read;
            std::function<juce::String (double)> text;
        };

        Row& addRow (const juce::String& id, const juce::String& name, double min, double max, double interval)
        {
            auto* row = rows.add (new Row());
            row->id = id;
            row->label.setText (name, juce::dontSendNotification);
            row->slider = std::make_unique<ValueSlider>();

            auto& slider = *row->slider;
            slider.setSliderStyle (juce::Slider::LinearHorizontal);
            slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 90, 20);
            slider.setRange (min, max, interval);
            slider.onDragStart = [this, name] { dispatcher.beginGesture (plugin.getName() + " " + name); };
            slider.onDragEnd = [this] { dispatcher.endGesture(); };
            slider.onValueChange = [this, row] { send ("params", makeObject ({ { row->id.toRawUTF8(), row->slider->getValue() } })); };

            addAndMakeVisible (row->label);
            addAndMakeVisible (slider);
            return *row;
        }

        void send (const char* key, const juce::var& value)
        {
            dispatcher.execute ("set_effect", makeObject ({ { "strip", strip }, { "effect", plugin.itemID.toString() },
                                                            { key, value } }));
        }

        /** Shows the current values (after undo, or automation) unless a slider is in use. */
        void refresh()
        {
            enabled.setToggleState (plugin.isEnabled(), juce::dontSendNotification);

            for (auto* row : rows)
            {
                row->slider->text = row->text;

                if (! row->slider->isMouseButtonDown())
                    row->slider->setValue (row->read(), juce::dontSendNotification);

                row->slider->updateText();
            }
        }

        void timerCallback() override   { refresh(); }

        te::Plugin& plugin;
        CommandDispatcher& dispatcher;
        juce::String strip;
        juce::ToggleButton enabled;
        juce::OwnedArray<Row> rows;
    };

    //==============================================================================
    /** A window holding one plugin's editor, sized to it. Closing it just hides the editor. */
    class PluginWindow  : public juce::DocumentWindow
    {
    public:
        PluginWindow (te::Plugin& p, CommandDispatcher* d)
            : DocumentWindow (p.getName(), juce::Colours::black, DocumentWindow::closeButton, addToDesktopStraightAway),
              plugin (p), dispatcher (d)
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
            auto newEditor = plugin.createEditor();

            if (newEditor == nullptr && dispatcher != nullptr && dynamic_cast<te::ExternalPlugin*> (&plugin) == nullptr)
                newEditor = std::make_unique<GenericEffectEditor> (plugin, *dispatcher);

            setEditor (std::move (newEditor));
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
        CommandDispatcher* dispatcher;
        std::unique_ptr<te::Plugin::EditorComponent> editor;
        bool remembersPosition = false;
    };
}

std::unique_ptr<juce::Component> PluginWindowBehaviour::createPluginWindow (te::PluginWindowState& state)
{
    auto* pluginState = dynamic_cast<te::Plugin::WindowState*> (&state);

    if (pluginState == nullptr || pluginState->plugin.isMissing())
        return {};

    auto window = std::make_unique<PluginWindow> (pluginState->plugin, dispatcher);

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
