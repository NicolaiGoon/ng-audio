#include "MainComponent.h"
#include "PluginWindow.h"
#include "StartupSplash.h"

namespace ng
{

class NgDawApplication  : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override       { return "Ng DAW"; }
    const juce::String getApplicationVersion() override    { return NG_VERSION; }
    bool moreThanOneInstanceAllowed() override             { return true; }

    void initialise (const juce::String& commandLine) override
    {
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

        // The splash goes up first and each step runs after it has had a chance to draw, so
        // there's something to look at (and a sense of progress) while the engine starts.
        splash = std::make_unique<StartupSplash>();
        splash->setProgress ("Starting the audio engine", 0.15f);
        pendingCommandLine = commandLine;

        juce::Timer::callAfterDelay (120, [this]
        {
            engine = std::make_unique<te::Engine> (getApplicationName(), std::make_unique<PluginWindowBehaviour>(),
                                                   std::make_unique<EngineBehaviour>());
            settings::applyTheme (settings::getTheme (*engine));
            splash->setProgress ("Opening the project", 0.6f);

            juce::Timer::callAfterDelay (60, [this]
            {
                mainWindow = std::make_unique<MainWindow> (getApplicationName(), *engine);
                splash->setProgress ("Ready", 1.0f);
                openFromCommandLine (pendingCommandLine);

                // Let the main window draw, then fade the splash away over it.
                juce::Timer::callAfterDelay (150, [this]
                {
                    if (splash != nullptr)
                    {
                        auto* s = splash.get();
                        s->fadeOutAndDelete (std::move (splash));
                    }
                });
            });
        });
    }

    void anotherInstanceStarted (const juce::String& commandLine) override
    {
        if (mainWindow == nullptr)
            pendingCommandLine = commandLine;
        else
            openFromCommandLine (commandLine);
    }

    void shutdown() override
    {
        splash = nullptr;
        mainWindow = nullptr;
        engine = nullptr;
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
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
    /** A project double-clicked in Explorer or Finder arrives as the command line. */
    void openFromCommandLine (const juce::String& commandLine)
    {
        auto path = commandLine.trim().unquoted();

        if (path.isEmpty() || ! juce::File::isAbsolutePath (path))
            return;

        juce::File file (path);

        if (file.existsAsFile() && file.hasFileExtension (projectFileExtension) && mainWindow != nullptr)
            if (auto* content = dynamic_cast<MainComponent*> (mainWindow->getContentComponent()))
                content->openProjectFile (file);
    }

private:
    class MainWindow  : public juce::DocumentWindow
    {
    public:
        MainWindow (const juce::String& name, te::Engine& engine)
            : DocumentWindow (name,
                              juce::Desktop::getInstance().getDefaultLookAndFeel()
                                  .findColour (juce::ResizableWindow::backgroundColourId),
                              DocumentWindow::allButtons),
              propertiesFile (engine.getPropertyStorage().getPropertiesFile())
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent (engine), true);
            setResizable (true, true);
            setResizeLimits (900, 560, 20000, 20000);

            // Where it was last time (and whether it was maximised), or centred.
            auto saved = engine.getPropertyStorage().getPropertiesFile().getValue ("ngMainWindow");

            if (saved.isEmpty() || ! restoreWindowStateFromString (saved))
                centreWithSize (getWidth(), getHeight());

            setVisible (true);

            // So the shortcuts work before anything has been clicked.
            getContentComponent()->grabKeyboardFocus();
        }

        ~MainWindow() override
        {
            propertiesFile.setValue ("ngMainWindow", getWindowStateAsString());
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        juce::PropertiesFile& propertiesFile;
    };

    NgLookAndFeel lookAndFeel;
    std::unique_ptr<StartupSplash> splash;
    juce::String pendingCommandLine;
    std::unique_ptr<te::Engine> engine;
    std::unique_ptr<MainWindow> mainWindow;
};

} // namespace ng

START_JUCE_APPLICATION (ng::NgDawApplication)
