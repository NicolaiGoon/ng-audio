#include "SamplerEditor.h"
#include "Widgets.h"

namespace ng
{
namespace
{
    /** "C1 (36)": a key's name, with middle C as C3 as in the piano roll, and its number. */
    juce::String keyName (int note)
    {
        return juce::MidiMessage::getMidiNoteName (note, true, true, 3) + " (" + juce::String (note) + ")";
    }

    class SamplerEditor  : public te::Plugin::EditorComponent,
                           public juce::FileDragAndDropTarget,
                           private juce::Timer
    {
    public:
        SamplerEditor (te::SamplerPlugin& s, CommandDispatcher& d)
            : sampler (s), dispatcher (d),
              channel (s.getOwnerTrack() != nullptr ? s.getOwnerTrack()->itemID.toString() : juce::String())
        {
            addButton.setButtonText ("Add samples...");
            addButton.setTooltip ("Add audio files, each on the next free key from C1");
            addButton.onClick = [this] { chooseSamples(); };
            addAndMakeVisible (addButton);

            kitButton.setButtonText ("Load kit...");
            kitButton.setTooltip ("Replace the samples with a folder of drum samples, kick on C1, snare on D1 and so on");
            kitButton.onClick = [this] { chooseKit(); };
            addAndMakeVisible (kitButton);

            hint.setText ("Drop audio files or a kit folder here", juce::dontSendNotification);
            hint.setJustificationType (juce::Justification::centredRight);
            hint.setColour (juce::Label::textColourId, juce::Colours::grey);
            addAndMakeVisible (hint);

            for (auto text : { "Sample", "Key", "Level", "One-shot" })
            {
                auto* heading = headings.add (new juce::Label ({}, text));
                heading->setColour (juce::Label::textColourId, juce::Colours::grey);
                heading->setFont (juce::FontOptions (12.0f));
                addAndMakeVisible (heading);
            }

            list.setScrollBarsShown (true, false);
            list.setViewedComponent (&rowHolder, false);
            addAndMakeVisible (list);

            rebuild();
            setSize (560, 360);
            startTimerHz (5);
        }

        bool allowWindowResizing() override                                 { return true; }
        juce::ComponentBoundsConstrainer* getBoundsConstrainer() override   { return &constrainer; }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colour (0xff1e2227));

            if (rows.isEmpty())
            {
                g.setColour (juce::Colours::grey);
                g.setFont (juce::FontOptions (15.0f));
                g.drawFittedText ("No samples yet.\nDrop audio files or a folder of drum samples here,\nor use Add samples or Load kit.",
                                  list.getBounds().reduced (20), juce::Justification::centred, 3);
            }
        }

        void paintOverChildren (juce::Graphics& g) override
        {
            if (isDraggingOver)
            {
                g.setColour (juce::Colour (0xff3c8ce0));
                g.drawRect (getLocalBounds(), 3);
            }
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (10, 8);
            auto top = r.removeFromTop (26);
            addButton.setBounds (top.removeFromLeft (120));
            top.removeFromLeft (6);
            kitButton.setBounds (top.removeFromLeft (100));
            hint.setBounds (top);

            r.removeFromTop (8);
            auto headingRow = r.removeFromTop (18);
            layOutRow (headingRow, headings[0], headings[1], headings[2], headings[3], nullptr);
            list.setBounds (r);
            layOutRows();
        }

        //==============================================================================
        bool isInterestedInFileDrag (const juce::StringArray& files) override
        {
            for (auto& path : files)
                if (juce::File (path).isDirectory() || isAudioFile (juce::File (path)))
                    return true;

            return false;
        }

        void fileDragEnter (const juce::StringArray&, int, int) override   { setDraggingOver (true); }
        void fileDragExit (const juce::StringArray&) override              { setDraggingOver (false); }

        void filesDropped (const juce::StringArray& files, int, int) override
        {
            setDraggingOver (false);
            juce::Array<juce::File> dropped;

            for (auto& path : files)
                dropped.add (juce::File (path));

            addFiles (dropped, false);
        }

    private:
        static constexpr int rowHeight = 30, keyWidth = 120, gainWidth = 150, oneShotWidth = 70, removeWidth = 28;

        struct Row  : public juce::Component
        {
            juce::Label name;
            juce::Slider key, gain;
            juce::ToggleButton oneShot;
            juce::TextButton remove;
            juce::var sample;
        };

        static void layOutRow (juce::Rectangle<int> r, juce::Component* name, juce::Component* key, juce::Component* gain,
                               juce::Component* oneShot, juce::Component* remove)
        {
            auto place = [&r] (juce::Component* c, int width)
            {
                auto area = r.removeFromRight (width);

                if (c != nullptr)
                    c->setBounds (area.reduced (2, 0));
            };

            place (remove, removeWidth);
            place (oneShot, oneShotWidth);
            place (gain, gainWidth);
            place (key, keyWidth);

            if (name != nullptr)
                name->setBounds (r.reduced (2, 0));
        }

        void layOutRows()
        {
            auto width = list.getMaximumVisibleWidth();
            rowHolder.setSize (width, rows.size() * rowHeight);

            for (int i = 0; i < rows.size(); ++i)
            {
                auto* row = rows[i];
                row->setBounds (0, i * rowHeight, width, rowHeight);
                layOutRow (row->getLocalBounds().reduced (0, 3), &row->name, &row->key, &row->gain, &row->oneShot, &row->remove);
            }
        }

        bool isAudioFile (const juce::File& file) const
        {
            return file.existsAsFile() && sampler.engine.getAudioFileFormatManager().readFormatManager.findFormatForFileExtension (file.getFileExtension()) != nullptr;
        }

        void setDraggingOver (bool over)
        {
            isDraggingOver = over;
            repaint();
        }

        //==============================================================================
        /** Adds audio files and kit folders as one undo step, then says what didn't load. */
        void addFiles (const juce::Array<juce::File>& files, bool replaceWithKit)
        {
            juce::StringArray errors;
            dispatcher.beginGesture (replaceWithKit ? "Load kit" : "Add samples");

            for (auto& file : files)
            {
                auto result = file.isDirectory()
                                ? dispatcher.execute ("load_drum_kit", makeObject ({ { "channel", channel }, { "folder", file.getFullPathName() },
                                                                                     { "replace", replaceWithKit } }))
                                : dispatcher.execute ("add_sample", makeObject ({ { "channel", channel }, { "file", file.getFullPathName() } }));

                if (! result.ok)
                    errors.add (result.error);
            }

            dispatcher.endGesture();
            refresh();

            if (! errors.isEmpty())
                juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsOk (juce::MessageBoxIconType::WarningIcon,
                                                                                     "Some samples didn't load",
                                                                                     errors.joinIntoString ("\n"), {}, this),
                                              nullptr);
        }

        void chooseSamples()
        {
            auto wildcard = sampler.engine.getAudioFileFormatManager().readFormatManager.getWildcardForAllFormats();
            chooser = std::make_unique<juce::FileChooser> ("Add samples", lastFolder, wildcard);
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                    | juce::FileBrowserComponent::canSelectMultipleItems,
                                  [this] (const juce::FileChooser& c)
            {
                auto files = c.getResults();

                if (! files.isEmpty())
                {
                    lastFolder = files.getFirst().getParentDirectory();
                    addFiles (files, false);
                }
            });
        }

        void chooseKit()
        {
            chooser = std::make_unique<juce::FileChooser> ("Load a kit folder", lastFolder);
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                  [this] (const juce::FileChooser& c)
            {
                auto folder = c.getResult();

                if (folder.isDirectory())
                {
                    lastFolder = folder.getParentDirectory();
                    addFiles ({ folder }, true);
                }
            });
        }

        void set (const juce::var& sample, const char* key, const juce::var& value)
        {
            dispatcher.execute ("set_sample", makeObject ({ { "channel", channel }, { "sample", sample["index"] }, { key, value } }));
        }

        //==============================================================================
        /** One row per sample. Rebuilt when samples are added, removed or reordered. */
        void rebuild()
        {
            rows.clear();
            auto samples = Session::describeSamples (sampler);

            for (auto& sample : *samples.getArray())
            {
                auto* row = rows.add (new Row());
                row->sample = sample;

                row->name.setEditable (false, true, false);
                row->name.setTooltip (sample["file"].toString() + "\nDouble-click to rename");
                row->name.onTextChange = [this, row] { set (row->sample, "name", row->name.getText()); };

                row->key.setSliderStyle (juce::Slider::IncDecButtons);
                row->key.setRange (0, 127, 1);
                row->key.setIncDecButtonsMode (juce::Slider::incDecButtonsDraggable_Vertical);
                row->key.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 70, 20);
                row->key.textFromValueFunction = [] (double v) { return juce::MidiMessage::getMidiNoteName ((int) v, true, true, 3); };
                row->key.valueFromTextFunction = [] (const juce::String& text) { return (double) noteFromText (text); };
                row->key.onDragStart = [this] { dispatcher.beginGesture ("Change sample key"); };
                row->key.onDragEnd = [this] { dispatcher.endGesture(); };
                row->key.onValueChange = [this, row] { set (row->sample, "note", (int) row->key.getValue()); };

                row->gain.setSliderStyle (juce::Slider::LinearHorizontal);
                row->gain.setRange (-48.0, 12.0, 0.1);
                row->gain.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 20);
                row->gain.setTextValueSuffix (" dB");
                row->gain.setDoubleClickReturnValue (true, 0.0);
                row->gain.onDragStart = [this] { dispatcher.beginGesture ("Change sample level"); };
                row->gain.onDragEnd = [this] { dispatcher.endGesture(); };
                row->gain.onValueChange = [this, row] { set (row->sample, "gain_db", row->gain.getValue()); };

                row->oneShot.setTooltip ("On: plays to its end, as drums do. Off: stops when the note ends");
                row->oneShot.onClick = [this, row] { set (row->sample, "one_shot", row->oneShot.getToggleState()); };

                row->remove.setButtonText (juce::String::fromUTF8 ("\xc3\x97"));
                row->remove.setTooltip ("Remove this sample");
                row->remove.onClick = [this, row]
                {
                    auto toRemove = row->sample["index"];
                    // Rebuilding deletes this row, so let go of it first.
                    juce::MessageManager::callAsync ([safeThis = juce::Component::SafePointer<SamplerEditor> (this), toRemove]
                    {
                        if (safeThis != nullptr)
                        {
                            safeThis->dispatcher.execute ("remove_sample", makeObject ({ { "channel", safeThis->channel },
                                                                                        { "sample", toRemove } }));
                            safeThis->refresh();
                        }
                    });
                };

                for (auto* c : std::initializer_list<juce::Component*> { &row->name, &row->key, &row->gain, &row->oneShot, &row->remove })
                    row->addAndMakeVisible (c);

                rowHolder.addAndMakeVisible (row);
            }

            structure = structureOf (samples);
            update (samples);
            layOutRows();
            repaint();
        }

        /** Shows the current values (after undo, say), leaving alone a control in use. */
        void update (const juce::var& samples)
        {
            for (int i = 0; i < rows.size(); ++i)
            {
                auto* row = rows[i];
                auto sample = samples[i];
                row->sample = sample;

                auto missing = static_cast<bool> (sample["missing"]);
                row->name.setText (sample["name"].toString() + (missing ? " (missing)" : ""), juce::dontSendNotification);
                row->name.setColour (juce::Label::textColourId, missing ? juce::Colour (0xffd0402f) : juce::Colours::white);

                if (! row->key.isMouseButtonDown())
                    row->key.setValue (sample["note"], juce::dontSendNotification);

                auto range = static_cast<int> (sample["min_note"]) != static_cast<int> (sample["max_note"]);
                row->key.setTooltip (range ? "Plays pitched from " + keyName (sample["min_note"]) + " to " + keyName (sample["max_note"])
                                               + ", at its own pitch on " + keyName (sample["note"])
                                           : "Plays on " + keyName (sample["note"]));

                if (! row->gain.isMouseButtonDown())
                    row->gain.setValue (sample["gain_db"], juce::dontSendNotification);

                row->oneShot.setToggleState (sample["one_shot"], juce::dontSendNotification);
            }
        }

        static juce::String structureOf (const juce::var& samples)
        {
            juce::String key;

            for (auto& s : *samples.getArray())
                key << s["file"].toString() << "|";

            return key;
        }

        static int noteFromText (const juce::String& text)
        {
            if (text.containsOnly ("0123456789"))
                return juce::jlimit (0, 127, text.getIntValue());

            for (int n = 0; n < 128; ++n)
                if (juce::MidiMessage::getMidiNoteName (n, true, true, 3).equalsIgnoreCase (text.trim())
                     || juce::MidiMessage::getMidiNoteName (n, false, true, 3).equalsIgnoreCase (text.trim()))
                    return n;

            return 60;
        }

        void refresh()
        {
            auto samples = Session::describeSamples (sampler);

            if (structureOf (samples) != structure && ! isMouseButtonDownAnywhere())
                rebuild();
            else if (samples.size() == rows.size())
                update (samples);
        }

        void timerCallback() override   { refresh(); }

        te::SamplerPlugin& sampler;
        CommandDispatcher& dispatcher;
        juce::String channel;

        juce::TextButton addButton, kitButton;
        juce::Label hint;
        juce::OwnedArray<juce::Label> headings;
        juce::Viewport list;
        juce::Component rowHolder;
        juce::OwnedArray<Row> rows;
        juce::String structure;

        std::unique_ptr<juce::FileChooser> chooser;
        juce::File lastFolder { juce::File::getSpecialLocation (juce::File::userMusicDirectory) };
        bool isDraggingOver = false;

        struct Constrainer  : public juce::ComponentBoundsConstrainer
        {
            Constrainer()   { setMinimumSize (460, 200); }
        } constrainer;
    };
}

std::unique_ptr<te::Plugin::EditorComponent> createSamplerEditor (te::SamplerPlugin& sampler, CommandDispatcher& dispatcher)
{
    return std::make_unique<SamplerEditor> (sampler, dispatcher);
}

} // namespace ng
