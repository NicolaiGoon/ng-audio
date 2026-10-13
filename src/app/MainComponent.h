#pragma once

#include "MixerView.h"
#include "PianoRoll.h"
#include "TimelineView.h"

namespace ng
{

/**
    The main window: a transport, the arrangement timeline, a channel rack, a mixer and
    a command console (hidden until you tick Console). Opening a clip swaps the rack and
    mixer for the piano roll until it is closed.
    Every button here calls the command layer rather than touching the Edit directly,
    so the UI does exactly what an AI client or macro could do.

    The shortcuts that work everywhere live in keyPressed(); the timeline and piano roll
    handle their own and pass on the rest. The ? button lists them all.
*/
class MainComponent  : public juce::Component,
                       private juce::Timer,
                       private juce::AsyncUpdater,
                       private juce::MidiKeyboardState::Listener,
                       private juce::FocusChangeListener
{
public:
    explicit MainComponent (te::Engine&);
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    /** If there are unsaved changes, asks whether to save them first. Calls onContinue
        once they're saved or the user chose not to save; doesn't call it on Cancel. */
    void confirmDiscardChanges (std::function<void()> onContinue);

private:
    void timerCallback() override;
    void handleAsyncUpdate() override;
    void handleNoteOn (juce::MidiKeyboardState*, int midiChannel, int note, float velocity) override;
    void handleNoteOff (juce::MidiKeyboardState*, int midiChannel, int note, float velocity) override;
    void toggleRecording();
    void showKeyboard (bool);
    void showShortcuts();
    void globalFocusChanged (juce::Component*) override;
    void restoreFocus();

    void addInstrument();
    void loadStarterProject();
    void newProject();
    void openProject();
    void saveProject (std::function<void()> onSaved = {});
    void saveProjectAs (std::function<void()> onSaved = {});
    void updateWindowTitle();
    void showError (const juce::String& title, const juce::String& message);
    void runConsoleCommand();
    void rebuildRows();
    void updateButtons();
    void updatePlayStep();
    void updatePositionDisplay();
    void showAudioSettings();
    void showPluginList();
    void setUpTransportControls();
    void updateTransportControls (const juce::var& state);
    void openPianoRoll (const juce::String& clipId);
    void closePianoRoll();
    void updatePianoRollTitle();

    Session session;
    CommandRegistry registry;
    CommandDispatcher dispatcher { session, registry };

    juce::TextButton newButton { "New" }, openButton { "Open" }, saveButton { "Save" }, saveAsButton { "Save as" },
                     addButton { "Add instrument" }, playButton { "Play" }, returnButton { "|<" }, recordButton { "Rec" },
                     undoButton { "Undo" }, redoButton { "Redo" }, settingsButton { "Audio" }, pluginsButton { "Plugins" }, shortcutsButton { "?" },
                     runButton { "Run" }, closePianoRollButton { "Close" },
                     quantizeButton { "Quantize" };
    juce::ToggleButton consoleButton { "Console" }, keysButton { "Keys" };
    juce::MidiKeyboardState keyboardState;
    juce::MidiKeyboardComponent keyboard { keyboardState, juce::KeyboardComponentBase::horizontalKeyboard };

    juce::Slider tempo;
    juce::ComboBox timeSignature, snapBox;
    juce::ToggleButton loopButton { "Loop" }, metronomeButton { "Metronome" };
    juce::Label positionDisplay;

    juce::Label timelineHint { {}, "Drag clips to move them and their edges to trim. Double-click empty space for a new clip, "
                                   "or a clip to edit its notes. Del deletes, Ctrl+D duplicates, S splits." },
                pianoRollHint { {}, "Click to draw, drag to move, drag the right edge to resize. Right-click or Del deletes. "
                                    "Ctrl+drag selects. Drag in Velocity to set it." },
                pianoRollHeader { {}, "Piano roll" }, snapLabel { {}, "Snap" },
                timelineHeader { {}, "Timeline" }, channelsHeader { {}, "Channel rack" }, mixerHeader { {}, "Mixer" },
                consoleHeader { {}, "Command console (JSON)" };

    juce::TextEditor consoleInput, consoleOutput;

    juce::OwnedArray<juce::Component> channelRows;
    juce::String channelRackKey;
    int playStep = -1;
    TimelineView timeline { session, dispatcher };
    MixerView mixer { session, dispatcher };
    PianoRoll pianoRoll { session, dispatcher };
    juce::TooltipWindow tooltips { this };
    std::unique_ptr<juce::FileChooser> fileChooser;

    /** The panel keys went to last (timeline, piano roll or keyboard), for restoreFocus(). */
    juce::Component::SafePointer<juce::Component> lastFocused;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace ng
