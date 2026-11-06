#include "PreferencesPanel.h"

namespace ng
{

juce::String getBufferAdvice (te::Engine& engine, int dropoutsSinceLastCheck)
{
    auto& dm = engine.getDeviceManager();
    auto cpu = dm.getCpuUsage();
    auto* device = dm.deviceManager.getCurrentAudioDevice();

    if (device == nullptr)
        return "No audio device is open. Pick one in Settings > Audio and MIDI.";

    if (dropoutsSinceLastCheck > 0 || cpu > 0.8f)
    {
        auto buffer = device->getCurrentBufferSizeSamples();
        auto bigger = buffer;

        for (auto size : device->getAvailableBufferSizes())
            if (size > buffer)
            {
                bigger = size;
                break;
            }

        auto why = dropoutsSinceLastCheck > 0 ? juce::String ("The audio dropped out") : juce::String ("The CPU is nearly full");

        if (bigger > buffer)
            return why + ". If you hear clicks, raise the buffer size from " + juce::String (buffer) + " to " + juce::String (bigger)
                     + " samples in Settings > Audio and MIDI (or freeze or remove heavy plugins).";

        return why + ". Remove or bypass heavy plugins, or close other apps.";
    }

    return {};
}

//==============================================================================
class PreferencesPanel::General  : public juce::Component
{
public:
    General (Session& s, std::function<void()> changed) : session (s), onChanged (std::move (changed))
    {
        auto& engine = session.getEngine();

        theme.addItemList (settings::getThemes(), 1);
        theme.setSelectedItemIndex (settings::getThemes().indexOf (settings::getTheme (engine)), juce::dontSendNotification);
        theme.onChange = [this] { settings::setTheme (session.getEngine(), theme.getText()); };

        for (auto minutes : settings::getAutosaveChoices())
            autosave.addItem (minutes == 0 ? juce::String ("Off") : minutes == 1 ? juce::String ("Every minute")
                                                                                 : "Every " + juce::String (minutes) + " minutes",
                              minutes + 1);

        autosave.setSelectedId (settings::getAutosaveMinutes (engine) + 1, juce::dontSendNotification);
        autosave.onChange = [this]
        {
            settings::setAutosaveMinutes (session.getEngine(), autosave.getSelectedId() - 1);
            notify();
        };

        folder.setColour (juce::Label::outlineColourId, juce::Colours::grey.withAlpha (0.4f));
        folder.setMinimumHorizontalScale (0.6f);
        chooseFolder.onClick = [this] { pickFolder(); };
        resetFolder.setTooltip ("Back to Music/Ng DAW");
        resetFolder.onClick = [this]
        {
            settings::setProjectFolder (session.getEngine(), {});
            updateFolder();
            notify();
        };

        note.setText ("New projects are saved in the project folder, and recordings and exports go there until a project is saved. "
                      "Autosave keeps your unsaved changes, so they can be recovered if Ng DAW closes unexpectedly.",
                      juce::dontSendNotification);
        note.setColour (juce::Label::textColourId, juce::Colours::grey);
        note.setFont (juce::FontOptions (12.0f));
        note.setJustificationType (juce::Justification::topLeft);

        for (auto* c : std::initializer_list<juce::Component*> { &themeLabel, &theme, &folderLabel, &folder, &chooseFolder,
                                                                 &resetFolder, &autosaveLabel, &autosave, &note })
            addAndMakeVisible (c);

        updateFolder();
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (16);

        auto row = [&r] (juce::Label& label)
        {
            auto line = r.removeFromTop (26);
            label.setBounds (line.removeFromLeft (130));
            r.removeFromTop (10);
            return line;
        };

        theme.setBounds (row (themeLabel).withWidth (180));
        auto folderRow = row (folderLabel);
        resetFolder.setBounds (folderRow.removeFromRight (60));
        folderRow.removeFromRight (6);
        chooseFolder.setBounds (folderRow.removeFromRight (80));
        folderRow.removeFromRight (6);
        folder.setBounds (folderRow);
        autosave.setBounds (row (autosaveLabel).withWidth (180));
        note.setBounds (r.removeFromTop (60));
    }

private:
    void notify()
    {
        if (onChanged != nullptr)
            onChanged();
    }

    void updateFolder()
    {
        auto chosen = settings::getProjectFolder (session.getEngine());
        session.setDefaultFolder (chosen);
        folder.setText (session.getDefaultFolder().getFullPathName(), juce::dontSendNotification);
        resetFolder.setEnabled (chosen != juce::File());
    }

    void pickFolder()
    {
        chooser = std::make_unique<juce::FileChooser> ("Choose the project folder", session.getDefaultFolder());
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [safeThis = juce::Component::SafePointer (this)] (const juce::FileChooser& fc)
                              {
                                  if (safeThis == nullptr || fc.getResult() == juce::File())
                                      return;

                                  settings::setProjectFolder (safeThis->session.getEngine(), fc.getResult());
                                  safeThis->updateFolder();
                                  safeThis->notify();
                              });
    }

    Session& session;
    std::function<void()> onChanged;
    juce::Label themeLabel { {}, "Theme" }, folderLabel { {}, "Project folder" }, autosaveLabel { {}, "Autosave" }, folder, note;
    juce::ComboBox theme, autosave;
    juce::TextButton chooseFolder { "Choose..." }, resetFolder { "Reset" };
    std::unique_ptr<juce::FileChooser> chooser;
};

//==============================================================================
juce::File findMcpServer()
{
    auto app = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
   #if JUCE_WINDOWS
    juce::String name ("NgDawMcp.exe");
   #else
    juce::String name ("NgDawMcp");
   #endif

    // Installed: beside the app. Built from source: build/NgDawMcp_artefacts/<config>/ beside build/NgDaw_artefacts/<config>/.
    for (auto folder = app.getParentDirectory(); folder.getParentDirectory() != folder; folder = folder.getParentDirectory())
    {
        if (folder.getChildFile (name).existsAsFile())
            return folder.getChildFile (name);

        auto built = folder.getChildFile ("NgDawMcp_artefacts").getChildFile (app.getParentDirectory().getFileName()).getChildFile (name);

        if (built.existsAsFile())
            return built;

        if (folder.getFileName() == "build" || folder.getChildFile ("CMakeLists.txt").existsAsFile())
            break;
    }

    return app.getSiblingFile (name);
}

//==============================================================================
class PreferencesPanel::Ai  : public juce::Component
{
public:
    Ai()
    {
        header.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        note.setFont (juce::FontOptions (12.0f));
        note.setColour (juce::Label::textColourId, juce::Colours::grey);
        note.setJustificationType (juce::Justification::topLeft);

        auto server = findMcpServer().getFullPathName();
        command = "claude mcp add ng-daw -- \"" + server + "\"";
        config = "{\"mcpServers\": {\"ng-daw\": {\"command\": " + juce::JSON::toString (server) + "}}}";

        connectText.setMultiLine (true);
        connectText.setReadOnly (true);
        connectText.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain));
        connectText.setText ("Claude Code:\n" + command + "\n\nClaude Desktop (claude_desktop_config.json):\n" + config);
        copyCommand.onClick = [this] { juce::SystemClipboard::copyTextToClipboard (command); };
        copyConfig.onClick = [this] { juce::SystemClipboard::copyTextToClipboard (config); };

        for (auto* c : std::initializer_list<juce::Component*> { &header, &note, &connectText, &copyCommand, &copyConfig })
            addAndMakeVisible (c);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (16);

        header.setBounds (r.removeFromTop (22));
        note.setBounds (r.removeFromTop (40));
        r.removeFromTop (6);
        auto buttons = r.removeFromBottom (26);
        copyCommand.setBounds (buttons.removeFromLeft (180));
        buttons.removeFromLeft (8);
        copyConfig.setBounds (buttons.removeFromLeft (220));
        r.removeFromBottom (6);
        connectText.setBounds (r);
    }

private:
    juce::Label header { {}, "Use Ng DAW from Claude Code, Claude Desktop or any MCP client" },
                note { {}, "NgDawMcp is an MCP server: an AI agent connected to it can run every command on the project "
                           "that's open here, and each change goes in the command log and the undo history." };
    juce::TextEditor connectText;
    juce::TextButton copyCommand { "Copy Claude Code command" }, copyConfig { "Copy Claude Desktop settings" };
    juce::String command, config;
};

//==============================================================================
PreferencesPanel::PreferencesPanel (Session& session, std::function<void()> onChanged, const juce::String& tab)
{
    auto& dm = session.getEngine().getDeviceManager().deviceManager;
    auto background = getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId);

    tabs.addTab ("Audio and MIDI", background, new juce::AudioDeviceSelectorComponent (dm, 0, 2, 0, 2, true, true, true, false), true);
    tabs.addTab ("General", background, new General (session, std::move (onChanged)), true);
    tabs.addTab ("AI", background, new Ai(), true);
    addAndMakeVisible (tabs);

    if (auto index = tabs.getTabNames().indexOf (tab); index >= 0)
        tabs.setCurrentTabIndex (index);

    setSize (600, 520);
}

void PreferencesPanel::resized()
{
    tabs.setBounds (getLocalBounds());
}

} // namespace ng
