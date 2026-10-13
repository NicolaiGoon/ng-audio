#pragma once

#include "MixerView.h"

namespace ng
{

/**
    The main window: a channel rack, a mixer, a transport and a command console.
    Every button here calls the command layer rather than touching the Edit directly,
    so the UI does exactly what an AI client or macro could do.
*/
class MainComponent  : public juce::Component,
                       private juce::Timer,
                       private juce::AsyncUpdater
{
public:
    explicit MainComponent (te::Engine&);
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void handleAsyncUpdate() override;

    void addInstrument();
    void runConsoleCommand();
    void rebuildRows();
    void updateButtons();
    void showAudioSettings();

    Session session;
    CommandRegistry registry;
    CommandDispatcher dispatcher { session, registry };

    juce::TextButton addButton { "Add instrument" }, playButton { "Play" },
                     undoButton { "Undo" }, redoButton { "Redo" }, settingsButton { "Audio settings" },
                     runButton { "Run" };

    juce::Label channelsHeader { {}, "Channel rack" }, mixerHeader { {}, "Mixer" },
                consoleHeader { {}, "Command console (JSON)" };

    juce::TextEditor consoleInput, consoleOutput;

    juce::OwnedArray<juce::Component> channelRows;
    juce::String channelRackKey;
    MixerView mixer { session, dispatcher };
    juce::TooltipWindow tooltips { this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace ng
