#include "MainComponent.h"
#include "PluginWindow.h"

namespace ng
{

class NgDawApplication  : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override       { return "Ng DAW"; }
    const juce::String getApplicationVersion() override    { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override             { return true; }

    void initialise (const juce::String&) override
    {
        engine = std::make_unique<te::Engine> (getApplicationName(), std::make_unique<PluginWindowBehaviour>(), nullptr);
        mainWindow = std::make_unique<MainWindow> (getApplicationName(), *engine);
    }

    void shutdown() override
    {
        mainWindow = nullptr;
        engine = nullptr;
    }

    void systemRequestedQuit() override
    {
        // Ask about unsaved changes first; Cancel keeps the app open.
        if (mainWindow != nullptr)
            if (auto* content = dynamic_cast<MainComponent*> (mainWindow->getContentComponent()))
                return content->confirmDiscardChanges ([] { quit(); });

        quit();
    }

private:
    class MainWindow  : public juce::DocumentWindow
    {
    public:
        MainWindow (const juce::String& name, te::Engine& engine)
            : DocumentWindow (name,
                              juce::Desktop::getInstance().getDefaultLookAndFeel()
                                  .findColour (juce::ResizableWindow::backgroundColourId),
                              DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent (engine), true);
            setResizable (true, true);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);

            // So the shortcuts work before anything has been clicked.
            getContentComponent()->grabKeyboardFocus();
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }
    };

    std::unique_ptr<te::Engine> engine;
    std::unique_ptr<MainWindow> mainWindow;
};

} // namespace ng

START_JUCE_APPLICATION (ng::NgDawApplication)
