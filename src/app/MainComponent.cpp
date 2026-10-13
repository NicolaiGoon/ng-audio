#include "MainComponent.h"
#include "Widgets.h"

namespace ng
{
namespace
{
    //==============================================================================
    /** A channel's step pattern: one pad per 16th note, in groups of four. Click to toggle. */
    class StepGrid  : public juce::Component,
                      public juce::SettableTooltipClient
    {
    public:
        StepGrid (CommandDispatcher& d, juce::String channelId, juce::String stepText,
                  juce::Colour c, const int& currentStep)
            : dispatcher (d), id (std::move (channelId)), steps (std::move (stepText)),
              colour (c), playStep (currentStep)
        {
            setTooltip ("Click a step to turn it on or off");
        }

        void paint (juce::Graphics& g) override
        {
            for (int i = 0; i < steps.length(); ++i)
            {
                auto pad = getPadBounds (i).reduced (1.5f);
                auto on = steps[i] == 'x';
                auto offColour = (i / 4) % 2 == 0 ? juce::Colour (0xff3a3f46) : juce::Colour (0xff2e3238);

                g.setColour (on ? colour : offColour);
                g.fillRoundedRectangle (pad, 3.0f);

                if (i == playStep)
                {
                    g.setColour (juce::Colours::white.withAlpha (on ? 0.8f : 0.45f));
                    g.drawRoundedRectangle (pad, 3.0f, 2.0f);
                }
            }
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            for (int i = 0; i < steps.length(); ++i)
                if (getPadBounds (i).contains (e.position))
                    dispatcher.execute ("set_steps", makeObject ({ { "channel", id }, { "step", i } }));
        }

    private:
        juce::Rectangle<float> getPadBounds (int i) const
        {
            // A small gap between each group of four steps.
            constexpr float groupGap = 6.0f;
            auto groups = (steps.length() + 3) / 4;
            auto padWidth = juce::jmin (28.0f, ((float) getWidth() - groupGap * (float) (groups - 1)) / (float) juce::jmax (1, steps.length()));
            auto x = (float) i * padWidth + (float) (i / 4) * groupGap;
            return { x, 0.0f, padWidth, (float) getHeight() };
        }

        CommandDispatcher& dispatcher;
        juce::String id, steps;
        juce::Colour colour;
        const int& playStep;
    };

    //==============================================================================
    /** One instrument in the channel rack: name, colour, instrument, which strip it feeds. */
    class ChannelRow  : public juce::Component
    {
    public:
        ChannelRow (CommandDispatcher& d, const juce::var& channel, const juce::var& strips, const int& playStep)
            : dispatcher (d), id (channel["id"].toString()), swatch (d, id, colourFrom (channel["colour"])),
              grid (d, id, channel["pattern"]["steps"].toString(), colourFrom (channel["colour"]), playStep)
        {
            setUpNameLabel (name, dispatcher, id, channel["name"].toString());
            setUpDeleteButton (deleteButton, dispatcher, id, "channel");

            status.setText (channel["linked"] ? "linked" : "detached", juce::dontSendNotification);
            status.setColour (juce::Label::textColourId, juce::Colours::grey);

            instrument.addItem ("4OSC synth", 1);
            instrument.addItem ("No instrument", 2);
            instrument.setSelectedId (channel["instrument"] == "4osc" ? 1 : 2, juce::dontSendNotification);
            instrument.onChange = [this]
            {
                dispatcher.execute ("set_instrument", makeObject ({ { "channel", id },
                                                                    { "instrument", instrument.getSelectedId() == 1 ? "4osc" : "none" } }));
            };

            output.addItem ("Master", 1);
            int selected = 1;

            for (int i = 0; i < strips.size(); ++i)
            {
                auto& s = strips[i];
                output.addItem ("-> " + s["name"].toString(), i + 2);
                stripIds.add (s["id"].toString());

                if (s["id"] == channel["output"])
                    selected = i + 2;
            }

            output.setSelectedId (selected, juce::dontSendNotification);
            output.onChange = [this]
            {
                auto index = output.getSelectedId() - 2;
                auto dest = index >= 0 ? stripIds[index] : juce::String ("master");
                dispatcher.execute ("route", makeObject ({ { "channel", id }, { "strip", dest } }));
            };

            for (auto* c : std::initializer_list<juce::Component*> { &swatch, &name, &status, &instrument, &output, &deleteButton, &grid })
                addAndMakeVisible (c);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (0, 4);
            grid.setBounds (r.removeFromBottom (22).withTrimmedLeft (20));
            r.removeFromBottom (4);
            swatch.setBounds (r.removeFromLeft (14));
            r.removeFromLeft (6);
            deleteButton.setBounds (r.removeFromRight (26));
            r.removeFromRight (4);
            output.setBounds (r.removeFromRight (130));
            r.removeFromRight (4);
            instrument.setBounds (r.removeFromRight (120));
            status.setBounds (r.removeFromRight (64));
            name.setBounds (r);
        }

    private:
        CommandDispatcher& dispatcher;
        juce::String id;
        ColourSwatch swatch;
        juce::StringArray stripIds;
        juce::Label name, status;
        juce::ComboBox instrument, output;
        juce::TextButton deleteButton;
        StepGrid grid;
    };
}

//==============================================================================
MainComponent::MainComponent (te::Engine& engine)
    : session (engine)
{
    registerProjectCommands (registry);

    dispatcher.onCommandExecuted = [this] (const juce::String& name, const CommandResult& result)
    {
        if (! result.ok && result.error.isNotEmpty())
            DBG (name << " failed: " << result.error);

        triggerAsyncUpdate();
    };

    // Before a project is replaced, let go of everything that points into it.
    session.onEditAboutToChange = [this]
    {
        mixer.clear();
        channelRows.clear();
        channelRackKey = {};
    };

    newButton.onClick = [this] { newProject(); };
    openButton.onClick = [this] { openProject(); };
    saveButton.onClick = [this] { saveProject(); };
    saveAsButton.onClick = [this] { saveProjectAs(); };
    newButton.setTooltip ("New project (Ctrl+N)");
    openButton.setTooltip ("Open a project (Ctrl+O)");
    saveButton.setTooltip ("Save (Ctrl+S)");
    saveAsButton.setTooltip ("Save under a new name (Ctrl+Shift+S)");

    addButton.onClick = [this] { addInstrument(); };
    playButton.onClick = [this]
    {
        dispatcher.execute (session.getEdit().getTransport().isPlaying() ? "stop" : "play");
    };
    undoButton.onClick = [this] { dispatcher.undo(); };
    redoButton.onClick = [this] { dispatcher.redo(); };
    settingsButton.onClick = [this] { showAudioSettings(); };
    setUpTransportControls();
    runButton.onClick = [this] { runConsoleCommand(); };

    for (auto* l : { &channelsHeader, &mixerHeader, &consoleHeader })
        l->setFont (juce::FontOptions (15.0f, juce::Font::bold));

    consoleInput.setMultiLine (true);
    consoleInput.setReturnKeyStartsNewLine (true);
    consoleInput.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain));
    consoleInput.setText (R"({"command": "get_state"})");

    consoleOutput.setMultiLine (true);
    consoleOutput.setReadOnly (true);
    consoleOutput.setFont (consoleInput.getFont());

    for (auto* c : std::initializer_list<juce::Component*> { &newButton, &openButton, &saveButton, &saveAsButton,
                                                             &addButton, &playButton, &undoButton, &redoButton,
                                                             &tempo, &timeSignature, &loopButton,
                                                             &settingsButton, &runButton, &channelsHeader,
                                                             &mixerHeader, &consoleHeader, &consoleInput, &mixer,
                                                             &consoleOutput })
        addAndMakeVisible (c);

    loadStarterProject();

    setWantsKeyboardFocus (true);
    setSize (1180, 760);
    startTimerHz (30);
}

MainComponent::~MainComponent()
{
    dispatcher.onCommandExecuted = nullptr;
    session.onEditAboutToChange = nullptr;
}

//==============================================================================
void MainComponent::addInstrument()
{
    // A new channel with its linked strip and an empty pattern, ready to fill in.
    auto name = "Synth " + juce::String (session.getChannels().size() + 1);
    dispatcher.execute ("create_track", makeObject ({ { "name", name } }));
}

void MainComponent::loadStarterProject()
{
    // One instrument and a four-on-the-floor pattern, so Play makes a sound straight away.
    // Nobody has changed it yet, so it starts with nothing to undo and nothing to save.
    dispatcher.executeBatch ("Starter pattern", juce::Array<juce::var> {
        makeObject ({ { "command", "create_track" }, { "params", makeObject ({ { "name", "Synth 1" } }) } }),
        makeObject ({ { "command", "set_steps" },
                      { "params", makeObject ({ { "channel", "Synth 1" }, { "steps", "x...x...x...x..." } }) } })
    });

    session.getUndoManager().clearUndoHistory();
    session.markAsSaved();
    updateWindowTitle();
}

//==============================================================================
void MainComponent::confirmDiscardChanges (std::function<void()> onContinue)
{
    if (! session.hasUnsavedChanges())
    {
        onContinue();
        return;
    }

    auto options = juce::MessageBoxOptions()
                       .withIconType (juce::MessageBoxIconType::QuestionIcon)
                       .withTitle ("Unsaved changes")
                       .withMessage ("Save the changes to \"" + session.getProjectName() + "\" first?")
                       .withButton ("Save")
                       .withButton ("Don't save")
                       .withButton ("Cancel")
                       .withAssociatedComponent (this);

    // With three buttons the results are 1, 2 and 0 (Cancel).
    juce::AlertWindow::showAsync (options, [safeThis = juce::Component::SafePointer (this), onContinue] (int result)
    {
        if (safeThis == nullptr)
            return;

        if (result == 1)
            safeThis->saveProject (onContinue);
        else if (result == 2)
            onContinue();
    });
}

void MainComponent::newProject()
{
    confirmDiscardChanges ([this]
    {
        dispatcher.execute ("new_project", makeObject ({ { "discard_changes", true } }));
        loadStarterProject();
    });
}

namespace
{
    juce::File defaultProjectFolder (const juce::File& current)
    {
        return current.existsAsFile() ? current.getParentDirectory()
                                      : juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    }

    const juce::String projectWildcard = juce::String ("*") + projectFileExtension;
}

void MainComponent::openProject()
{
    confirmDiscardChanges ([this]
    {
        fileChooser = std::make_unique<juce::FileChooser> ("Open project", defaultProjectFolder (session.getFile()), projectWildcard);
        fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [this] (const juce::FileChooser& chooser)
        {
            auto file = chooser.getResult();

            if (file == juce::File())
                return;

            auto result = dispatcher.execute ("open_project", makeObject ({ { "path", file.getFullPathName() },
                                                                            { "discard_changes", true } }));
            if (! result.ok)
                showError ("Couldn't open the project", result.error);
        });
    });
}

void MainComponent::saveProject (std::function<void()> onSaved)
{
    if (session.getFile() == juce::File())
    {
        saveProjectAs (std::move (onSaved));
        return;
    }

    auto result = dispatcher.execute ("save_project");

    if (! result.ok)
        showError ("Couldn't save the project", result.error);
    else if (onSaved != nullptr)
        onSaved();
}

void MainComponent::saveProjectAs (std::function<void()> onSaved)
{
    auto folder = defaultProjectFolder (session.getFile());
    auto suggestion = folder.getChildFile (session.getProjectName()).withFileExtension (projectFileExtension);

    fileChooser = std::make_unique<juce::FileChooser> ("Save project as", suggestion, projectWildcard);
    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this, onSaved] (const juce::FileChooser& chooser)
    {
        auto file = chooser.getResult();

        if (file == juce::File())
            return;

        if (! file.hasFileExtension (projectFileExtension))
            file = file.withFileExtension (projectFileExtension);

        auto result = dispatcher.execute ("save_project", makeObject ({ { "path", file.getFullPathName() } }));

        if (! result.ok)
            showError ("Couldn't save the project", result.error);
        else if (onSaved != nullptr)
            onSaved();
    });
}

void MainComponent::showError (const juce::String& title, const juce::String& message)
{
    juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsOk (juce::MessageBoxIconType::WarningIcon,
                                                                         title, message, {}, this),
                                  nullptr);
}

void MainComponent::updateWindowTitle()
{
    if (auto* window = findParentComponentOfClass<juce::DocumentWindow>())
        window->setName (session.getProjectName() + (session.hasUnsavedChanges() ? "*" : "") + " - Ng DAW");
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    auto command = juce::ModifierKeys::commandModifier;

    if (key == juce::KeyPress ('n', command, 0))                                     { newProject(); return true; }
    if (key == juce::KeyPress ('o', command, 0))                                     { openProject(); return true; }
    if (key == juce::KeyPress ('s', command, 0))                                     { saveProject(); return true; }
    if (key == juce::KeyPress ('s', command | juce::ModifierKeys::shiftModifier, 0)) { saveProjectAs(); return true; }

    return false;
}

void MainComponent::runConsoleCommand()
{
    consoleOutput.setText (dispatcher.executeJson (consoleInput.getText()));
}

namespace
{
    // The time signatures offered in the transport; set_time_signature takes any.
    const std::pair<int, int> commonTimeSignatures[] = { { 2, 4 }, { 3, 4 }, { 4, 4 }, { 5, 4 }, { 6, 8 }, { 7, 8 }, { 12, 8 } };
}

void MainComponent::setUpTransportControls()
{
    tempo.setSliderStyle (juce::Slider::IncDecButtons);
    tempo.setIncDecButtonsMode (juce::Slider::incDecButtonsDraggable_Vertical);
    tempo.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 80, 30);
    tempo.setRange (20.0, 300.0, 1.0);
    tempo.setTextValueSuffix (" BPM");
    tempo.setTooltip ("Tempo: type a value, click the arrows or drag up and down");
    tempo.onDragStart = [this] { dispatcher.beginGesture ("Tempo"); };
    tempo.onDragEnd = [this] { dispatcher.endGesture(); };
    tempo.onValueChange = [this] { dispatcher.execute ("set_tempo", makeObject ({ { "bpm", tempo.getValue() } })); };

    for (int i = 0; i < (int) std::size (commonTimeSignatures); ++i)
        timeSignature.addItem (juce::String (commonTimeSignatures[i].first) + "/" + juce::String (commonTimeSignatures[i].second), i + 1);

    timeSignature.setTooltip ("Time signature");
    timeSignature.onChange = [this]
    {
        auto index = timeSignature.getSelectedId() - 1;

        if (juce::isPositiveAndBelow (index, (int) std::size (commonTimeSignatures)))
            dispatcher.execute ("set_time_signature", makeObject ({ { "numerator", commonTimeSignatures[index].first },
                                                                    { "denominator", commonTimeSignatures[index].second } }));
    };

    loopButton.setTooltip ("Loop playback (the whole project unless a loop region is set)");
    loopButton.onClick = [this]
    {
        dispatcher.execute ("set_loop", makeObject ({ { "enabled", loopButton.getToggleState() } }));
    };
}

void MainComponent::updateTransportControls (const juce::var& state)
{
    if (! tempo.isMouseButtonDown (true))
        tempo.setValue (state["tempo_bpm"], juce::dontSendNotification);

    auto numerator = static_cast<int> (state["time_signature"]["numerator"]);
    auto denominator = static_cast<int> (state["time_signature"]["denominator"]);
    auto text = juce::String (numerator) + "/" + juce::String (denominator);
    timeSignature.setSelectedId (0, juce::dontSendNotification);

    for (int i = 0; i < (int) std::size (commonTimeSignatures); ++i)
        if (commonTimeSignatures[i] == std::make_pair (numerator, denominator))
            timeSignature.setSelectedId (i + 1, juce::dontSendNotification);

    if (timeSignature.getSelectedId() == 0)
        timeSignature.setText (text, juce::dontSendNotification);

    loopButton.setToggleState (state["transport"]["looping"], juce::dontSendNotification);
}

void MainComponent::showAudioSettings()
{
    auto& dm = session.getEngine().getDeviceManager().deviceManager;
    auto selector = std::make_unique<juce::AudioDeviceSelectorComponent> (dm, 0, 2, 0, 2, true, true, true, false);
    selector->setSize (500, 400);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (selector.release());
    options.dialogTitle = "Audio settings";
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.launchAsync();
}

//==============================================================================
void MainComponent::handleAsyncUpdate()
{
    rebuildRows();
    updateButtons();
    updateWindowTitle();
}

void MainComponent::timerCallback()
{
    updateButtons();
    updatePlayStep();
}

void MainComponent::updatePlayStep()
{
    // The 16th note under the playhead, or -1 when stopped. Each row checks it against its own length.
    auto& edit = session.getEdit();
    auto& transport = edit.getTransport();
    int step = -1;

    if (transport.isPlaying())
    {
        auto beats = edit.tempoSequence.toBeats (transport.getPosition()).inBeats();
        step = (int) std::floor (beats / 0.25 + 1.0e-6);
    }

    if (step != playStep)
    {
        playStep = step;

        for (auto* row : channelRows)
            row->repaint();
    }
}

void MainComponent::updateButtons()
{
    playButton.setButtonText (session.getEdit().getTransport().isPlaying() ? "Stop" : "Play");
    undoButton.setEnabled (dispatcher.canUndo());
    redoButton.setEnabled (dispatcher.canRedo());
}

void MainComponent::rebuildRows()
{
    auto state = session.toVar();
    auto strips = state["strips"];

    // The rack shows channels and the strip names they can route to; fader moves don't change it.
    juce::String key = juce::JSON::toString (state["channels"], true);

    for (auto& s : *strips.getArray())
        key << "|" << s["id"].toString() << ":" << s["name"].toString();

    if (key != channelRackKey)
    {
        channelRackKey = key;
        channelRows.clear();

        for (auto& c : *state["channels"].getArray())
            addAndMakeVisible (channelRows.add (new ChannelRow (dispatcher, c, strips, playStep)));
    }

    mixer.update (state);
    updateTransportControls (state);
    resized();
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void MainComponent::resized()
{
    auto r = getLocalBounds().reduced (10);

    auto toolbar = r.removeFromTop (30);

    for (auto* b : { &newButton, &openButton, &saveButton, &saveAsButton })
    {
        b->setBounds (toolbar.removeFromLeft (b == &saveAsButton ? 70 : 56));
        toolbar.removeFromLeft (4);
    }

    toolbar.removeFromLeft (14);

    for (auto* b : { &addButton, &playButton, &undoButton, &redoButton })
    {
        b->setBounds (toolbar.removeFromLeft (b == &addButton ? 110 : 64));
        toolbar.removeFromLeft (4);
    }

    toolbar.removeFromLeft (14);
    tempo.setBounds (toolbar.removeFromLeft (150));
    toolbar.removeFromLeft (6);
    timeSignature.setBounds (toolbar.removeFromLeft (70));
    toolbar.removeFromLeft (10);
    loopButton.setBounds (toolbar.removeFromLeft (70));

    settingsButton.setBounds (toolbar.removeFromRight (120));
    r.removeFromTop (10);

    auto console = r.removeFromBottom (170);
    consoleHeader.setBounds (console.removeFromTop (24));
    auto consoleTop = console.removeFromTop (60);
    runButton.setBounds (consoleTop.removeFromRight (70).reduced (4, 14));
    consoleInput.setBounds (consoleTop);
    console.removeFromTop (6);
    consoleOutput.setBounds (console);
    r.removeFromBottom (10);

    auto left = r.removeFromLeft (juce::jmax (500, r.getWidth() * 9 / 20)).withTrimmedRight (8);
    auto right = r.withTrimmedLeft (8);

    channelsHeader.setBounds (left.removeFromTop (24));
    for (auto* row : channelRows)
        row->setBounds (left.removeFromTop (60));

    mixerHeader.setBounds (right.removeFromTop (24));
    mixer.setBounds (right);
}

} // namespace ng
