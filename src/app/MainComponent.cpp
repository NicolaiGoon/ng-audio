#include "MainComponent.h"
#include "Widgets.h"

namespace ng
{
namespace
{
    //==============================================================================
    /** One instrument in the channel rack: name, colour, instrument, which strip it feeds. */
    class ChannelRow  : public juce::Component
    {
    public:
        ChannelRow (CommandDispatcher& d, const juce::var& channel, const juce::var& strips)
            : dispatcher (d), id (channel["id"].toString()), swatch (d, id, colourFrom (channel["colour"]))
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

            for (auto* c : std::initializer_list<juce::Component*> { &swatch, &name, &status, &instrument, &output, &deleteButton })
                addAndMakeVisible (c);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (0, 4);
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

    addButton.onClick = [this] { addInstrument(); };
    playButton.onClick = [this]
    {
        dispatcher.execute (session.getEdit().getTransport().isPlaying() ? "stop" : "play");
    };
    undoButton.onClick = [this] { dispatcher.undo(); };
    redoButton.onClick = [this] { dispatcher.redo(); };
    settingsButton.onClick = [this] { showAudioSettings(); };
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

    for (auto* c : std::initializer_list<juce::Component*> { &addButton, &playButton, &undoButton, &redoButton,
                                                             &settingsButton, &runButton, &channelsHeader,
                                                             &mixerHeader, &consoleHeader, &consoleInput, &mixer,
                                                             &consoleOutput })
        addAndMakeVisible (c);

    // Start with one instrument so Play makes a sound straight away.
    addInstrument();
    session.getUndoManager().clearUndoHistory();

    setSize (1180, 760);
    startTimerHz (10);
}

MainComponent::~MainComponent()
{
    dispatcher.onCommandExecuted = nullptr;
}

//==============================================================================
void MainComponent::addInstrument()
{
    // One undo step: the channel, its linked strip and a short arpeggio.
    auto name = "Synth " + juce::String (session.getChannels().size() + 1);

    juce::Array<juce::var> notes;
    const int pattern[] = { 0, 4, 7, 12, 7, 4, 0, -5 };
    auto root = 57 + 2 * session.getChannels().size() % 12;

    for (int i = 0; i < 8; ++i)
        notes.add (makeObject ({ { "pitch", root + pattern[i] }, { "start", i * 0.5 },
                                 { "length", 0.45 }, { "velocity", 100 } }));

    juce::Array<juce::var> batch {
        makeObject ({ { "command", "create_track" }, { "params", makeObject ({ { "name", name } }) } }),
        makeObject ({ { "command", "add_midi_clip" },
                      { "params", makeObject ({ { "channel", name }, { "length_beats", 4 },
                                                { "notes", notes } }) } })
    };

    dispatcher.executeBatch ("Add " + name, batch);
}

void MainComponent::runConsoleCommand()
{
    consoleOutput.setText (dispatcher.executeJson (consoleInput.getText()));
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
}

void MainComponent::timerCallback()
{
    updateButtons();
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
            addAndMakeVisible (channelRows.add (new ChannelRow (dispatcher, c, strips)));
    }

    mixer.update (state);
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

    for (auto* b : { &addButton, &playButton, &undoButton, &redoButton })
    {
        b->setBounds (toolbar.removeFromLeft (110));
        toolbar.removeFromLeft (6);
    }

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
        row->setBounds (left.removeFromTop (34));

    mixerHeader.setBounds (right.removeFromTop (24));
    mixer.setBounds (right);
}

} // namespace ng
