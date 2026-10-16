#include "PreferencesPanel.h"

#include <thread>

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
    explicit Ai (Session& s) : session (s)
    {
        for (auto& p : AiChat::getProviders())
            provider.addItem (p.name, provider.getNumItems() + 1);

        provider.setText (settings::getAiProvider (engine()).name, juce::dontSendNotification);
        provider.onChange = [this] { load(); };

        key.setPasswordCharacter ((juce::juce_wchar) 0x2022);
        model.setEditableText (true);
        model.setTextWhenNothingSelected ("Type a model name, or load the list");

        for (auto* editor : { &address, &key })
            editor->onTextChange = [this] { save(); };

        model.onChange = [this] { save(); };
        loadModels.onClick = [this] { fetchModels(); };
        loadModels.setTooltip ("Ask the service which models it offers");

        note.setFont (juce::FontOptions (12.0f));
        note.setColour (juce::Label::textColourId, juce::Colours::grey);
        note.setJustificationType (juce::Justification::topLeft);

        connectHeader.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        auto server = findMcpServer().getFullPathName();
        command = "claude mcp add ng-daw -- \"" + server + "\"";
        config = "{\"mcpServers\": {\"ng-daw\": {\"command\": " + juce::JSON::toString (server) + "}}}";

        connectText.setMultiLine (true);
        connectText.setReadOnly (true);
        connectText.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain));
        connectText.setText ("Claude Code:\n" + command + "\n\nClaude Desktop (claude_desktop_config.json):\n" + config);
        copyCommand.onClick = [this] { juce::SystemClipboard::copyTextToClipboard (command); };
        copyConfig.onClick = [this] { juce::SystemClipboard::copyTextToClipboard (config); };

        for (auto* c : std::initializer_list<juce::Component*> { &providerLabel, &provider, &addressLabel, &address, &keyLabel, &key,
                                                                 &modelLabel, &model, &loadModels, &note, &connectHeader, &connectText,
                                                                 &copyCommand, &copyConfig })
            addAndMakeVisible (c);

        load();
    }

    ~Ai() override
    {
        cancellation->cancel();

        if (fetching.joinable())
            fetching.join();
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (16);

        auto row = [&r] (juce::Label& label)
        {
            auto line = r.removeFromTop (26);
            label.setBounds (line.removeFromLeft (110));
            r.removeFromTop (8);
            return line;
        };

        provider.setBounds (row (providerLabel).withWidth (260));
        address.setBounds (row (addressLabel));
        key.setBounds (row (keyLabel));
        auto modelRow = row (modelLabel);
        loadModels.setBounds (modelRow.removeFromRight (100));
        modelRow.removeFromRight (6);
        model.setBounds (modelRow);
        note.setBounds (r.removeFromTop (58));
        r.removeFromTop (6);
        connectHeader.setBounds (r.removeFromTop (22));
        auto buttons = r.removeFromBottom (26);
        copyCommand.setBounds (buttons.removeFromLeft (180));
        buttons.removeFromLeft (8);
        copyConfig.setBounds (buttons.removeFromLeft (220));
        r.removeFromBottom (6);
        connectText.setBounds (r);
    }

private:
    te::Engine& engine()    { return session.getEngine(); }

    void load()
    {
        loading = true;
        auto p = settings::getAiProvider (engine(), provider.getText());
        auto& f = settings::file (engine());

        address.setText (p.baseUrl, juce::dontSendNotification);
        address.setReadOnly (AiChat::findProvider (p.name).baseUrl.isNotEmpty());
        address.setTextToShowWhenEmpty ("e.g. http://localhost:8080/v1", juce::Colours::grey);
        key.setText (f.getValue (settings::providerKey (p.name, "Key")), juce::dontSendNotification);

        auto variable = settings::getKeyVariable (p.name);
        key.setTextToShowWhenEmpty (! p.needsKey ? "Not needed for most local servers"
                                                 : variable.isNotEmpty() ? "Paste your key (or set " + variable + ")"
                                                                         : "Paste your key", juce::Colours::grey);
        key.repaint();
        model.clear (juce::dontSendNotification);
        model.setText (p.model, juce::dontSendNotification);

        note.setText (p.needsKey ? "Messages and project details go to " + p.name + ", and its charges apply to your account. "
                                   "The key is kept in this computer's Ng DAW settings."
                                 : "Runs on this computer: start the server and load a model that supports tools (function "
                                   "calling) first. Nothing leaves your computer.",
                      juce::dontSendNotification);
        loading = false;
        save();
    }

    void save()
    {
        if (loading)
            return;

        auto p = AiChat::findProvider (provider.getText());
        p.baseUrl = address.getText().trim();
        p.apiKey = key.getText().trim();
        p.model = model.getText().trim();
        settings::setAiProvider (engine(), p);
    }

    void fetchModels()
    {
        if (fetching.joinable())
            return;

        loadModels.setEnabled (false);
        loadModels.setButtonText ("Loading...");
        auto p = settings::getAiProvider (engine(), provider.getText());

        fetching = std::thread ([p, cancellation = cancellation, safeThis = juce::Component::SafePointer (this)]
        {
            juce::String error;
            auto models = AiChat::listModels (p, AiChat::httpTransport (cancellation), error);

            juce::MessageManager::callAsync ([safeThis, models, error]
            {
                if (safeThis == nullptr)
                    return;

                safeThis->fetching.join();
                safeThis->loadModels.setEnabled (true);
                safeThis->loadModels.setButtonText ("Load models");

                if (error.isNotEmpty())
                {
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Couldn't load the models", error);
                    return;
                }

                auto current = safeThis->model.getText();
                safeThis->model.clear (juce::dontSendNotification);
                safeThis->model.addItemList (models, 1);
                safeThis->model.setText (current.isNotEmpty() ? current : models[0], juce::dontSendNotification);
                safeThis->save();
            });
        });
    }

    Session& session;
    bool loading = false;
    juce::Label providerLabel { {}, "Provider" }, addressLabel { {}, "Address" }, keyLabel { {}, "API key" },
                modelLabel { {}, "Model" }, note, connectHeader { {}, "Use Ng DAW from Claude Desktop or Claude Code" };
    juce::ComboBox provider, model;
    juce::TextEditor address, key, connectText;
    juce::TextButton loadModels { "Load models" }, copyCommand { "Copy Claude Code command" },
                     copyConfig { "Copy Claude Desktop settings" };
    juce::String command, config;
    std::shared_ptr<AiChat::Cancellation> cancellation = std::make_shared<AiChat::Cancellation>();
    std::thread fetching;
};

//==============================================================================
PreferencesPanel::PreferencesPanel (Session& session, std::function<void()> onChanged, const juce::String& tab)
{
    auto& dm = session.getEngine().getDeviceManager().deviceManager;
    auto background = getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId);

    tabs.addTab ("Audio and MIDI", background, new juce::AudioDeviceSelectorComponent (dm, 0, 2, 0, 2, true, true, true, false), true);
    tabs.addTab ("General", background, new General (session, std::move (onChanged)), true);
    tabs.addTab ("AI", background, new Ai (session), true);
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
