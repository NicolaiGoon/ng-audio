#include "ChannelRack.h"
#include "BrowserPanel.h"
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
            setTooltip ("Click a step to turn it on or off, or drag across steps to paint them");
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
            auto i = padAt (e.position);

            if (i < 0)
                return;

            // The first pad decides whether the drag paints steps on or off; right-click only clears.
            paintOn = ! e.mods.isPopupMenu() && steps[i] != 'x';
            dispatcher.beginGesture (paintOn ? "Steps on" : "Steps off");
            painting = true;
            setStep (i);
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (painting)
                if (auto i = padAt (e.position); i >= 0)
                    setStep (i);
        }

        void mouseUp (const juce::MouseEvent&) override
        {
            if (std::exchange (painting, false))
                dispatcher.endGesture();
        }

    private:
        int padAt (juce::Point<float> pos) const
        {
            for (int i = 0; i < steps.length(); ++i)
                if (getPadBounds (i).contains (pos.withY (getHeight() / 2.0f)))
                    return i;

            return -1;
        }

        void setStep (int i)
        {
            if ((steps[i] == 'x') == paintOn)
                return;

            // Shown straight away; the rack is rebuilt from the project after the command.
            steps = steps.substring (0, i) + (paintOn ? "x" : ".") + steps.substring (i + 1);
            repaint();
            dispatcher.execute ("set_steps", makeObject ({ { "channel", id }, { "step", i }, { "on", paintOn } }));
        }

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
        bool painting = false, paintOn = true;
    };

    //==============================================================================
    /** One instrument in the channel rack: name, colour, instrument, which strip it feeds.
        Takes instruments and presets dropped from the browser, and samples and kit folders
        (from the browser or the desktop) when it plays the sampler. Right-click it to save its
        instrument as a preset. */
    class ChannelRow  : public juce::Component,
                        public juce::DragAndDropTarget,
                        public juce::FileDragAndDropTarget
    {
    public:
        /** pluginInstruments: the scanned plugin instruments, as (id, name). */
        ChannelRow (CommandDispatcher& d, const juce::var& channel, const juce::var& strips,
                    const juce::Array<std::pair<juce::String, juce::String>>& pluginInstruments, const int& playStep,
                    const juce::String& audioFileWildcard, const juce::var& audioInputs)
            : dispatcher (d), id (channel["id"].toString()), swatch (d, id, colourFrom (channel["colour"])),
              grid (d, id, channel["pattern"]["steps"].toString(), colourFrom (channel["colour"]), playStep),
              audioWildcard (audioFileWildcard), channelName (channel["name"].toString()),
              isSampler (channel["instrument"] == "sampler"), isAudio (channel["kind"] == "audio"),
              hasInstrument (! isAudio && channel["instrument"] != "none" && ! static_cast<bool> (channel["instrument_missing"]))
        {
            name.addMouseListener (this, false);
            status.addMouseListener (this, false);

            setUpNameLabel (name, dispatcher, id, channel["name"].toString());
            setUpDeleteButton (deleteButton, dispatcher, id, "channel");

            armButton.setClickingTogglesState (false);
            armButton.setToggleState (channel["armed"], juce::dontSendNotification);
            armButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffd0402f));
            armButton.setTooltip ("Arm for MIDI input: play this channel from a MIDI keyboard (or Keys) and record into it");
            armButton.onClick = [this]
            {
                auto arming = ! armButton.getToggleState();

                // An audio channel with no input yet gets the first one.
                if (isAudio && arming && inputIds.isEmpty())
                {
                    showWarning ("Can't arm for audio", "The audio device has no inputs. Pick one that does in Audio settings.");
                    return;
                }

                if (isAudio && arming && input.getSelectedId() <= 1)
                    dispatcher.execute ("set_audio_input", makeObject ({ { "channel", id }, { "input", inputIds[0] } }));

                auto result = dispatcher.execute ("arm_channel", makeObject ({ { "channel", id }, { "armed", arming } }));

                if (! result.ok)
                    showWarning ("Can't arm", result.error);
            };

            status.setText (channel["linked"] ? "linked" : "detached", juce::dontSendNotification);
            status.setColour (juce::Label::textColourId, juce::Colours::grey);

            // The built-in choices, then every scanned plugin instrument. A plugin the project
            // uses but this computer doesn't have still shows, marked missing.
            instrumentIds = { "4osc", "sampler", "none" };
            instrument.addItem ("4OSC synth", 1);
            instrument.addItem ("Sampler", 2);
            instrument.addItem ("No instrument", 3);

            auto current = currentInstrument = channel["instrument"].toString();
            auto missing = static_cast<bool> (channel["instrument_missing"]);

            if (! pluginInstruments.isEmpty() || missing)
                instrument.addSectionHeading ("Plugins");

            for (auto& plugin : pluginInstruments)
            {
                instrumentIds.add (plugin.first);
                instrument.addItem (plugin.second, instrumentIds.size());
            }

            if (! instrumentIds.contains (current))
            {
                instrumentIds.add (current);
                instrument.addItem (channel["instrument_name"].toString() + (missing ? " (missing)" : ""), instrumentIds.size());
            }

            instrument.setSelectedId (instrumentIds.indexOf (current) + 1, juce::dontSendNotification);
            instrument.setTooltip (missing ? "This plugin isn't installed on this computer (or didn't load); scan for it in Plugins"
                                           : "The instrument this channel plays");
            instrument.onChange = [this]
            {
                auto choice = instrumentIds[instrument.getSelectedId() - 1];
                auto result = dispatcher.execute ("set_instrument", makeObject ({ { "channel", id }, { "instrument", choice } }));

                if (result.ok)
                    return;

                instrument.setSelectedId (instrumentIds.indexOf (currentInstrument) + 1, juce::dontSendNotification);
                juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsOk (juce::MessageBoxIconType::WarningIcon,
                                                                                     "Couldn't change the instrument",
                                                                                     result.error + "\n\nIf the plugin was moved or "
                                                                                     "uninstalled, scan again in Plugins.",
                                                                                     {}, this),
                                              nullptr);
            };

            auto isPlugin = current != "4osc" && current != "sampler" && current != "none";
            editButton.setEnabled ((isPlugin && ! missing) || isSampler);
            editButton.setTooltip (isSampler ? "Open the sampler: add samples or load a drum kit"
                                             : isPlugin ? "Open the plugin's own window" : "The 4OSC synth has no window of its own");
            editButton.onClick = [this]
            {
                dispatcher.execute ("show_plugin_window", makeObject ({ { "channel", id } }));
            };

            // A plugin with more than one stereo output can give each output a strip.
            auto multiOutput = static_cast<bool> (channel["multi_output"]);
            hasOutputs = isPlugin && ! missing && static_cast<int> (channel["outputs"]) > 2;
            outsButton.setClickingTogglesState (false);
            outsButton.setToggleState (multiOutput, juce::dontSendNotification);
            outsButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff3c6e8c));
            outsButton.setTooltip (multiOutput ? "Each output has its own mixer strip; click to go back to one stereo output"
                                               : "This plugin has " + channel["outputs"].toString()
                                                   + " outputs: click to give each stereo output its own mixer strip");
            outsButton.onClick = [this]
            {
                auto result = dispatcher.execute ("set_multi_output", makeObject ({ { "channel", id },
                                                                                    { "enabled", ! outsButton.getToggleState() } }));

                if (! result.ok)
                    juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsOk (juce::MessageBoxIconType::WarningIcon,
                                                                                         "Multiple outputs", result.error, {}, this),
                                                  nullptr);
            };

            if (multiOutput)
                instrument.setTooltip ("Turn off Outs to change the instrument");

            instrument.setEnabled (! multiOutput);
            addChildComponent (outsButton);
            outsButton.setVisible (hasOutputs || multiOutput);

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

            for (auto* c : std::initializer_list<juce::Component*> { &swatch, &name, &status, &armButton, &instrument, &editButton, &output, &deleteButton, &grid })
                addAndMakeVisible (c);

            // An audio channel plays its clips and records from an audio input: no instrument,
            // no MIDI, no steps.
            if (isAudio)
            {
                for (auto* c : std::initializer_list<juce::Component*> { &instrument, &editButton, &outsButton, &grid })
                    c->setVisible (false);

                armButton.setTooltip ("Arm to record from the audio input (any number of audio channels can be armed)");

                input.addItem ("No input", 1);
                auto currentInput = channel["input"].toString();

                for (auto& inputName : *audioInputs.getArray())
                {
                    inputIds.add (inputName.toString());
                    input.addItem (inputName.toString(), inputIds.size() + 1);
                }

                if (currentInput.isNotEmpty() && ! inputIds.contains (currentInput))
                {
                    // Saved on a computer whose sound card had this input.
                    inputIds.add (currentInput);
                    input.addItem (currentInput + " (not here)", inputIds.size() + 1);
                }

                input.setSelectedId (currentInput.isEmpty() ? 1 : inputIds.indexOf (currentInput) + 2, juce::dontSendNotification);
                input.setTooltip ("The audio input this channel records from");
                input.onChange = [this]
                {
                    auto index = input.getSelectedId() - 2;
                    dispatcher.execute ("set_audio_input", makeObject ({ { "channel", id },
                                                                         { "input", index >= 0 ? inputIds[index] : juce::String ("none") } }));
                };

                monitorIds = { "auto", "on", "off" };
                monitor.addItemList ({ "Mon auto", "Mon on", "Mon off" }, 1);
                monitor.setSelectedId (monitorIds.indexOf (channel["monitor"].toString()) + 1, juce::dontSendNotification);
                monitor.setTooltip ("Monitoring: hear the input through this channel while it's armed (auto), always (on) or never (off)");
                monitor.onChange = [this]
                {
                    dispatcher.execute ("set_audio_input", makeObject ({ { "channel", id }, { "monitor", monitorIds[monitor.getSelectedId() - 1] } }));
                };

                addAndMakeVisible (input);
                addAndMakeVisible (monitor);
                audioHint.setText ("Drop audio files on its lane in the timeline, or arm it and record", juce::dontSendNotification);
                audioHint.setColour (juce::Label::textColourId, juce::Colours::grey);
                audioHint.setFont (juce::FontOptions (12.0f));
                addAndMakeVisible (audioHint);
            }
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (6, 5);
            grid.setBounds (r.removeFromBottom (22).withTrimmedLeft (20));
            audioHint.setBounds (grid.getBounds());
            r.removeFromBottom (4);
            swatch.setBounds (r.removeFromLeft (14));
            r.removeFromLeft (6);
            deleteButton.setBounds (r.removeFromRight (26));
            r.removeFromRight (4);
            output.setBounds (r.removeFromRight (130));
            r.removeFromRight (4);
            editButton.setBounds (r.removeFromRight (40));
            r.removeFromRight (2);

            if (outsButton.isVisible())
            {
                outsButton.setBounds (r.removeFromRight (42));
                r.removeFromRight (2);
            }

            instrument.setBounds (r.removeFromRight (outsButton.isVisible() ? 106 : 150));
            auto audioArea = instrument.getBounds().getUnion (editButton.getBounds());
            monitor.setBounds (audioArea.removeFromRight (94));
            audioArea.removeFromRight (2);
            input.setBounds (audioArea);
            r.removeFromRight (4);
            armButton.setBounds (r.removeFromRight (26));
            status.setBounds (r.removeFromRight (64));
            name.setBounds (r);
        }

        //==============================================================================
        void mouseDown (const juce::MouseEvent& e) override
        {
            if (! e.mods.isPopupMenu())
                return;

            juce::PopupMenu menu;
            menu.addItem ("Save instrument as preset...", hasInstrument, false, [this]
            {
                browser::savePresetWithName (dispatcher, this, channelName, makeObject ({ { "channel", id } }));
            });
            menu.showMenuAsync (juce::PopupMenu::Options());
        }

        void paint (juce::Graphics& g) override
        {
            g.setColour (juce::Colours::white.withAlpha (0.035f));
            g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (0.0f, 1.0f), 5.0f);
        }

        void paintOverChildren (juce::Graphics& g) override
        {
            if (dropHighlight)
            {
                g.setColour (juce::Colour (0xff3c8ce0));
                g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 4.0f, 2.0f);
            }
        }

        bool isInterestedInDragSource (const SourceDetails& details) override
        {
            return ! isAudio && browser::canDropOnChannel (details.description, isSampler);
        }

        void itemDragEnter (const SourceDetails&) override     { setDropHighlight (true); }
        void itemDragExit (const SourceDetails&) override      { setDropHighlight (false); }

        void itemDropped (const SourceDetails& details) override
        {
            setDropHighlight (false);
            drop ({ details.description });
        }

        bool isInterestedInFileDrag (const juce::StringArray& files) override
        {
            return isSampler && ! itemsForFiles (files).isEmpty();
        }

        void fileDragEnter (const juce::StringArray&, int, int) override  { setDropHighlight (true); }
        void fileDragExit (const juce::StringArray&) override             { setDropHighlight (false); }

        void filesDropped (const juce::StringArray& files, int, int) override
        {
            setDropHighlight (false);
            drop (itemsForFiles (files));
        }

    private:
        void showWarning (const juce::String& title, const juce::String& message)
        {
            juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsOk (juce::MessageBoxIconType::WarningIcon,
                                                                                 title, message, {}, this),
                                          nullptr);
        }

        juce::Array<juce::var> itemsForFiles (const juce::StringArray& files) const
        {
            juce::Array<juce::var> items;

            for (auto& f : files)
                if (auto item = browser::itemForFile (juce::File (f), audioWildcard); item.isObject())
                    items.add (item);

            return items;
        }

        void setDropHighlight (bool on)
        {
            dropHighlight = on;
            repaint();
        }

        void drop (const juce::Array<juce::var>& items)
        {
            // The rack is rebuilt after the commands (asynchronously), so this row is still here.
            browser::showDropError (getParentComponent(), browser::dropOnChannel (dispatcher, id, items));
        }

        CommandDispatcher& dispatcher;
        juce::String id;
        ColourSwatch swatch;
        juce::StringArray stripIds, instrumentIds;
        juce::String currentInstrument;
        juce::Label name, status;
        juce::ComboBox instrument, output;
        juce::TextButton deleteButton, armButton { juce::String::fromUTF8 ("\xe2\x97\x8f") }, editButton { "Edit" },
                         outsButton { "Outs" };
        bool hasOutputs = false;
        StepGrid grid;
        juce::String audioWildcard, channelName;
        juce::Label audioHint;
        juce::ComboBox input, monitor;
        juce::StringArray inputIds, monitorIds;
        bool isSampler = false, isAudio = false, hasInstrument = false, dropHighlight = false;
    };
}

//==============================================================================
ChannelRack::ChannelRack (Session& s, CommandDispatcher& d)
    : session (s), dispatcher (d)
{
    viewport.setViewedComponent (&holder, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (10);
    addAndMakeVisible (viewport);
}

ChannelRack::~ChannelRack()
{
    rows.clear();
}

void ChannelRack::clear()
{
    rows.clear();
    key = {};
}

void ChannelRack::update (const juce::var& state)
{
    auto strips = state["strips"];
    juce::Array<std::pair<juce::String, juce::String>> pluginInstruments;

    for (auto& d : session.getEngine().getPluginManager().knownPluginList.getTypes())
        if (d.isInstrument)
            pluginInstruments.add ({ d.createIdentifierString(), d.name });

    std::sort (pluginInstruments.begin(), pluginInstruments.end(),
               [] (auto& a, auto& b) { return a.second.compareNatural (b.second) < 0; });

    // The rack shows channels, the strip names they can route to and the instruments they
    // can pick; fader moves don't change it.
    juce::String newKey = juce::JSON::toString (state["channels"], true);

    for (auto& s : *strips.getArray())
        newKey << "|" << s["id"].toString() << ":" << s["name"].toString();

    for (auto& plugin : pluginInstruments)
        newKey << "|" << plugin.first;

    newKey << "|" << juce::JSON::toString (state["audio_inputs"], true);

    if (newKey == key)
        return;

    key = newKey;
    rows.clear();

    for (auto& c : *state["channels"].getArray())
        holder.addAndMakeVisible (rows.add (new ChannelRow (dispatcher, c, strips, pluginInstruments, playStep,
                                                            session.getAudioFileWildcard(), state["audio_inputs"])));

    resized();
    repaint();
}

void ChannelRack::setPlayStep (int step)
{
    if (step == playStep)
        return;

    playStep = step;

    for (auto* row : rows)
        row->repaint();
}

void ChannelRack::paint (juce::Graphics& g)
{
    if (rows.isEmpty())
    {
        g.setColour (juce::Colours::grey);
        g.setFont (juce::FontOptions (14.0f));
        g.drawFittedText ("No channels yet. Add an instrument, or drop one (or samples) here from the browser.",
                          getLocalBounds().reduced (20), juce::Justification::centred, 3);
    }
}

void ChannelRack::paintOverChildren (juce::Graphics& g)
{
    if (! highlight)
        return;

    auto r = getLocalBounds().toFloat().reduced (2.0f);
    g.setColour (juce::Colour (0xff3c8ce0).withAlpha (0.12f));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (juce::Colour (0xff3c8ce0));
    g.drawRoundedRectangle (r, 5.0f, 2.0f);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText ("Drop for a new channel", r.removeFromBottom (26.0f), juce::Justification::centred);
}

void ChannelRack::resized()
{
    viewport.setBounds (getLocalBounds().reduced (6, 4));
    auto width = viewport.getWidth() - (rows.size() * rowHeight > viewport.getHeight() ? viewport.getScrollBarThickness() + 2 : 0);
    holder.setSize (width, rows.size() * rowHeight);

    for (int i = 0; i < rows.size(); ++i)
        rows[i]->setBounds (0, i * rowHeight, width, rowHeight - 2);
}

void ChannelRack::setHighlight (bool on)
{
    if (highlight != on)
    {
        highlight = on;
        repaint();
    }
}

bool ChannelRack::isInterestedInDragSource (const SourceDetails& details)
{
    return browser::canDropOnRack (details.description);
}

void ChannelRack::itemDropped (const SourceDetails& details)
{
    setHighlight (false);
    browser::showDropError (this, browser::dropOnRack (dispatcher, { details.description }));
}

juce::Array<juce::var> ChannelRack::itemsForFiles (const juce::StringArray& files) const
{
    juce::Array<juce::var> items;

    for (auto& f : files)
        if (auto item = browser::itemForFile (juce::File (f), session.getAudioFileWildcard()); item.isObject())
            items.add (item);

    return items;
}

bool ChannelRack::isInterestedInFileDrag (const juce::StringArray& files)
{
    return ! itemsForFiles (files).isEmpty();
}

void ChannelRack::filesDropped (const juce::StringArray& files, int, int)
{
    setHighlight (false);
    browser::showDropError (this, browser::dropOnRack (dispatcher, itemsForFiles (files)));
}

} // namespace ng
