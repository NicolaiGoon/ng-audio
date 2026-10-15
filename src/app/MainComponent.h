#pragma once

#include "BrowserPanel.h"
#include "ChannelRack.h"
#include "CommandLogPanel.h"
#include "core/Autosave.h"
#include "core/Bridge.h"
#include "DockPanel.h"
#include "HistoryPanel.h"
#include "MixerView.h"
#include "PianoRoll.h"
#include "PreferencesPanel.h"
#include "TimelineView.h"

namespace ng
{

/**
    The main window: a top bar (menus, file buttons, the transport and view switches), the
    browser down the left, and the timeline over the channel rack and mixer. Opening a clip
    shows the piano roll in place of the rack and mixer until it's closed.

    The timeline, rack, mixer and piano roll are panels that can each be detached into a
    window of their own (and attached again); the bars between panels can be dragged to give
    each more or less room. The layout is remembered.

    Every button here calls the command layer rather than touching the Edit directly,
    so the UI does exactly what an AI client or macro could do.

    The shortcuts that work everywhere live in keyPressed() (detached windows send their keys
    here too); the timeline and piano roll handle their own and pass on the rest. With the
    typing keyboard on, letter keys play the armed channel instead, as in FL Studio.
*/
class MainComponent  : public juce::Component,
                       public juce::DragAndDropContainer,
                       public juce::KeyListener,
                       private juce::Timer,
                       private juce::AsyncUpdater,
                       private juce::MidiKeyboardState::Listener,
                       private juce::FocusChangeListener,
                       private juce::ChangeListener
{
public:
    explicit MainComponent (te::Engine&);
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;

    // Keys from detached panel windows.
    bool keyPressed (const juce::KeyPress&, juce::Component*) override;
    bool keyStateChanged (bool isKeyDown, juce::Component*) override;


    /** If there are unsaved changes, asks whether to save them first. Calls onContinue
        once they're saved or the user chose not to save; doesn't call it on Cancel. */
    void confirmDiscardChanges (std::function<void()> onContinue);

    /** Opens a project file, e.g. one double-clicked in Explorer (asks about unsaved changes first). */
    void openProjectFile (const juce::File&);

private:
    /** The menus in the top bar. (Not MainComponent itself: a menu bar model is an AsyncUpdater too.) */
    struct Menus  : public juce::MenuBarModel
    {
        explicit Menus (MainComponent& o) : owner (o) {}

        juce::StringArray getMenuBarNames() override;
        juce::PopupMenu getMenuForIndex (int menuIndex, const juce::String& menuName) override;
        void menuItemSelected (int, int) override {}

        MainComponent& owner;
    };

    juce::PopupMenu getMenu (int menuIndex);

    void timerCallback() override;
    void handleAsyncUpdate() override;
    void handleNoteOn (juce::MidiKeyboardState*, int midiChannel, int note, float velocity) override;
    void handleNoteOff (juce::MidiKeyboardState*, int midiChannel, int note, float velocity) override;
    void toggleRecording();
    void showKeyboard (bool);
    void setTypingKeyboard (bool);
    bool playTypedNote (const juce::KeyPress&);
    void releaseTypedNotes (bool all);
    void showShortcuts();
    void showAbout();
    void globalFocusChanged (juce::Component*) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override     { triggerAsyncUpdate(); }
    void restoreFocus();
    bool ownsComponent (juce::Component*) const;

    void addInstrument();
    void showAudioMenu (juce::Component* target);
    void loadStarterProject();
    void newProject();
    void openProject();
    void saveProject (std::function<void()> onSaved = {});
    void saveProjectAs (std::function<void()> onSaved = {});
    void updateWindowTitle();
    void showError (const juce::String& title, const juce::String& message);
    void runConsoleCommand();
    void showConsole (bool);
    void refreshViews();
    void updateButtons();
    void updatePlayStep();
    void updatePositionDisplay();
    void showPreferences();
    void showHistory();
    void checkForRecovery();
    void updateCpuMeter();
    void autosaveIfDue();
    void showPluginList();
    void showExport();
    void showBrowser (bool);
    void setUpTopBar();
    void setUpPanels();
    void restoreDetachedPanels();
    void resetLayout();
    void saveLayout();
    void updateTransportControls (const juce::var& state);
    void openPianoRoll (const juce::String& clipId);
    void closePianoRoll();
    void updatePianoRollTitle();
    void layOutTopBar (juce::Rectangle<int>);
    void layOutPanels (juce::Rectangle<int>);

    static constexpr int topBarHeight = 46;

    juce::PropertiesFile& getProperties()   { return session.getEngine().getPropertyStorage().getPropertiesFile(); }

    Session session;
    CommandRegistry registry;
    CommandDispatcher dispatcher { session, registry };

    /** Keeps a copy of unsaved changes, for recovery after a crash. */
    Autosave autosave { session };
    juce::Time lastAutosave = juce::Time::getCurrentTime();

    //==============================================================================
    // The top bar.
    Menus menus { *this };
    juce::MenuBarComponent menuBar { &menus };
    IconButton newButton { "New project (Ctrl+N)", icons::Icon::newFile },
               openButton { "Open a project (Ctrl+O)", icons::Icon::open },
               saveButton { "Save (Ctrl+S)", icons::Icon::save },
               undoButton { "Undo (Ctrl+Z)", icons::Icon::undo },
               redoButton { "Redo (Ctrl+Y or Ctrl+Shift+Z)", icons::Icon::redo },
               returnButton { "Back to the start (Home)", icons::Icon::toStart },
               playButton { "Play or stop (Space)", icons::Icon::play },
               recordButton { "Record into the armed channels from the playhead (MIDI or audio); click again (or Stop) to finish (R)",
                              icons::Icon::record },
               loopButton { "Loop playback: the loop region, or the whole project if none is drawn (Ctrl+L)", icons::Icon::loop },
               metronomeButton { "Metronome: a click on every beat while playing, louder on the bar (C)", icons::Icon::metronome },
               typingKeysButton { "Typing keyboard: play the armed channel from the computer keyboard, as in FL Studio "
                                  "(Z S X D C... and Q 2 W 3 E...). While it's on, those keys play notes instead of their shortcuts (Ctrl+K)",
                                  icons::Icon::typingKeys },
               pianoKeysButton { "Show a piano keyboard to play the armed channel with the mouse (Ctrl+Shift+K)", icons::Icon::piano },
               browserButton { "Show or hide the browser: instruments, effects, presets and samples to drag in (Ctrl+B)", icons::Icon::browser },
               consoleButton { "Show the JSON command console and the log of what AI clients did", icons::Icon::console },
               historyButton { "The undo history: click a step to go back to it (Ctrl+H)", icons::Icon::history },
               exportButton { "Export the song or the loop region as a WAV or MP3 file, or as stems (Ctrl+E)", icons::Icon::exportAudio },
               settingsButton { "Settings: audio and MIDI devices, buffer size, theme, project folder and autosave (Ctrl+,)", icons::Icon::settings },
               helpButton { "Keyboard shortcuts (F1)", icons::Icon::help };

    juce::Slider tempo;
    juce::ComboBox timeSignature, snapBox;
    juce::Label positionDisplay;

    /** How hard the audio engine is working, with advice when it struggles. */
    juce::Label cpuMeter;
    int cpuMeterTicks = 0, lastXRunCount = -1;

    //==============================================================================
    // Panel contents, and the buttons in their headers.
    TimelineView timeline { session, dispatcher };
    ChannelRack rack { session, dispatcher };
    MixerView mixer { session, dispatcher };
    PianoRoll pianoRoll { session, dispatcher };
    BrowserPanel browserPanel { session, dispatcher };
    CommandLogPanel commandLog { dispatcher };

    IconButton addAudioButton { "Import audio files onto new audio channels at the playhead (Ctrl+I), or add an empty audio channel",
                                icons::Icon::audio, "Audio" },
               addInstrumentButton { "Add an instrument channel (Ctrl+T)", icons::Icon::plus, "Instrument" },
               addBusButton { "Add a bus: a strip other strips can send to, e.g. for a shared reverb", icons::Icon::plus, "Bus" },
               timelineZoomOut { "Zoom out in time (Ctrl + wheel)", icons::Icon::zoomOut },
               timelineZoomIn { "Zoom in in time (Ctrl + wheel)", icons::Icon::zoomIn },
               timelineShorter { "Shorter lanes (Alt + wheel)", icons::Icon::zoomOutVertical },
               timelineTaller { "Taller lanes (Alt + wheel); drag a lane's bottom edge to size just that one", icons::Icon::zoomInVertical },
               rollZoomOut { "Zoom out in time (Ctrl + wheel)", icons::Icon::zoomOut },
               rollZoomIn { "Zoom in in time (Ctrl + wheel)", icons::Icon::zoomIn },
               rollShorter { "Smaller rows (Alt + wheel)", icons::Icon::zoomOutVertical },
               rollTaller { "Bigger rows (Alt + wheel)", icons::Icon::zoomInVertical },
               closePianoRollButton { "Close the piano roll (Esc)", icons::Icon::close };
    juce::TextButton quantizeButton { "Quantize" };
    juce::Label timelineHint { {}, "Click a clip, then click its lane to paint copies. Right-click deletes. Drag in the ruler for a loop." },
                pianoRollHint { {}, "Click or drag to draw notes, drag a note to move it, its right edge to resize. Right-click or right-drag deletes." },
                snapLabel { {}, "Snap" };

    //==============================================================================
    // The panels and the bars between them. They come after their contents, so they go first.
    DockPanel timelinePanel { "timeline", "Timeline", icons::Icon::timeline, timeline, getProperties(), this },
              rackPanel { "rack", "Channel rack", icons::Icon::rack, rack, getProperties(), this },
              mixerPanel { "mixer", "Mixer", icons::Icon::mixer, mixer, getProperties(), this },
              pianoRollPanel { "pianoroll", "Piano roll", icons::Icon::piano, pianoRoll, getProperties(), this };
    SplitterBar browserSplitter { true }, timelineSplitter { false }, rackSplitter { true };

    int browserWidth = 240;
    double timelineProportion = 0.48, rackProportion = 0.5;
    int dragStartSize = 0;
    double dragStartProportion = 0.0;
    juce::Rectangle<int> centreArea, bottomArea;

    //==============================================================================
    juce::MidiKeyboardState keyboardState;
    juce::MidiKeyboardComponent keyboard { keyboardState, juce::KeyboardComponentBase::horizontalKeyboard };

    /** Notes the typing keyboard is holding, by the key playing them. */
    std::map<int, int> typedNotes;

    juce::Label consoleHeader { {}, "Command console (JSON)" };
    juce::TextEditor consoleInput, consoleOutput;
    juce::TextButton runButton { "Run" };
    bool consoleShown = false;

    /** Lets the MCP server (and so Claude) run commands here. */
    BridgeServer bridge { dispatcher };
    bool commandLogShown = false;

    bool browserNeedsRefresh = false;
    juce::TooltipWindow tooltips { this, 600 };
    std::unique_ptr<juce::FileChooser> fileChooser;

    /** The panel keys went to last (timeline or piano roll), for restoreFocus(). */
    juce::Component::SafePointer<juce::Component> lastFocused;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace ng
