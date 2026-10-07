#include "MainComponent.h"

namespace ng
{
namespace
{
    juce::var makeObject (std::initializer_list<std::pair<const char*, juce::var>> props)
    {
        auto obj = new juce::DynamicObject();

        for (auto& [k, v] : props)
            obj->setProperty (k, v);

        return juce::var (obj);
    }

    juce::Colour colourFrom (const juce::var& v)
    {
        return juce::Colour::fromString ("ff" + v.toString().removeCharacters ("#"));
    }

    //==============================================================================
    /** One instrument in the channel rack, with a picker for which mixer strip it feeds. */
    class ChannelRow  : public juce::Component
    {
    public:
        ChannelRow (CommandDispatcher& d, const juce::var& channel, const juce::var& strips)
            : dispatcher (d), id (channel["id"].toString()), colour (colourFrom (channel["colour"]))
        {
            name.setText (channel["name"].toString(), juce::dontSendNotification);
            status.setText (channel["linked"] ? "linked" : "detached", juce::dontSendNotification);
            status.setColour (juce::Label::textColourId, juce::Colours::grey);

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

            addAndMakeVisible (name);
            addAndMakeVisible (status);
            addAndMakeVisible (output);
        }

        void paint (juce::Graphics& g) override
        {
            g.setColour (colour);
            g.fillRoundedRectangle (getLocalBounds().removeFromLeft (8).reduced (0, 4).toFloat(), 2.0f);
        }

        void resized() override
        {
            auto r = getLocalBounds().withTrimmedLeft (14).reduced (0, 4);
            output.setBounds (r.removeFromRight (160));
            status.setBounds (r.removeFromRight (70));
            name.setBounds (r);
        }

    private:
        CommandDispatcher& dispatcher;
        juce::String id;
        juce::Colour colour;
        juce::StringArray stripIds;
        juce::Label name, status;
        juce::ComboBox output;
    };

    /** One mixer strip and how many channels feed it. */
    class StripRow  : public juce::Component
    {
    public:
        explicit StripRow (const juce::var& strip)
            : colour (colourFrom (strip["colour"]))
        {
            name.setText (strip["name"].toString(), juce::dontSendNotification);

            auto numInputs = strip["inputs"].size();
            inputs.setText (juce::String (numInputs) + (numInputs == 1 ? " input" : " inputs"), juce::dontSendNotification);
            inputs.setColour (juce::Label::textColourId, juce::Colours::grey);

            addAndMakeVisible (name);
            addAndMakeVisible (inputs);
        }

        void paint (juce::Graphics& g) override
        {
            g.setColour (colour.withAlpha (0.25f));
            g.fillRoundedRectangle (getLocalBounds().reduced (0, 2).toFloat(), 4.0f);
            g.setColour (colour);
            g.fillRoundedRectangle (getLocalBounds().removeFromLeft (8).reduced (0, 2).toFloat(), 2.0f);
        }

        void resized() override
        {
            auto r = getLocalBounds().withTrimmedLeft (14).reduced (0, 2);
            inputs.setBounds (r.removeFromRight (80));
            name.setBounds (r);
        }

    private:
        juce::Colour colour;
        juce::Label name, inputs;
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
                                                             &mixerHeader, &consoleHeader, &consoleInput,
                                                             &consoleOutput })
        addAndMakeVisible (c);

    // Start with one instrument so Play makes a sound straight away.
    addInstrument();
    session.getUndoManager().clearUndoHistory();

    setSize (960, 640);
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

    channelRows.clear();
    stripRows.clear();

    for (auto& c : *state["channels"].getArray())
        addAndMakeVisible (channelRows.add (new ChannelRow (dispatcher, c, strips)));

    for (auto& s : *strips.getArray())
        addAndMakeVisible (stripRows.add (new StripRow (s)));

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

    auto console = r.removeFromBottom (190);
    consoleHeader.setBounds (console.removeFromTop (24));
    auto consoleTop = console.removeFromTop (60);
    runButton.setBounds (consoleTop.removeFromRight (70).reduced (4, 14));
    consoleInput.setBounds (consoleTop);
    console.removeFromTop (6);
    consoleOutput.setBounds (console);
    r.removeFromBottom (10);

    auto left = r.removeFromLeft (r.getWidth() / 2).withTrimmedRight (8);
    auto right = r.withTrimmedLeft (8);

    channelsHeader.setBounds (left.removeFromTop (24));
    for (auto* row : channelRows)
        row->setBounds (left.removeFromTop (34));

    mixerHeader.setBounds (right.removeFromTop (24));
    for (auto* row : stripRows)
        row->setBounds (right.removeFromTop (34));
}

} // namespace ng
