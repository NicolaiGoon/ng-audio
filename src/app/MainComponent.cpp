#include "MainComponent.h"
#include "ExportPanel.h"
#include "PluginWindow.h"
#include "TypingKeyboard.h"
#include "Widgets.h"

namespace ng
{
namespace
{
    //==============================================================================
    /** Every keyboard shortcut, in sections, for the ? button and F1. */
    class ShortcutsList  : public juce::Component
    {
    public:
        ShortcutsList()
        {
            setSize (2 * columnWidth + 3 * margin, std::max (getColumnHeight (left()), getColumnHeight (right())));
        }

        void paint (juce::Graphics& g) override
        {
            paintColumn (g, left(), margin);
            paintColumn (g, right(), columnWidth + 2 * margin);
        }

    private:
        // A row with no description is a section heading.
        using Rows = std::vector<std::pair<const char*, const char*>>;

        static const Rows& left()
        {
            static const Rows rows {
                { "Everywhere", nullptr },
                { "Space", "Play or stop" },
                { "Home", "Back to the start" },
                { "R", "Record into the armed channel, again to stop" },
                { "C", "Metronome on or off" },
                { "Ctrl+L", "Looping on or off" },
                { "Ctrl+Z", "Undo" },
                { "Ctrl+Y, Ctrl+Shift+Z", "Redo" },
                { "Ctrl+N", "New project" },
                { "Ctrl+O", "Open a project" },
                { "Ctrl+S", "Save" },
                { "Ctrl+Shift+S", "Save as" },
                { "Ctrl+T", "Add an instrument" },
                { "Ctrl+I", "Import audio files" },
                { "Ctrl+E", "Export the song, the loop or stems" },
                { "Ctrl+K", "Typing keyboard on or off" },
                { "Ctrl+Shift+K", "Show or hide the piano keyboard" },
                { "Ctrl+B", "Show or hide the browser" },
                { "Ctrl+J", "Show or hide the AI chat" },
                { "Ctrl+H", "The undo history" },
                { "Ctrl+,", "Settings" },
                { "F1", "This list" },
                { "Typing keyboard (FL Studio style)", nullptr },
                { "Z S X D C V ...", "Play from middle C" },
                { "Q 2 W 3 E R ...", "Play the octave above" },
            };
            return rows;
        }

        static const Rows& right()
        {
            static const Rows rows {
                { "Timeline", nullptr },
                { "Click a clip", "Pick it (what clicks on its lane paint)" },
                { "Click, drag", "Paint copies of it along the lane" },
                { "Right-click, drag", "Delete clips" },
                { "Drag in the ruler", "Draw a loop" },
                { "Alt + wheel", "Taller or shorter lanes" },
                { "Ctrl + left click", "Fit the whole song in view" },
                { "Enter", "Edit the clip's notes" },
                { "Del", "Delete the clip" },
                { "Ctrl+D", "Duplicate it after itself" },
                { "Ctrl+C", "Copy it" },
                { "Ctrl+V", "Paste a copy at the playhead" },
                { "S", "Split it at the playhead" },
                { "Left, Right", "Move it by a beat (Shift: a bar)" },
                { "Up, Down", "Move it to the channel above or below" },
                { "Esc", "Deselect it" },
                { "Piano roll", nullptr },
                { "Click, drag", "Draw notes (as long as the last one)" },
                { "Right-click, drag", "Delete notes" },
                { "Ctrl+A", "Select all notes" },
                { "Del", "Delete the selected notes" },
                { "Ctrl+C, Ctrl+X", "Copy or cut them" },
                { "Ctrl+V", "Paste at the playhead" },
                { "Ctrl+D", "Duplicate them after themselves" },
                { "Up, Down", "Move by a semitone (Shift: an octave)" },
                { "Left, Right", "Move by the snap grid" },
                { "Q", "Quantize" },
                { "Esc", "Deselect, then close the piano roll" },
            };
            return rows;
        }

        static int getColumnHeight (const Rows& rows)
        {
            auto height = margin;

            for (auto& row : rows)
                height += row.second == nullptr ? headingHeight : rowHeight;

            return height + margin;
        }

        static void paintColumn (juce::Graphics& g, const Rows& rows, int x)
        {
            auto y = margin;

            for (auto& row : rows)
            {
                if (row.second == nullptr)
                {
                    g.setColour (juce::Colours::white);
                    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
                    g.drawText (row.first, x, y + headingHeight - rowHeight - 4, columnWidth, rowHeight, juce::Justification::centredLeft);
                    y += headingHeight;
                    continue;
                }

                g.setColour (juce::Colour (0xff8fd18f));
                g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::bold));
                g.drawText (row.first, x, y, keyWidth, rowHeight, juce::Justification::centredLeft);

                g.setColour (juce::Colours::lightgrey);
                g.setFont (juce::FontOptions (14.0f));
                g.drawText (row.second, x + keyWidth, y, columnWidth - keyWidth, rowHeight, juce::Justification::centredLeft);
                y += rowHeight;
            }
        }

        static constexpr int margin = 16, columnWidth = 440, keyWidth = 170, rowHeight = 22, headingHeight = 36;
    };

    //==============================================================================
    /** The Plugins window: JUCE's plugin list (every scanned plugin, sortable, with
        Options for removing entries) under a Scan button. The scan asks which folders
        to look in and remembers them, for scan_plugins too; the engine saves the list. */
    class PluginsPanel  : public juce::Component,
                          private juce::ChangeListener
    {
    public:
        explicit PluginsPanel (te::Engine& engine)
            : plugins (engine.getPluginManager()),
              list (plugins.pluginFormatManager, plugins.knownPluginList,
                    engine.getTemporaryFileManager().getTempFile ("PluginScanDeadMansPedal"),
                    &engine.getPropertyStorage().getPropertiesFile(), true)
        {
            scanButton.setTooltip ("Pick the folders your VST3 plugins are in, then scan them for new or updated plugins");
            scanButton.onClick = [this]
            {
                for (auto* format : plugins.pluginFormatManager.getFormats())
                    if (format->getName() == "VST3")
                        list.scanFor (*format);
            };

            summary.setColour (juce::Label::textColourId, juce::Colours::grey);
            plugins.knownPluginList.addChangeListener (this);
            updateSummary();

            for (auto* c : std::initializer_list<juce::Component*> { &scanButton, &summary, &list })
                addAndMakeVisible (c);

            setSize (760, 520);
        }

        ~PluginsPanel() override
        {
            plugins.knownPluginList.removeChangeListener (this);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (8);
            auto top = r.removeFromTop (30);
            scanButton.setBounds (top.removeFromLeft (180));
            top.removeFromLeft (12);
            summary.setBounds (top);
            r.removeFromTop (8);
            list.setBounds (r);
        }

    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override
        {
            updateSummary();
        }

        void updateSummary()
        {
            auto types = plugins.knownPluginList.getTypes();
            auto instruments = (int) std::count_if (types.begin(), types.end(), [] (auto& d) { return d.isInstrument; });
            auto count = [] (int n, const char* thing) { return juce::String (n) + " " + thing + (n == 1 ? "" : "s"); };
            auto text = types.isEmpty() ? juce::String ("No plugins yet. Scan to find the ones installed on this computer.")
                                        : count (instruments, "instrument") + " and " + count (types.size() - instruments, "effect")
                                              + ". Right-click one to remove it or show its file.";
            summary.setText (text, juce::dontSendNotification);
        }

        te::PluginManager& plugins;
        juce::TextButton scanButton { "Scan for VST3 plugins..." };
        juce::Label summary;
        juce::PluginListComponent list;
    };

    // The time signatures offered in the transport; set_time_signature takes any.
    const std::pair<int, int> commonTimeSignatures[] = { { 2, 4 }, { 3, 4 }, { 4, 4 }, { 5, 4 }, { 6, 8 }, { 7, 8 }, { 12, 8 } };
}

//==============================================================================
MainComponent::MainComponent (te::Engine& engine)
    : session (engine)
{
    registerProjectCommands (registry);
    session.setDefaultFolder (settings::getProjectFolder (engine));

    // Built-in effects' windows change them through the dispatcher.
    if (auto windows = dynamic_cast<PluginWindowBehaviour*> (&engine.getUIBehaviour()))
        windows->setDispatcher (&dispatcher);

    dispatcher.onCommandExecuted = [this] (const juce::String& name, const CommandResult& result)
    {
        if (! result.ok && result.error.isNotEmpty())
            DBG (name << " failed: " << result.error);

        // A new preset or sample folder shows up in the browser (not straight away: the
        // browser runs commands itself while it refreshes).
        if (result.ok && (name == "save_preset" || name == "set_library"))
            browserNeedsRefresh = true;

        // Once a project is saved or replaced, the autosave of its old changes goes.
        if (result.ok && (name == "new_project" || name == "open_project" || name == "save_project"))
        {
            autosave.discard();
            lastAutosave = juce::Time::getCurrentTime();
        }

        commandLog.refresh();
        triggerAsyncUpdate();
    };

    // The first change an AI client makes opens the console, so its log is in view.
    dispatcher.onLog = [this] (const CommandDispatcher::LogEntry& entry)
    {
        commandLog.add (entry);

        // (Not for the chat, whose own transcript already shows what it did.)
        if (! commandLogShown && entry.source != "Console" && ! entry.source.endsWith ("(chat)") && ! consoleShown)
            showConsole (true);

        commandLogShown = true;
    };

    // Before a project is replaced, let go of everything that points into it.
    session.onEditAboutToChange = [this]
    {
        releaseTypedNotes (true);
        mixer.clear();
        timeline.clear();
        closePianoRoll();
        closeSampleEditor();
        rack.clear();
    };

    setUpTopBar();
    setUpPanels();

    keyboardState.addListener (this);
    keyboard.setAvailableRange (24, 108);
    keyboard.setLowestVisibleKey (48);
    keyboard.setOctaveForMiddleC (4);   // middle C is C4, as in the piano roll
    keyboard.clearKeyMappings();        // the typing keyboard plays from the computer keys instead
    keyboard.setWantsKeyboardFocus (false);
    addChildComponent (keyboard);

    consoleHeader.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    consoleInput.setMultiLine (true);
    consoleInput.setReturnKeyStartsNewLine (true);
    consoleInput.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain));
    consoleInput.setText (R"({"command": "get_state"})");
    consoleOutput.setMultiLine (true);
    consoleOutput.setReadOnly (true);
    consoleOutput.setFont (consoleInput.getFont());
    runButton.onClick = [this] { runConsoleCommand(); };

    for (auto* c : std::initializer_list<juce::Component*> { &consoleHeader, &consoleInput, &consoleOutput, &runButton, &commandLog })
        addChildComponent (c);

    loadStarterProject();

    if (auto error = bridge.start(); error.isNotEmpty())
        DBG ("AI bridge: " << error);

    setWantsKeyboardFocus (true);
    juce::Desktop::getInstance().addFocusChangeListener (this);
    session.getEngine().getPluginManager().knownPluginList.addChangeListener (this);   // a scan adds instruments to pick
    setTypingKeyboard (getProperties().getBoolValue ("ngTypingKeyboard", true));
    setSize (1440, 860);
    startTimerHz (30);

    // After the window is up: panels that were in their own windows go back there, and
    // anything a crash left unsaved is offered back.
    juce::MessageManager::callAsync ([safeThis = juce::Component::SafePointer (this)]
    {
        if (safeThis != nullptr)
        {
            safeThis->restoreDetachedPanels();
            safeThis->checkForRecovery();
        }
    });
}

MainComponent::~MainComponent()
{
    saveLayout();
    releaseTypedNotes (true);

    if (auto windows = dynamic_cast<PluginWindowBehaviour*> (&session.getEngine().getUIBehaviour()))
        windows->setDispatcher (nullptr);

    // Effect windows can use the dispatcher, which goes before the project does.
    session.forEachEffect ([] (te::Plugin& effect) { effect.hideWindowForShutdown(); });
    juce::Desktop::getInstance().removeFocusChangeListener (this);
    session.getEngine().getPluginManager().knownPluginList.removeChangeListener (this);
    keyboardState.removeListener (this);
    bridge.stop();
    dispatcher.onLog = nullptr;
    dispatcher.onCommandExecuted = nullptr;
    session.onEditAboutToChange = nullptr;
    pianoRoll.clear();
    sampleEditor.clear();
}

//==============================================================================
void MainComponent::setUpTopBar()
{
    newButton.onClick = [this] { newProject(); };
    openButton.onClick = [this] { openProject(); };
    saveButton.onClick = [this] { saveProject(); };
    undoButton.onClick = [this] { dispatcher.undo(); };
    redoButton.onClick = [this] { dispatcher.redo(); };

    returnButton.onClick = [this] { dispatcher.execute ("set_position", makeObject ({ { "beat", 0 } })); };
    playButton.setOnColour (theme::play);
    playButton.onClick = [this]
    {
        dispatcher.execute (session.getEdit().getTransport().isPlaying() ? "stop" : "play");
    };
    recordButton.setIconColour (theme::record);
    recordButton.setOnColour (theme::record);
    recordButton.onClick = [this] { toggleRecording(); };

    loopButton.onClick = [this]
    {
        dispatcher.execute ("set_loop", makeObject ({ { "enabled", ! loopButton.getToggleState() } }));
    };
    metronomeButton.onClick = [this]
    {
        dispatcher.execute ("set_metronome", makeObject ({ { "enabled", ! metronomeButton.getToggleState() } }));
    };

    typingKeysButton.onClick = [this] { setTypingKeyboard (! typingKeyboard::enabled); };
    pianoKeysButton.onClick = [this] { showKeyboard (! keyboard.isVisible()); };
    browserButton.onClick = [this] { showBrowser (! browserPanel.isVisible()); };
    chatButton.onClick = [this] { showChat (! chatPanel.isVisible()); };
    consoleButton.onClick = [this] { showConsole (! consoleShown); };
    historyButton.onClick = [this] { showHistory(); };
    exportButton.onClick = [this] { showExport(); };
    settingsButton.onClick = [this] { showPreferences(); };
    helpButton.onClick = [this] { showShortcuts(); };

    tempo.setSliderStyle (juce::Slider::IncDecButtons);
    tempo.setIncDecButtonsMode (juce::Slider::incDecButtonsDraggable_Vertical);
    tempo.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 70, 30);
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

    positionDisplay.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 17.0f, juce::Font::bold));
    positionDisplay.setJustificationType (juce::Justification::centred);
    positionDisplay.setColour (juce::Label::backgroundColourId, juce::Colour (0xff15181b));
    positionDisplay.setColour (juce::Label::textColourId, juce::Colour (0xff8fd18f));
    positionDisplay.setColour (juce::Label::outlineColourId, juce::Colours::white.withAlpha (0.08f));
    positionDisplay.setTooltip ("Song position: bar . beat . sixteenth");

    cpuMeter.setFont (juce::FontOptions (12.0f));
    cpuMeter.setJustificationType (juce::Justification::centred);
    cpuMeter.setColour (juce::Label::outlineColourId, juce::Colours::white.withAlpha (0.1f));

    for (auto* c : std::initializer_list<juce::Component*> { &menuBar, &newButton, &openButton, &saveButton, &undoButton, &redoButton,
                                                             &returnButton, &playButton, &recordButton, &tempo, &timeSignature,
                                                             &positionDisplay, &loopButton, &metronomeButton, &typingKeysButton,
                                                             &pianoKeysButton, &cpuMeter, &browserButton, &chatButton, &consoleButton,
                                                             &historyButton, &exportButton, &settingsButton, &helpButton })
        addAndMakeVisible (c);
}

void MainComponent::setUpPanels()
{
    auto& props = getProperties();
    browserWidth = juce::jlimit (160, 600, props.getIntValue ("ngBrowserWidth", 240));
    chatWidth = juce::jlimit (260, 800, props.getIntValue ("ngChatWidth", 360));
    timelineProportion = juce::jlimit (0.1, 0.9, props.getDoubleValue ("ngTimelineProportion", 0.48));
    rackProportion = juce::jlimit (0.1, 0.9, props.getDoubleValue ("ngRackProportion", 0.5));

    // The timeline.
    timeline.onOpenClip = [this] (const juce::String& clipId)
    {
        if (dynamic_cast<te::WaveAudioClip*> (session.findClip (clipId)) != nullptr)
            openSampleEditor (clipId);
        else
            openPianoRoll (clipId);
    };
    addAudioButton.onClick = [this] { showAudioMenu (&addAudioButton); };
    timelineZoomOut.onClick = [this] { timeline.zoomHorizontally (0.7); };
    timelineZoomIn.onClick = [this] { timeline.zoomHorizontally (1.0 / 0.7); };
    timelineShorter.onClick = [this] { timeline.zoomVertically (0.8); };
    timelineTaller.onClick = [this] { timeline.zoomVertically (1.25); };

    for (auto* hint : { &timelineHint, &pianoRollHint, &snapLabel })
    {
        hint->setFont (juce::FontOptions (12.0f));
        hint->setColour (juce::Label::textColourId, juce::Colours::grey);
    }

    snapLabel.setJustificationType (juce::Justification::centredRight);
    timelinePanel.addHeaderItem (timelineHint, 700);
    timelinePanel.addHeaderItem (addAudioButton, 78, true);
    timelinePanel.addHeaderItem (timelineTaller, 24, true);
    timelinePanel.addHeaderItem (timelineShorter, 24, true);
    timelinePanel.addHeaderItem (timelineZoomIn, 24, true);
    timelinePanel.addHeaderItem (timelineZoomOut, 24, true);

    // The rack and the mixer.
    addInstrumentButton.onClick = [this] { addInstrument(); };
    addBusButton.onClick = [this] { dispatcher.execute ("create_bus"); };
    rackPanel.addHeaderItem (addInstrumentButton, 112, true);
    mixerPanel.addHeaderItem (addBusButton, 64, true);

    // The piano roll.
    closePianoRollButton.onClick = [this] { closePianoRoll(); };
    quantizeButton.onClick = [this] { pianoRoll.quantize(); };
    quantizeButton.setTooltip ("Move the selected notes, or all of them, onto the snap grid (Q)");
    rollZoomOut.onClick = [this] { pianoRoll.zoomHorizontally (0.7); };
    rollZoomIn.onClick = [this] { pianoRoll.zoomHorizontally (1.0 / 0.7); };
    rollShorter.onClick = [this] { pianoRoll.zoomVertically (0.8); };
    rollTaller.onClick = [this] { pianoRoll.zoomVertically (1.25); };

    // Grid sizes in beats: quarter, eighth, sixteenth and thirty-second notes.
    snapBox.addItemList ({ "1/4", "1/8", "1/16", "1/32" }, 1);
    snapBox.setSelectedId (3, juce::dontSendNotification);
    snapBox.setTooltip ("Grid the piano roll snaps to (hold Alt to place freely)");
    snapBox.onChange = [this] { pianoRoll.setGrid (1.0 / std::pow (2.0, snapBox.getSelectedId() - 1)); };

    pianoRollPanel.addHeaderItem (pianoRollHint, 600);
    pianoRollPanel.addHeaderItem (closePianoRollButton, 24, true);
    pianoRollPanel.addHeaderItem (snapBox, 64, true);
    pianoRollPanel.addHeaderItem (snapLabel, 36, true);
    pianoRollPanel.addHeaderItem (quantizeButton, 72, true);
    pianoRollPanel.addHeaderItem (rollTaller, 24, true);
    pianoRollPanel.addHeaderItem (rollShorter, 24, true);
    pianoRollPanel.addHeaderItem (rollZoomIn, 24, true);
    pianoRollPanel.addHeaderItem (rollZoomOut, 24, true);
    pianoRollPanel.setActive (false);

    // The sample editor, for audio clips.
    closeSampleEditorButton.onClick = [this] { closeSampleEditor(); };
    sampleEditorHint.setFont (juce::FontOptions (12.0f));
    sampleEditorHint.setColour (juce::Label::textColourId, juce::Colours::grey);
    sampleEditorPanel.addHeaderItem (sampleEditorHint, 600);
    sampleEditorPanel.addHeaderItem (closeSampleEditorButton, 24, true);
    sampleEditorPanel.setActive (false);

    for (auto* panel : { &timelinePanel, &rackPanel, &mixerPanel, &pianoRollPanel, &sampleEditorPanel })
    {
        panel->onDetachChanged = [this] { resized(); };
        addChildComponent (panel);
    }

    // The bars between panels.
    browserSplitter.onDragStart = [this] { dragStartSize = browserWidth; };
    browserSplitter.onDrag = [this] (int delta)
    {
        browserWidth = juce::jlimit (160, std::max (160, getWidth() / 2), dragStartSize + delta);
        resized();
    };
    browserSplitter.onReset = [this] { browserWidth = 240; resized(); };

    timelineSplitter.onDragStart = [this] { dragStartProportion = timelineProportion; };
    timelineSplitter.onDrag = [this] (int delta)
    {
        timelineProportion = juce::jlimit (0.1, 0.9, dragStartProportion + delta / (double) std::max (1, centreArea.getHeight()));
        resized();
    };
    timelineSplitter.onReset = [this] { timelineProportion = 0.48; resized(); };

    rackSplitter.onDragStart = [this] { dragStartProportion = rackProportion; };
    rackSplitter.onDrag = [this] (int delta)
    {
        rackProportion = juce::jlimit (0.1, 0.9, dragStartProportion + delta / (double) std::max (1, bottomArea.getWidth()));
        resized();
    };
    rackSplitter.onReset = [this] { rackProportion = 0.5; resized(); };

    // The chat, down the right. Dragging its bar left makes it wider.
    chatSplitter.onDragStart = [this] { dragStartSize = chatWidth; };
    chatSplitter.onDrag = [this] (int delta)
    {
        chatWidth = juce::jlimit (260, std::max (260, getWidth() / 2), dragStartSize - delta);
        resized();
    };
    chatSplitter.onReset = [this] { chatWidth = 360; resized(); };

    for (auto* bar : { &browserSplitter, &chatSplitter, &timelineSplitter, &rackSplitter })
    {
        bar->onDragEnd = [this] { saveLayout(); };
        addChildComponent (bar);
    }

    addChildComponent (browserPanel);
    showBrowser (props.getBoolValue ("ngBrowserVisible", true));

    chatPanel.onOpenSettings = [this] { showPreferences ("AI"); };
    addChildComponent (chatPanel);
    showChat (props.getBoolValue ("ngChatVisible", false));
}

void MainComponent::restoreDetachedPanels()
{
    for (auto* panel : { &timelinePanel, &rackPanel, &mixerPanel, &pianoRollPanel, &sampleEditorPanel })
        if (getProperties().getBoolValue ("ngPanelDetached_" + panel->getId()))
            panel->setDetached (true);
}

void MainComponent::resetLayout()
{
    for (auto* panel : { &timelinePanel, &rackPanel, &mixerPanel, &pianoRollPanel, &sampleEditorPanel })
        panel->setDetached (false);

    browserWidth = 240;
    chatWidth = 360;
    timelineProportion = 0.48;
    rackProportion = 0.5;
    showBrowser (true);
    saveLayout();
    resized();
}

void MainComponent::saveLayout()
{
    auto& props = getProperties();
    props.setValue ("ngBrowserWidth", browserWidth);
    props.setValue ("ngChatWidth", chatWidth);
    props.setValue ("ngTimelineProportion", timelineProportion);
    props.setValue ("ngRackProportion", rackProportion);
}

//==============================================================================
juce::StringArray MainComponent::Menus::getMenuBarNames()
{
    return { "File", "Edit", "View", "Options", "Help" };
}

namespace
{
    juce::PopupMenu::Item menuItem (const juce::String& text, const juce::String& keys, std::function<void()> action,
                                    bool enabled = true, bool ticked = false)
    {
        juce::PopupMenu::Item item (text);
        item.shortcutKeyDescription = keys;
        item.action = std::move (action);
        item.isEnabled = enabled;
        item.isTicked = ticked;
        return item;
    }
}

juce::PopupMenu MainComponent::Menus::getMenuForIndex (int menuIndex, const juce::String&)
{
    return owner.getMenu (menuIndex);
}

juce::PopupMenu MainComponent::getMenu (int menuIndex)
{
    juce::PopupMenu menu;

    if (menuIndex == 0)
    {
        menu.addItem (menuItem ("New project", "Ctrl+N", [this] { newProject(); }));
        menu.addItem (menuItem ("Open...", "Ctrl+O", [this] { openProject(); }));
        menu.addItem (menuItem ("Save", "Ctrl+S", [this] { saveProject(); }));
        menu.addItem (menuItem ("Save as...", "Ctrl+Shift+S", [this] { saveProjectAs(); }));
        menu.addSeparator();
        menu.addItem (menuItem ("Import audio files...", "Ctrl+I", [this] { timeline.chooseAudioFiles ({}, timeline.getPlayheadBeatForImport()); }));
        menu.addItem (menuItem ("Export...", "Ctrl+E", [this] { showExport(); }));
        menu.addSeparator();
        menu.addItem (menuItem ("Quit", {}, [] { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }));
    }
    else if (menuIndex == 1)
    {
        menu.addItem (menuItem ("Undo", "Ctrl+Z", [this] { dispatcher.undo(); }, dispatcher.canUndo()));
        menu.addItem (menuItem ("Redo", "Ctrl+Y", [this] { dispatcher.redo(); }, dispatcher.canRedo()));
        menu.addItem (menuItem ("History...", "Ctrl+H", [this] { showHistory(); }));
        menu.addSeparator();
        menu.addItem (menuItem ("Add instrument", "Ctrl+T", [this] { addInstrument(); }));
        menu.addItem (menuItem ("Add audio channel", {}, [this] { dispatcher.execute ("create_track", makeObject ({ { "instrument", "audio" } })); }));
        menu.addItem (menuItem ("Add bus", {}, [this] { dispatcher.execute ("create_bus"); }));
    }
    else if (menuIndex == 2)
    {
        menu.addItem (menuItem ("Browser", "Ctrl+B", [this] { showBrowser (! browserPanel.isVisible()); }, true, browserPanel.isVisible()));
        menu.addItem (menuItem ("Typing keyboard", "Ctrl+K", [this] { setTypingKeyboard (! typingKeyboard::enabled); }, true, typingKeyboard::enabled));
        menu.addItem (menuItem ("Piano keyboard", "Ctrl+Shift+K", [this] { showKeyboard (! keyboard.isVisible()); }, true, keyboard.isVisible()));
        menu.addItem (menuItem ("AI chat", "Ctrl+J", [this] { showChat (! chatPanel.isVisible()); }, true, chatPanel.isVisible()));
        menu.addItem (menuItem ("Command console", {}, [this] { showConsole (! consoleShown); }, true, consoleShown));
        menu.addSeparator();

        for (auto* panel : { &timelinePanel, &rackPanel, &mixerPanel, &pianoRollPanel, &sampleEditorPanel })
        {
            auto name = panel == &timelinePanel ? "Timeline" : panel == &rackPanel ? "Channel rack" : panel == &mixerPanel ? "Mixer"
                          : panel == &pianoRollPanel ? "Piano roll" : "Sample editor";
            menu.addItem (menuItem (juce::String (name) + " in its own window", {}, [this, panel] { panel->setDetached (! panel->isDetached()); },
                                    true, panel->isDetached()));
        }

        menu.addSeparator();
        menu.addItem (menuItem ("Zoom the timeline to fit the song", "Ctrl + left click", [this] { timeline.zoomToFit(); }));
        menu.addItem (menuItem ("Reset the layout", {}, [this] { resetLayout(); }));
    }
    else if (menuIndex == 3)
    {
        menu.addItem (menuItem ("Settings...", "Ctrl+,", [this] { showPreferences(); }));
        menu.addItem (menuItem ("Plugins...", {}, [this] { showPluginList(); }));
    }
    else if (menuIndex == 4)
    {
        menu.addItem (menuItem ("Keyboard shortcuts", "F1", [this] { showShortcuts(); }));
        menu.addItem (menuItem ("About Ng DAW", {}, [this] { showAbout(); }));
    }

    return menu;
}

void MainComponent::showAbout()
{
    juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsOk (juce::MessageBoxIconType::InfoIcon, "Ng DAW",
                                                                         "Version " + juce::String (NG_VERSION)
                                                                           + "\n\nBuilt on Tracktion Engine and JUCE.", {}, this),
                                  nullptr);
}

//==============================================================================
void MainComponent::addInstrument()
{
    // A new channel with its linked strip and an empty pattern, ready to fill in.
    auto name = "Synth " + juce::String (session.getChannels().size() + 1);
    dispatcher.execute ("create_track", makeObject ({ { "name", name } }));
}

void MainComponent::showAudioMenu (juce::Component* target)
{
    juce::PopupMenu menu;
    menu.addItem ("Import audio files... (Ctrl+I)", [this]
    {
        timeline.chooseAudioFiles ({}, timeline.getPlayheadBeatForImport());
    });
    menu.addItem ("Empty audio channel", [this] { dispatcher.execute ("create_track", makeObject ({ { "instrument", "audio" } })); });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target));
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
    juce::File defaultProjectFolder (Session& session)
    {
        auto current = session.getFile();

        if (current.existsAsFile())
            return current.getParentDirectory();

        auto folder = session.getDefaultFolder();
        folder.createDirectory();
        return folder;
    }

    const juce::String projectWildcard = juce::String ("*") + projectFileExtension;
}

void MainComponent::openProject()
{
    confirmDiscardChanges ([this]
    {
        fileChooser = std::make_unique<juce::FileChooser> ("Open project", defaultProjectFolder (session), projectWildcard);
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

void MainComponent::openProjectFile (const juce::File& file)
{
    confirmDiscardChanges ([this, file]
    {
        auto result = dispatcher.execute ("open_project", makeObject ({ { "path", file.getFullPathName() },
                                                                        { "discard_changes", true } }));
        if (! result.ok)
            showError ("Couldn't open the project", result.error);
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
    auto folder = defaultProjectFolder (session);
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

void MainComponent::toggleRecording()
{
    if (session.getEdit().getTransport().isRecording())
    {
        dispatcher.execute ("stop");
        return;
    }

    auto result = dispatcher.execute ("record");

    if (! result.ok)
        showError ("Can't record yet", result.error.startsWith ("record: arm")
                                         ? "Arm a channel first: click its " + juce::String::fromUTF8 ("\xe2\x97\x8f")
                                             + " button in the channel rack. An instrument channel records MIDI; an audio channel "
                                               "records its audio input."
                                         : result.error.fromFirstOccurrenceOf ("record: ", false, false));
}

void MainComponent::showPreferences (const juce::String& tab)
{
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (new PreferencesPanel (session, [this] { lastAutosave = juce::Time::getCurrentTime(); }, tab));
    options.dialogTitle = "Settings";
    options.dialogBackgroundColour = getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId);
    options.componentToCentreAround = this;
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.escapeKeyTriggersCloseButton = true;
    options.launchAsync();
}

void MainComponent::showHistory()
{
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (new HistoryPanel (session, dispatcher));
    options.dialogTitle = "History";
    options.dialogBackgroundColour = getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId);
    options.componentToCentreAround = this;
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.escapeKeyTriggersCloseButton = true;
    options.launchAsync();
}

void MainComponent::checkForRecovery()
{
    auto found = Autosave::findRecoverable();

    if (found.empty())
        return;

    // The newest one is offered; any older ones are from crashes before it, and go.
    auto recovery = found.front();

    for (size_t i = 1; i < found.size(); ++i)
        Autosave::remove (found[i]);

    auto when = recovery.time.toString (true, true, false);
    auto options = juce::MessageBoxOptions()
                       .withIconType (juce::MessageBoxIconType::QuestionIcon)
                       .withTitle ("Recover unsaved changes?")
                       .withMessage ("Ng DAW didn't close properly last time. Recover the unsaved changes to '"
                                     + recovery.projectName + "' from " + when + "?\n\n"
                                     "Recovered changes open as an unsaved project, so you can look before you save.")
                       .withButton ("Recover")
                       .withButton ("Discard")
                       .withButton ("Later")
                       .withAssociatedComponent (this);

    juce::AlertWindow::showAsync (options, [safeThis = juce::Component::SafePointer (this), recovery] (int button)
    {
        if (safeThis == nullptr)
            return;

        if (button == 1)
        {
            if (auto error = Autosave::recover (safeThis->session, recovery); error.isNotEmpty())
                return safeThis->showError ("Couldn't recover the changes", error);

            safeThis->session.getUndoManager().clearUndoHistory();
            safeThis->dispatcher.execute ("get_state");
            safeThis->triggerAsyncUpdate();
        }
        else if (button == 2)
        {
            Autosave::remove (recovery);
        }
    });
}

void MainComponent::autosaveIfDue()
{
    auto minutes = settings::getAutosaveMinutes (session.getEngine());
    auto now = juce::Time::getCurrentTime();

    if (minutes <= 0 || now - lastAutosave < juce::RelativeTime::minutes (minutes))
        return;

    lastAutosave = now;

    // Writing a project mid-recording or mid-export could stall them; try again shortly.
    if (session.getEdit().getTransport().isRecording() || session.isExporting())
        return;

    autosave.saveIfNeeded();
}

void MainComponent::updateCpuMeter()
{
    auto& engine = session.getEngine();
    auto& dm = engine.getDeviceManager();
    auto xruns = dm.deviceManager.getXRunCount();
    auto dropouts = lastXRunCount < 0 ? 0 : std::max (0, xruns - lastXRunCount);
    lastXRunCount = xruns;

    auto cpu = juce::jlimit (0.0, 1.0, static_cast<double> (dm.getCpuUsage()));
    auto colour = dropouts > 0 || cpu > 0.8 ? juce::Colour (0xffe0533f)
                : cpu > 0.5                 ? juce::Colour (0xffe0b03f)
                                            : juce::Colours::lightgrey;

    cpuMeter.setText ("CPU " + juce::String (juce::roundToInt (cpu * 100.0)) + "%", juce::dontSendNotification);
    cpuMeter.setColour (juce::Label::textColourId, colour);

    juce::String tip = "How much of each audio buffer the engine needs to fill it.";

    if (auto* device = dm.deviceManager.getCurrentAudioDevice())
    {
        auto rate = device->getCurrentSampleRate();
        auto buffer = device->getCurrentBufferSizeSamples();
        auto latency = rate > 0 ? 1000.0 * (buffer + device->getOutputLatencyInSamples()) / rate : 0.0;

        tip << "\n" << device->getName() << ": " << juce::String (rate / 1000.0, 1) << " kHz, buffer " << buffer
            << " samples, output latency " << juce::String (latency, 1) << " ms.";
    }

    if (auto advice = getBufferAdvice (engine, dropouts); advice.isNotEmpty())
        tip << "\n" << advice;
    else
        tip << "\nA larger buffer gives the CPU more room; a smaller one makes playing live feel tighter.";

    cpuMeter.setTooltip (tip);
}

void MainComponent::showExport()
{
    if (session.getEdit().getTransport().isRecording())
        return showError ("Can't export yet", "Stop recording first.");

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (new ExportPanel (session, dispatcher));
    options.dialogTitle = "Export";
    options.dialogBackgroundColour = getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId);
    options.componentToCentreAround = this;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.escapeKeyTriggersCloseButton = true;
    options.launchAsync();
}

void MainComponent::showPluginList()
{
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (new PluginsPanel (session.getEngine()));
    options.dialogTitle = "Plugins";
    options.dialogBackgroundColour = getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId);
    options.componentToCentreAround = this;
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.escapeKeyTriggersCloseButton = true;
    options.launchAsync();
}

//==============================================================================
bool MainComponent::keyPressed (const juce::KeyPress& key, juce::Component*)
{
    return keyPressed (key);
}

bool MainComponent::keyStateChanged (bool isKeyDown, juce::Component*)
{
    return keyStateChanged (isKeyDown);
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    if (playTypedNote (key))
        return true;

    auto command = juce::ModifierKeys::commandModifier;
    auto shift = juce::ModifierKeys::shiftModifier;
    auto none = juce::ModifierKeys::noModifiers;

    if (key == juce::KeyPress ('n', command, 0))                    { newProject(); return true; }
    if (key == juce::KeyPress ('o', command, 0))                    { openProject(); return true; }
    if (key == juce::KeyPress ('s', command, 0))                    { saveProject(); return true; }
    if (key == juce::KeyPress ('s', command | shift, 0))            { saveProjectAs(); return true; }
    if (key == juce::KeyPress ('z', command, 0))                    { dispatcher.undo(); return true; }
    if (key == juce::KeyPress ('y', command, 0)
         || key == juce::KeyPress ('z', command | shift, 0))        { dispatcher.redo(); return true; }
    if (key == juce::KeyPress ('t', command, 0))                    { addInstrument(); return true; }
    if (key == juce::KeyPress ('i', command, 0))                    { timeline.chooseAudioFiles ({}, timeline.getPlayheadBeatForImport()); return true; }
    if (key == juce::KeyPress ('e', command, 0))                    { showExport(); return true; }
    if (key == juce::KeyPress ('h', command, 0))                    { showHistory(); return true; }
    if (key == juce::KeyPress (',', command, 0))                    { showPreferences(); return true; }
    if (key == juce::KeyPress ('b', command, 0))                    { showBrowser (! browserPanel.isVisible()); return true; }
    if (key == juce::KeyPress ('j', command, 0))                    { showChat (! chatPanel.isVisible()); return true; }
    if (key == juce::KeyPress ('k', command, 0))                    { setTypingKeyboard (! typingKeyboard::enabled); return true; }
    if (key == juce::KeyPress ('k', command | shift, 0))            { showKeyboard (! keyboard.isVisible()); return true; }

    if (key == juce::KeyPress (juce::KeyPress::spaceKey))           { playButton.onClick(); return true; }
    if (key == juce::KeyPress (juce::KeyPress::homeKey))            { returnButton.onClick(); return true; }
    if (key == juce::KeyPress ('r', none, 0))                       { toggleRecording(); return true; }
    if (key == juce::KeyPress ('c', none, 0))                       { dispatcher.execute ("set_metronome"); return true; }

    if (key == juce::KeyPress ('l', command, 0))
    {
        dispatcher.execute ("set_loop", makeObject ({ { "enabled", ! loopButton.getToggleState() } }));
        return true;
    }

    if (key == juce::KeyPress (juce::KeyPress::F1Key) || key.getTextCharacter() == '?')
    {
        showShortcuts();
        return true;
    }

    // The piano roll uses Escape to clear its selection first; with nothing selected, it closes.
    if (key == juce::KeyPress (juce::KeyPress::escapeKey) && pianoRoll.getClip().isNotEmpty())
    {
        closePianoRoll();
        timeline.grabKeyboardFocus();
        return true;
    }

    if (key == juce::KeyPress (juce::KeyPress::escapeKey) && sampleEditor.getClip().isNotEmpty())
    {
        closeSampleEditor();
        timeline.grabKeyboardFocus();
        return true;
    }

    return false;
}

bool MainComponent::playTypedNote (const juce::KeyPress& key)
{
    if (! typingKeyboard::isNoteKey (key))
        return false;

    // Text being typed somewhere isn't music.
    auto* focused = juce::Component::getCurrentlyFocusedComponent();

    if (dynamic_cast<juce::TextEditor*> (focused) != nullptr)
        return false;

    auto keyCode = typingKeyboard::getKeyCode (key);

    if (typedNotes.count (keyCode) > 0)
        return true;   // the key repeating while held

    // Something has to be listening: arm the first instrument channel if nothing is.
    if (session.getArmedChannel() == nullptr)
        for (auto* channel : session.getChannels())
            if (! Session::isAudioChannel (*channel))
            {
                dispatcher.execute ("arm_channel", makeObject ({ { "channel", channel->itemID.toString() } }));
                break;
            }

    auto note = juce::jlimit (0, 127, typingKeyboard::baseNote + typingKeyboard::getOffset (keyCode));
    typedNotes[keyCode] = note;
    keyboardState.noteOn (1, note, 0.8f);
    return true;
}

bool MainComponent::keyStateChanged (bool)
{
    releaseTypedNotes (false);
    return false;
}

void MainComponent::releaseTypedNotes (bool all)
{
    for (auto it = typedNotes.begin(); it != typedNotes.end();)
    {
        if (all || ! juce::KeyPress::isKeyCurrentlyDown (it->first))
        {
            keyboardState.noteOff (1, it->second, 0.0f);
            it = typedNotes.erase (it);
        }
        else
        {
            ++it;
        }
    }
}

void MainComponent::setTypingKeyboard (bool shouldBeOn)
{
    releaseTypedNotes (true);
    typingKeyboard::enabled = shouldBeOn;
    typingKeysButton.setToggleState (shouldBeOn, juce::dontSendNotification);
    getProperties().setValue ("ngTypingKeyboard", shouldBeOn);
}

void MainComponent::showShortcuts()
{
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (new ShortcutsList());
    options.dialogTitle = "Keyboard shortcuts";
    options.dialogBackgroundColour = getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId);
    options.componentToCentreAround = this;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.launchAsync();
}

bool MainComponent::ownsComponent (juce::Component* c) const
{
    if (c == nullptr)
        return false;

    if (isParentOf (c))
        return true;

    for (auto* panel : { &timelinePanel, &rackPanel, &mixerPanel, &pianoRollPanel, &sampleEditorPanel })
        if (panel->isParentOf (c))
            return true;

    return false;
}

void MainComponent::globalFocusChanged (juce::Component* focused)
{
    // Buttons and drop-downs keep the keyboard focus after they're clicked, and would then
    // take Space, Return and the arrow keys. Give it back to the panel being worked in.
    // If nothing has the focus (e.g. a text box just closed), keys would go nowhere.
    if (focused == nullptr)
    {
        releaseTypedNotes (true);

        if (isShowing() && juce::Process::isForegroundProcess())
            juce::MessageManager::callAsync ([safeThis = juce::Component::SafePointer (this)]
            {
                if (safeThis != nullptr && juce::Component::getCurrentlyFocusedComponent() == nullptr)
                    safeThis->restoreFocus();
            });

        return;
    }

    if (! ownsComponent (focused))
        return;

    if (focused == &timeline || focused == &pianoRoll)
        lastFocused = focused;
    else if (dynamic_cast<juce::Button*> (focused) != nullptr || dynamic_cast<juce::ComboBox*> (focused) != nullptr)
        juce::MessageManager::callAsync ([safeThis = juce::Component::SafePointer (this)]
        {
            if (safeThis != nullptr)
                safeThis->restoreFocus();
        });
}

void MainComponent::restoreFocus()
{
    if (lastFocused != nullptr && lastFocused->isShowing())
        lastFocused->grabKeyboardFocus();
    else
        grabKeyboardFocus();
}

void MainComponent::runConsoleCommand()
{
    CommandDispatcher::ScopedSource source (dispatcher, "Console");
    consoleOutput.setText (dispatcher.executeJson (consoleInput.getText()));
}

void MainComponent::showConsole (bool shouldShow)
{
    consoleShown = shouldShow;
    consoleButton.setToggleState (shouldShow, juce::dontSendNotification);

    for (auto* c : std::initializer_list<juce::Component*> { &consoleHeader, &consoleInput, &consoleOutput, &runButton, &commandLog })
        c->setVisible (shouldShow);

    resized();
}

void MainComponent::showBrowser (bool shouldShow)
{
    browserButton.setToggleState (shouldShow, juce::dontSendNotification);
    browserPanel.setVisible (shouldShow);
    getProperties().setValue ("ngBrowserVisible", shouldShow);
    resized();
}

void MainComponent::showChat (bool shouldShow)
{
    chatButton.setToggleState (shouldShow, juce::dontSendNotification);
    chatPanel.setVisible (shouldShow);
    getProperties().setValue ("ngChatVisible", shouldShow);
    resized();

    if (shouldShow)
        chatPanel.focusInput();
}

void MainComponent::showKeyboard (bool shouldShow)
{
    keyboard.setVisible (shouldShow);
    pianoKeysButton.setToggleState (shouldShow, juce::dontSendNotification);

    // Something has to be listening: arm the first channel if none is.
    if (shouldShow && session.getArmedChannel() == nullptr && ! session.getChannels().isEmpty())
        dispatcher.execute ("arm_channel", makeObject ({ { "channel", session.getChannels().getFirst()->itemID.toString() } }));

    resized();
}

void MainComponent::handleNoteOn (juce::MidiKeyboardState*, int midiChannel, int note, float velocity)
{
    session.sendLiveMidi (juce::MidiMessage::noteOn (midiChannel, note, velocity));
}

void MainComponent::handleNoteOff (juce::MidiKeyboardState*, int midiChannel, int note, float velocity)
{
    session.sendLiveMidi (juce::MidiMessage::noteOff (midiChannel, note, velocity));
}

void MainComponent::updatePositionDisplay()
{
    auto text = session.getPositionText();

    if (positionDisplay.getText() != text)
        positionDisplay.setText (text, juce::dontSendNotification);
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
    metronomeButton.setToggleState (state["transport"]["metronome"], juce::dontSendNotification);
    updatePositionDisplay();
}

//==============================================================================
void MainComponent::handleAsyncUpdate()
{
    if (std::exchange (browserNeedsRefresh, false))
        browserPanel.refresh();

    refreshViews();
    updateButtons();
    updateWindowTitle();
}

void MainComponent::timerCallback()
{
    if (++cpuMeterTicks >= 15)
    {
        cpuMeterTicks = 0;
        updateCpuMeter();
        autosaveIfDue();

        if (chatPanel.isVisible())
            chatPanel.updateProvider();   // e.g. after a key was set in Settings
    }

    // A key let go while another window had the focus never says so; check.
    if (! typedNotes.empty())
        releaseTypedNotes (false);

    updateButtons();
    updatePlayStep();
    updatePositionDisplay();
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

    rack.setPlayStep (step);
    sampleEditor.updatePlayhead();
}

void MainComponent::updateButtons()
{
    auto playing = session.getEdit().getTransport().isPlaying();

    if (playButton.getToggleState() != playing)
    {
        playButton.setToggleState (playing, juce::dontSendNotification);
        playButton.setIcon (playing ? icons::Icon::stop : icons::Icon::play);
    }

    recordButton.setToggleState (session.getEdit().getTransport().isRecording(), juce::dontSendNotification);
    undoButton.setEnabled (dispatcher.canUndo());
    redoButton.setEnabled (dispatcher.canRedo());
}

void MainComponent::refreshViews()
{
    auto state = session.toVar();

    rack.update (state);
    mixer.update (state);
    timeline.update (state);

    // The clip may have been deleted (or undone away) while open.
    if (pianoRoll.getClip().isNotEmpty() && ! pianoRoll.update())
        closePianoRoll();

    if (sampleEditor.getClip().isNotEmpty() && ! sampleEditor.update())
        closeSampleEditor();

    updatePianoRollTitle();
    updateSampleEditorTitle();
    updateTransportControls (state);
}

void MainComponent::openPianoRoll (const juce::String& clipId)
{
    // One editor at a time takes the rack and mixer's place.
    sampleEditor.clear();
    sampleEditorPanel.setActive (false);

    pianoRoll.setClip (clipId);
    pianoRollPanel.setActive (true);
    updatePianoRollTitle();
    resized();
    pianoRollPanel.toFront();
    pianoRoll.grabKeyboardFocus();

    // With the typing keyboard on, the keys play the clip's channel, as in FL Studio.
    if (typingKeyboard::enabled)
        if (auto clip = session.findClip (clipId))
            if (auto track = dynamic_cast<te::AudioTrack*> (clip->getTrack()); track != nullptr && session.getArmedChannel() != track)
                dispatcher.execute ("arm_channel", makeObject ({ { "channel", track->itemID.toString() } }));
}

void MainComponent::closePianoRoll()
{
    pianoRoll.clear();
    pianoRollPanel.setActive (false);
    resized();
}

void MainComponent::openSampleEditor (const juce::String& clipId)
{
    pianoRoll.clear();
    pianoRollPanel.setActive (false);

    sampleEditor.setClip (clipId);
    sampleEditorPanel.setActive (true);
    updateSampleEditorTitle();
    resized();
    sampleEditorPanel.toFront();
}

void MainComponent::closeSampleEditor()
{
    sampleEditor.clear();
    sampleEditorPanel.setActive (false);
    resized();
}

void MainComponent::updateSampleEditorTitle()
{
    auto clip = session.findClip (sampleEditor.getClip());

    if (clip == nullptr)
        return;

    auto title = "Sample editor: " + clip->getName();

    if (auto track = clip->getTrack())
        title << " on " << track->getName();

    sampleEditorPanel.setTitle (title);
}

void MainComponent::updatePianoRollTitle()
{
    auto clip = session.findClip (pianoRoll.getClip());

    if (clip == nullptr)
        return;

    auto title = "Piano roll: " + clip->getName();

    if (auto track = clip->getTrack())
        title << " on " << track->getName();

    pianoRollPanel.setTitle (title);
}

//==============================================================================
void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId).darker (0.25f));

    // The top bar.
    g.setColour (theme::panelHeader);
    g.fillRect (getLocalBounds().removeFromTop (topBarHeight));
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.fillRect (0, topBarHeight - 1, getWidth(), 1);

    if (centreArea.isEmpty())
        return;

    auto anyAttached = (! timelinePanel.isDetached()) || (! rackPanel.isDetached()) || (! mixerPanel.isDetached())
                         || (pianoRollPanel.isActive() && ! pianoRollPanel.isDetached())
                         || (sampleEditorPanel.isActive() && ! sampleEditorPanel.isDetached());

    if (! anyAttached)
    {
        g.setColour (juce::Colours::grey);
        g.setFont (juce::FontOptions (15.0f));
        g.drawFittedText ("Every panel is in its own window. Attach one with its button, or from the View menu.",
                          centreArea.reduced (40), juce::Justification::centred, 2);
    }
}

void MainComponent::resized()
{
    auto r = getLocalBounds();
    layOutTopBar (r.removeFromTop (topBarHeight).reduced (8, 6));
    r = r.reduced (8);

    if (keyboard.isVisible())
    {
        keyboard.setBounds (r.removeFromBottom (70));

        // Spread the 50 white keys from C0 to C7 across the window.
        keyboard.setKeyWidth ((float) keyboard.getWidth() / 50.0f);
        r.removeFromBottom (8);
    }

    if (consoleShown)
    {
        auto console = r.removeFromBottom (170);
        commandLog.setBounds (console.removeFromRight (juce::jlimit (300, 520, console.getWidth() / 3)));
        console.removeFromRight (10);
        consoleHeader.setBounds (console.removeFromTop (24));
        auto consoleTop = console.removeFromTop (60);
        runButton.setBounds (consoleTop.removeFromRight (70).reduced (4, 14));
        consoleInput.setBounds (consoleTop);
        console.removeFromTop (6);
        consoleOutput.setBounds (console);
        r.removeFromBottom (8);
    }

    // The browser runs down the left, beside the panels.
    browserSplitter.setVisible (browserPanel.isVisible());

    if (browserPanel.isVisible())
    {
        browserWidth = juce::jlimit (160, std::max (160, getWidth() / 2), browserWidth);
        browserPanel.setBounds (r.removeFromLeft (browserWidth));
        browserSplitter.setBounds (r.removeFromLeft (8));
    }

    // The chat runs down the right.
    chatSplitter.setVisible (chatPanel.isVisible());

    if (chatPanel.isVisible())
    {
        chatWidth = juce::jlimit (260, std::max (260, getWidth() / 2), chatWidth);
        chatPanel.setBounds (r.removeFromRight (chatWidth));
        chatSplitter.setBounds (r.removeFromRight (8));
    }

    layOutPanels (r);
    repaint();
}

void MainComponent::layOutTopBar (juce::Rectangle<int> r)
{
    auto h = r.getHeight();
    auto gap = [&r] (int width) { r.removeFromLeft (width); };
    auto place = [&r, h] (juce::Component& c, int width) { c.setBounds (r.removeFromLeft (width).withSizeKeepingCentre (width, h)); };

    // Menus, then the file buttons (left out when the window is narrow; the File menu has them).
    menuBar.setBounds (r.removeFromLeft (300));
    gap (6);

    auto wide = getWidth() >= 1320;

    for (auto* b : { &newButton, &openButton, &saveButton })
    {
        b->setVisible (wide);

        if (wide)
        {
            place (*b, h);
            gap (2);
        }
    }

    if (wide)
        gap (10);

    place (undoButton, h);
    gap (2);
    place (redoButton, h);
    gap (14);

    // The transport.
    place (returnButton, h + 4);
    gap (3);
    place (playButton, h + 14);
    gap (3);
    place (recordButton, h + 4);
    gap (12);
    place (tempo, 118);
    gap (6);
    place (timeSignature, 64);
    gap (8);
    place (positionDisplay, 92);
    gap (8);
    place (loopButton, h);
    gap (2);
    place (metronomeButton, h);
    gap (12);
    place (typingKeysButton, h + 6);
    gap (2);
    place (pianoKeysButton, h + 6);

    // Views and dialogs from the right. (The space before the CPU meter is free for more.)
    for (auto* b : { &helpButton, &settingsButton, &exportButton, &historyButton, &consoleButton, &chatButton, &browserButton })
    {
        b->setBounds (r.removeFromRight (h));
        r.removeFromRight (2);
    }

    r.removeFromRight (8);
    cpuMeter.setBounds (r.removeFromRight (70).withSizeKeepingCentre (70, std::min (h, 24)));
}

void MainComponent::layOutPanels (juce::Rectangle<int> r)
{
    centreArea = r;
    bottomArea = {};

    // What's in the main window: the attached panels. The piano roll or the sample editor,
    // while open, takes the rack and mixer's place.
    auto* editorPanel = sampleEditorPanel.isActive() ? &sampleEditorPanel : &pianoRollPanel;
    auto rollHere = editorPanel->isActive() && ! editorPanel->isDetached();
    auto timelineHere = ! timelinePanel.isDetached();
    auto rackHere = ! rackPanel.isDetached() && ! rollHere;
    auto mixerHere = ! mixerPanel.isDetached() && ! rollHere;
    auto bottomHere = rollHere || rackHere || mixerHere;

    timelinePanel.setVisible (timelineHere || timelinePanel.isDetached());
    rackPanel.setVisible (rackHere || rackPanel.isDetached());
    mixerPanel.setVisible (mixerHere || mixerPanel.isDetached());
    pianoRollPanel.setVisible (pianoRollPanel.isActive());
    sampleEditorPanel.setVisible (sampleEditorPanel.isActive());
    timelineSplitter.setVisible (timelineHere && bottomHere);
    rackSplitter.setVisible (rackHere && mixerHere);

    if (timelineHere)
    {
        if (bottomHere)
        {
            auto height = juce::jlimit (120, std::max (120, r.getHeight() - 160), juce::roundToInt ((r.getHeight() - 8) * timelineProportion));
            timelinePanel.setBounds (r.removeFromTop (height));
            timelineSplitter.setBounds (r.removeFromTop (8));
        }
        else
        {
            timelinePanel.setBounds (r);
            return;
        }
    }

    bottomArea = r;

    if (rollHere)
    {
        editorPanel->setBounds (r);
        return;
    }

    if (rackHere && mixerHere)
    {
        auto width = juce::jlimit (300, std::max (300, r.getWidth() - 200), juce::roundToInt ((r.getWidth() - 8) * rackProportion));
        rackPanel.setBounds (r.removeFromLeft (width));
        rackSplitter.setBounds (r.removeFromLeft (8));
        mixerPanel.setBounds (r);
    }
    else if (rackHere)
    {
        rackPanel.setBounds (r);
    }
    else if (mixerHere)
    {
        mixerPanel.setBounds (r);
    }
}

} // namespace ng
