#include "MainComponent.h"
#include "ExportPanel.h"
#include "Widgets.h"
#include "PluginWindow.h"

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
            auto r = getLocalBounds().reduced (0, 4);
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
                { "Ctrl+K", "Show or hide the keyboard" },
                { "Ctrl+B", "Show or hide the browser" },
                { "Ctrl+H", "The undo history" },
                { "Ctrl+J", "Show or hide the AI chat" },
                { "Ctrl+,", "Settings" },
                { "F1", "This list" },
            };
            return rows;
        }

        static const Rows& right()
        {
            static const Rows rows {
                { "Timeline (click a clip first)", nullptr },
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

        // (Not for the chat panel's changes: they show in the chat.)
        if (! commandLogShown && entry.source != "Console" && ! entry.source.endsWith ("(chat)") && ! consoleButton.getToggleState())
        {
            consoleButton.setToggleState (true, juce::dontSendNotification);
            consoleButton.onClick();
        }

        commandLogShown = true;
    };

    // Before a project is replaced, let go of everything that points into it.
    session.onEditAboutToChange = [this]
    {
        mixer.clear();
        timeline.clear();
        closePianoRoll();
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
    audioButton.onClick = [this] { showAudioMenu(); };
    exportButton.onClick = [this] { showExport(); };
    exportButton.setTooltip ("Export the song or the loop region as a WAV or MP3 file, or as stems (Ctrl+E)");
    audioButton.setTooltip ("Import audio files onto new audio channels at the playhead (Ctrl+I), or add an empty audio channel");
    addButton.setTooltip ("Add an instrument channel (Ctrl+T)");
    playButton.setTooltip ("Play or stop (Space)");
    playButton.onClick = [this]
    {
        dispatcher.execute (session.getEdit().getTransport().isPlaying() ? "stop" : "play");
    };
    returnButton.onClick = [this] { dispatcher.execute ("set_position", makeObject ({ { "beat", 0 } })); };
    recordButton.onClick = [this] { toggleRecording(); };
    recordButton.setTooltip ("Record into the armed channels from the playhead (MIDI or audio); click again (or Stop) to finish (R)");
    recordButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffd0402f));

    keysButton.setTooltip ("Show a keyboard to play the armed channel with the mouse or the computer keys (A W S E D ...) (Ctrl+K)");
    keysButton.onClick = [this] { showKeyboard (keysButton.getToggleState()); };

    browserButton.setTooltip ("Show the browser: instruments, effects, presets and samples to drag in (Ctrl+B)");
    browserButton.onClick = [this] { showBrowser (browserButton.getToggleState()); };

    chatButton.setTooltip ("Show the AI chat: ask for changes in plain words, with Claude, OpenAI, Gemini or a local model (Ctrl+J)");
    chatButton.onClick = [this] { showChat (chatButton.getToggleState()); };
    chatPanel.onOpenSettings = [this] { showPreferences ("AI"); };
    addChildComponent (chatPanel);
    addChildComponent (browserPanel);
    showBrowser (session.getEngine().getPropertyStorage().getPropertiesFile().getBoolValue ("ngBrowserVisible", true));
    keyboardState.addListener (this);
    keyboard.setAvailableRange (24, 108);
    keyboard.setLowestVisibleKey (48);
    keyboard.setKeyPressBaseOctave (5);
    keyboard.setOctaveForMiddleC (4);   // middle C is C4, as in the piano roll
    returnButton.setTooltip ("Back to the start (Home)");
    undoButton.onClick = [this] { dispatcher.undo(); };
    redoButton.onClick = [this] { dispatcher.redo(); };
    undoButton.setTooltip ("Undo (Ctrl+Z)");
    redoButton.setTooltip ("Redo (Ctrl+Y or Ctrl+Shift+Z)");
    shortcutsButton.setTooltip ("Keyboard shortcuts (F1)");
    shortcutsButton.onClick = [this] { showShortcuts(); };
    settingsButton.onClick = [this] { showPreferences(); };
    settingsButton.setTooltip ("Settings: audio and MIDI devices, buffer size, theme, project folder and autosave (Ctrl+,)");
    historyButton.onClick = [this] { showHistory(); };
    historyButton.setTooltip ("The undo history: click a step to go back to it (Ctrl+H)");

    cpuMeter.setFont (juce::FontOptions (12.0f));
    cpuMeter.setJustificationType (juce::Justification::centred);
    cpuMeter.setColour (juce::Label::outlineColourId, juce::Colours::grey.withAlpha (0.4f));
    pluginsButton.onClick = [this] { showPluginList(); };
    addBusButton.setTooltip ("Add a bus: a strip other strips can send to, e.g. for a shared reverb");
    addBusButton.onClick = [this] { dispatcher.execute ("create_bus"); };
    pluginsButton.setTooltip ("Your VST3 plugins: scan folders for new ones, or remove ones you don't use");
    setUpTransportControls();
    runButton.onClick = [this] { runConsoleCommand(); };

    timeline.onOpenClip = [this] (const juce::String& clipId) { openPianoRoll (clipId); };
    closePianoRollButton.onClick = [this] { closePianoRoll(); };
    closePianoRollButton.setTooltip ("Back to the channel rack and mixer");
    quantizeButton.onClick = [this] { pianoRoll.quantize(); };
    quantizeButton.setTooltip ("Move the selected notes, or all of them, onto the snap grid (Q)");

    // Grid sizes in beats: quarter, eighth, sixteenth and thirty-second notes.
    snapBox.addItemList ({ "1/4", "1/8", "1/16", "1/32" }, 1);
    snapBox.setSelectedId (3, juce::dontSendNotification);
    snapBox.setTooltip ("Grid the piano roll snaps to (hold Alt to place freely)");
    snapBox.onChange = [this] { pianoRoll.setGrid (1.0 / std::pow (2.0, snapBox.getSelectedId() - 1)); };
    snapLabel.setJustificationType (juce::Justification::centredRight);

    consoleButton.setTooltip ("Show the JSON command console and the log of what AI clients did");
    consoleButton.onClick = [this]
    {
        for (auto* c : std::initializer_list<juce::Component*> { &consoleHeader, &consoleInput, &consoleOutput, &runButton, &commandLog })
            c->setVisible (consoleButton.getToggleState());

        resized();
    };

    for (auto* l : { &timelineHeader, &channelsHeader, &mixerHeader, &consoleHeader, &pianoRollHeader })
        l->setFont (juce::FontOptions (15.0f, juce::Font::bold));

    for (auto* hint : { &timelineHint, &pianoRollHint })
    {
        hint->setFont (juce::FontOptions (12.0f));
        hint->setColour (juce::Label::textColourId, juce::Colours::grey);
        hint->setJustificationType (juce::Justification::centredRight);
    }

    consoleInput.setMultiLine (true);
    consoleInput.setReturnKeyStartsNewLine (true);
    consoleInput.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain));
    consoleInput.setText (R"({"command": "get_state"})");

    consoleOutput.setMultiLine (true);
    consoleOutput.setReadOnly (true);
    consoleOutput.setFont (consoleInput.getFont());

    for (auto* c : std::initializer_list<juce::Component*> { &newButton, &openButton, &saveButton, &saveAsButton,
                                                             &addButton, &playButton, &undoButton, &redoButton,
                                                             &returnButton, &recordButton, &keysButton, &tempo, &timeSignature, &positionDisplay,
                                                             &loopButton, &metronomeButton,
                                                             &settingsButton, &historyButton, &cpuMeter, &chatButton, &pluginsButton, &shortcutsButton, &consoleButton, &browserButton, &audioButton, &exportButton, &timelineHeader, &timelineHint, &timeline,
                                                             &channelsHeader, &mixerHeader, &addBusButton, &mixer })
        addAndMakeVisible (c);

    for (auto* c : std::initializer_list<juce::Component*> { &consoleHeader, &consoleInput, &consoleOutput, &runButton, &commandLog, &keyboard,
                                                             &pianoRollHeader, &pianoRollHint, &snapLabel, &snapBox,
                                                             &closePianoRollButton, &quantizeButton, &pianoRoll })
        addChildComponent (c);

    loadStarterProject();
    showChat (session.getEngine().getPropertyStorage().getPropertiesFile().getBoolValue ("ngChatVisible", false));

    if (auto error = bridge.start(); error.isNotEmpty())
        DBG ("AI bridge: " << error);

    setWantsKeyboardFocus (true);
    juce::Desktop::getInstance().addFocusChangeListener (this);
    session.getEngine().getPluginManager().knownPluginList.addChangeListener (this);   // a scan adds instruments to pick
    setSize (1440, 860);
    startTimerHz (30);

    // After the window is up, offer back anything a crash left unsaved.
    juce::MessageManager::callAsync ([safeThis = juce::Component::SafePointer (this)]
    {
        if (safeThis != nullptr)
            safeThis->checkForRecovery();
    });
}

MainComponent::~MainComponent()
{
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
}

//==============================================================================
void MainComponent::addInstrument()
{
    // A new channel with its linked strip and an empty pattern, ready to fill in.
    auto name = "Synth " + juce::String (session.getChannels().size() + 1);
    dispatcher.execute ("create_track", makeObject ({ { "name", name } }));
}

void MainComponent::showAudioMenu()
{
    juce::PopupMenu menu;
    menu.addItem ("Import audio files... (Ctrl+I)", [this]
    {
        timeline.chooseAudioFiles ({}, timeline.getPlayheadBeatForImport());
    });
    menu.addItem ("Empty audio channel", [this] { dispatcher.execute ("create_track", makeObject ({ { "instrument", "audio" } })); });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&audioButton));
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

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
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
    if (key == juce::KeyPress ('j', command, 0))                    { showChat (! chatPanel.isVisible()); return true; }
    if (key == juce::KeyPress (',', command, 0))                    { showPreferences(); return true; }

    if (key == juce::KeyPress (juce::KeyPress::spaceKey))           { playButton.onClick(); return true; }
    if (key == juce::KeyPress (juce::KeyPress::homeKey))            { returnButton.onClick(); return true; }
    if (key == juce::KeyPress ('r', none, 0))                       { toggleRecording(); return true; }
    if (key == juce::KeyPress ('c', none, 0))                       { dispatcher.execute ("set_metronome"); return true; }

    if (key == juce::KeyPress ('l', command, 0))
    {
        dispatcher.execute ("set_loop", makeObject ({ { "enabled", ! loopButton.getToggleState() } }));
        return true;
    }

    if (key == juce::KeyPress ('b', command, 0))
    {
        showBrowser (! browserPanel.isVisible());
        return true;
    }

    if (key == juce::KeyPress ('k', command, 0))
    {
        keysButton.setToggleState (! keysButton.getToggleState(), juce::dontSendNotification);
        showKeyboard (keysButton.getToggleState());
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

    return false;
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

void MainComponent::globalFocusChanged (juce::Component* focused)
{
    // Buttons and drop-downs keep the keyboard focus after they're clicked, and would then
    // take Space, Return and the arrow keys. Give it back to the panel being worked in.
    // If nothing has the focus (e.g. a text box just closed), keys would go nowhere.
    if (focused == nullptr)
    {
        if (isShowing() && juce::Process::isForegroundProcess())
            juce::MessageManager::callAsync ([safeThis = juce::Component::SafePointer (this)]
            {
                if (safeThis != nullptr && juce::Component::getCurrentlyFocusedComponent() == nullptr)
                    safeThis->restoreFocus();
            });

        return;
    }

    if (! isParentOf (focused))
        return;

    if (focused == &timeline || focused == &pianoRoll || focused == &keyboard)
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

namespace
{
    // The time signatures offered in the transport; set_time_signature takes any.
    const std::pair<int, int> commonTimeSignatures[] = { { 2, 4 }, { 3, 4 }, { 4, 4 }, { 5, 4 }, { 6, 8 }, { 7, 8 }, { 12, 8 } };
}

void MainComponent::setUpTransportControls()
{
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

    loopButton.setTooltip ("Loop playback (the whole project unless a loop region is set) (Ctrl+L)");
    loopButton.onClick = [this]
    {
        dispatcher.execute ("set_loop", makeObject ({ { "enabled", loopButton.getToggleState() } }));
    };

    metronomeButton.setTooltip ("Click on every beat while playing, louder on the first beat of a bar (C)");
    metronomeButton.onClick = [this]
    {
        dispatcher.execute ("set_metronome", makeObject ({ { "enabled", metronomeButton.getToggleState() } }));
    };

    positionDisplay.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 16.0f, juce::Font::bold));
    positionDisplay.setJustificationType (juce::Justification::centred);
    positionDisplay.setColour (juce::Label::backgroundColourId, juce::Colour (0xff1e2124));
    positionDisplay.setColour (juce::Label::textColourId, juce::Colour (0xff8fd18f));
    positionDisplay.setTooltip ("Song position: bar . beat . sixteenth");
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

void MainComponent::showBrowser (bool shouldShow)
{
    browserButton.setToggleState (shouldShow, juce::dontSendNotification);
    browserPanel.setVisible (shouldShow);
    session.getEngine().getPropertyStorage().getPropertiesFile().setValue ("ngBrowserVisible", shouldShow);
    resized();
}

//==============================================================================
void MainComponent::paintOverChildren (juce::Graphics& g)
{
    if (rackHighlight && ! rackArea.isEmpty())
    {
        g.setColour (juce::Colour (0xff3c8ce0));
        g.drawRoundedRectangle (rackArea.toFloat().expanded (2.0f), 5.0f, 2.0f);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText ("Drop for a new channel", rackArea.removeFromBottom (24), juce::Justification::centred);
    }
}

void MainComponent::setRackHighlight (bool on)
{
    if (rackHighlight != on)
    {
        rackHighlight = on;
        repaint();
    }
}

bool MainComponent::isInterestedInDragSource (const SourceDetails& details)
{
    return browser::canDropOnRack (details.description);
}

void MainComponent::itemDragMove (const SourceDetails& details)
{
    setRackHighlight (rackArea.contains (details.localPosition));
}

void MainComponent::itemDragExit (const SourceDetails&)
{
    setRackHighlight (false);
}

void MainComponent::itemDropped (const SourceDetails& details)
{
    setRackHighlight (false);

    if (rackArea.contains (details.localPosition))
        browser::showDropError (this, browser::dropOnRack (dispatcher, { details.description }));
}

juce::Array<juce::var> MainComponent::itemsForFiles (const juce::StringArray& files) const
{
    juce::Array<juce::var> items;

    for (auto& f : files)
        if (auto item = browser::itemForFile (juce::File (f), session.getAudioFileWildcard()); item.isObject())
            items.add (item);

    return items;
}

bool MainComponent::isInterestedInFileDrag (const juce::StringArray& files)
{
    return ! itemsForFiles (files).isEmpty();
}

void MainComponent::fileDragMove (const juce::StringArray&, int x, int y)
{
    setRackHighlight (rackArea.contains (x, y));
}

void MainComponent::fileDragExit (const juce::StringArray&)
{
    setRackHighlight (false);
}

void MainComponent::filesDropped (const juce::StringArray& files, int x, int y)
{
    setRackHighlight (false);

    if (rackArea.contains (x, y))
        browser::showDropError (this, browser::dropOnRack (dispatcher, itemsForFiles (files)));
}

void MainComponent::showKeyboard (bool shouldShow)
{
    keyboard.setVisible (shouldShow);

    // Something has to be listening: arm the first channel if none is.
    if (shouldShow && session.getArmedChannel() == nullptr && ! session.getChannels().isEmpty())
        dispatcher.execute ("arm_channel", makeObject ({ { "channel", session.getChannels().getFirst()->itemID.toString() } }));

    if (shouldShow)
        keyboard.grabKeyboardFocus();

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

void MainComponent::showChat (bool shouldShow)
{
    chatButton.setToggleState (shouldShow, juce::dontSendNotification);
    chatPanel.setVisible (shouldShow);
    session.getEngine().getPropertyStorage().getPropertiesFile().setValue ("ngChatVisible", shouldShow);
    resized();

    if (shouldShow && isShowing())
        chatPanel.focusInput();
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
void MainComponent::handleAsyncUpdate()
{
    if (std::exchange (browserNeedsRefresh, false))
        browserPanel.refresh();

    rebuildRows();
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
            chatPanel.updateProvider();   // Settings may have changed the provider
    }

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
    recordButton.setToggleState (session.getEdit().getTransport().isRecording(), juce::dontSendNotification);
    undoButton.setEnabled (dispatcher.canUndo());
    redoButton.setEnabled (dispatcher.canRedo());
}

void MainComponent::rebuildRows()
{
    auto state = session.toVar();
    auto strips = state["strips"];

    juce::Array<std::pair<juce::String, juce::String>> pluginInstruments;

    for (auto& d : session.getEngine().getPluginManager().knownPluginList.getTypes())
        if (d.isInstrument)
            pluginInstruments.add ({ d.createIdentifierString(), d.name });

    std::sort (pluginInstruments.begin(), pluginInstruments.end(),
               [] (auto& a, auto& b) { return a.second.compareNatural (b.second) < 0; });

    // The rack shows channels, the strip names they can route to and the instruments they
    // can pick; fader moves don't change it.
    juce::String key = juce::JSON::toString (state["channels"], true);

    for (auto& s : *strips.getArray())
        key << "|" << s["id"].toString() << ":" << s["name"].toString();

    for (auto& plugin : pluginInstruments)
        key << "|" << plugin.first;

    key << "|" << juce::JSON::toString (state["audio_inputs"], true);

    if (key != channelRackKey)
    {
        channelRackKey = key;
        channelRows.clear();

        for (auto& c : *state["channels"].getArray())
            addAndMakeVisible (channelRows.add (new ChannelRow (dispatcher, c, strips, pluginInstruments, playStep,
                                                                session.getAudioFileWildcard(), state["audio_inputs"])));
    }

    mixer.update (state);
    timeline.update (state);

    // The clip may have been deleted (or undone away) while open.
    if (pianoRoll.getClip().isNotEmpty() && ! pianoRoll.update())
        closePianoRoll();

    updatePianoRollTitle();
    updateTransportControls (state);
    resized();
}

void MainComponent::openPianoRoll (const juce::String& clipId)
{
    pianoRoll.setClip (clipId);
    updatePianoRollTitle();
    resized();
    pianoRoll.grabKeyboardFocus();
}

void MainComponent::closePianoRoll()
{
    pianoRoll.clear();
    resized();
}

void MainComponent::updatePianoRollTitle()
{
    auto clip = session.findClip (pianoRoll.getClip());

    if (clip == nullptr)
        return;

    auto title = "Piano roll: " + clip->getName();

    if (auto track = clip->getTrack())
        title << " on " << track->getName();

    pianoRollHeader.setText (title, juce::dontSendNotification);
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
        b->setBounds (toolbar.removeFromLeft (b == &saveAsButton ? 64 : 50));
        toolbar.removeFromLeft (4);
    }

    toolbar.removeFromLeft (10);

    for (auto* b : { &addButton, &returnButton, &playButton, &recordButton, &undoButton, &redoButton })
    {
        b->setBounds (toolbar.removeFromLeft (b == &addButton ? 104 : b == &returnButton ? 34 : b == &recordButton ? 44 : 52));
        toolbar.removeFromLeft (4);
    }

    toolbar.removeFromLeft (8);
    tempo.setBounds (toolbar.removeFromLeft (118));
    toolbar.removeFromLeft (6);
    timeSignature.setBounds (toolbar.removeFromLeft (64));
    toolbar.removeFromLeft (8);
    positionDisplay.setBounds (toolbar.removeFromLeft (80));
    toolbar.removeFromLeft (8);
    loopButton.setBounds (toolbar.removeFromLeft (62));
    metronomeButton.setBounds (toolbar.removeFromLeft (92));

    shortcutsButton.setBounds (toolbar.removeFromRight (28));
    toolbar.removeFromRight (4);
    settingsButton.setBounds (toolbar.removeFromRight (64));
    toolbar.removeFromRight (4);
    historyButton.setBounds (toolbar.removeFromRight (60));
    toolbar.removeFromRight (4);
    pluginsButton.setBounds (toolbar.removeFromRight (58));
    toolbar.removeFromRight (8);
    consoleButton.setBounds (toolbar.removeFromRight (78));
    keysButton.setBounds (toolbar.removeFromRight (58));
    r.removeFromTop (10);

    if (keyboard.isVisible())
    {
        keyboard.setBounds (r.removeFromBottom (70));

        // Spread the 50 white keys from C0 to C7 across the window.
        keyboard.setKeyWidth ((float) keyboard.getWidth() / 50.0f);
        r.removeFromBottom (10);
    }

    if (consoleButton.getToggleState())
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
        r.removeFromBottom (10);
    }

    // The AI chat runs down the right, the browser down the left.
    if (chatPanel.isVisible())
    {
        chatPanel.setBounds (r.removeFromRight (juce::jlimit (300, 420, r.getWidth() / 4)));
        r.removeFromRight (10);
    }

    if (browserPanel.isVisible())
    {
        browserPanel.setBounds (r.removeFromLeft (230));
        r.removeFromLeft (10);
    }

    // The timeline gets a bit under half; the rack and mixer share the rest.
    auto timelineTop = r.removeFromTop (24);
    browserButton.setBounds (timelineTop.removeFromLeft (80));
    chatButton.setBounds (timelineTop.removeFromLeft (80));
    timelineTop.removeFromLeft (6);
    audioButton.setBounds (timelineTop.removeFromRight (70).reduced (0, 1));
    timelineTop.removeFromRight (6);
    exportButton.setBounds (timelineTop.removeFromRight (64).reduced (0, 1));
    timelineTop.removeFromRight (6);
    cpuMeter.setBounds (timelineTop.removeFromRight (70).reduced (0, 2));
    timelineTop.removeFromRight (6);
    timelineHeader.setBounds (timelineTop.removeFromLeft (120));
    timelineHint.setBounds (timelineTop);
    timeline.setBounds (r.removeFromTop (juce::jmax (140, (r.getHeight() - 10) * 9 / 20)));
    r.removeFromTop (10);

    // Below it, either the piano roll or the channel rack and mixer.
    auto rollOpen = pianoRoll.getClip().isNotEmpty();

    for (auto* c : std::initializer_list<juce::Component*> { &pianoRollHeader, &pianoRollHint, &snapLabel, &snapBox,
                                                             &closePianoRollButton, &quantizeButton, &pianoRoll })
        c->setVisible (rollOpen);

    for (auto* c : std::initializer_list<juce::Component*> { &channelsHeader, &mixerHeader, &addBusButton, &mixer })
        c->setVisible (! rollOpen);

    for (auto* row : channelRows)
        row->setVisible (! rollOpen);

    rackArea = {};

    if (rollOpen)
    {
        auto top = r.removeFromTop (24);
        pianoRollHeader.setBounds (top.removeFromLeft (320));
        closePianoRollButton.setBounds (top.removeFromRight (64));
        top.removeFromRight (10);
        snapBox.setBounds (top.removeFromRight (70));
        snapLabel.setBounds (top.removeFromRight (44));
        top.removeFromRight (8);
        quantizeButton.setBounds (top.removeFromRight (80));
        top.removeFromRight (10);
        pianoRollHint.setBounds (top);
        r.removeFromTop (4);
        pianoRoll.setBounds (r);
        return;
    }

    auto left = r.removeFromLeft (juce::jmax (600, r.getWidth() * 9 / 20)).withTrimmedRight (8);
    auto right = r.withTrimmedLeft (8);

    channelsHeader.setBounds (left.removeFromTop (24));
    rackArea = left;

    for (auto* row : channelRows)
        row->setBounds (left.removeFromTop (60));

    auto mixerTop = right.removeFromTop (24);
    addBusButton.setBounds (mixerTop.removeFromRight (70).reduced (0, 1));
    mixerHeader.setBounds (mixerTop);
    mixer.setBounds (right);
}

} // namespace ng
