#include "BrowserPanel.h"
#include "Widgets.h"

namespace ng
{
namespace browser
{
    namespace
    {
        juce::var item (std::initializer_list<std::pair<const char*, juce::var>> props)
        {
            return makeObject (props);
        }

        juce::String typeOf (const juce::var& item)          { return item["type"].toString(); }

        bool isInstrumentPreset (const juce::var& item)
        {
            return typeOf (item) == "preset" && item["kind"] == "instrument";
        }

        /** Runs drop commands as one undo step; if one fails, takes back what the others did. */
        struct DropRun
        {
            DropRun (CommandDispatcher& d, const juce::String& label) : dispatcher (d)   { dispatcher.beginGesture (label); }

            ~DropRun()
            {
                dispatcher.endGesture();

                if (error.isNotEmpty() && anyDone)
                    dispatcher.undo();
            }

            /** Runs a command unless an earlier one failed; returns its result value, or void. */
            juce::var run (const juce::String& command, const juce::var& params)
            {
                if (error.isNotEmpty())
                    return {};

                auto result = dispatcher.execute (command, params);

                if (! result.ok)
                {
                    error = result.error;
                    return {};
                }

                anyDone = true;
                return result.value.isVoid() ? juce::var (true) : result.value;
            }

            CommandDispatcher& dispatcher;
            juce::String error;
            bool anyDone = false;
        };
    }

    juce::var instrumentItem (const juce::String& id, const juce::String& name)
    {
        return item ({ { "type", "instrument" }, { "id", id }, { "name", name } });
    }

    juce::var effectItem (const juce::String& id, const juce::String& name)
    {
        return item ({ { "type", "effect" }, { "id", id }, { "name", name } });
    }

    juce::var presetItem (const juce::String& path, const juce::String& name, const juce::String& kind)
    {
        return item ({ { "type", "preset" }, { "path", path }, { "name", name }, { "kind", kind } });
    }

    juce::var sampleItem (const juce::File& file)
    {
        return item ({ { "type", "sample" }, { "path", file.getFullPathName() }, { "name", file.getFileNameWithoutExtension() } });
    }

    juce::var folderItem (const juce::File& folder)
    {
        return item ({ { "type", "folder" }, { "path", folder.getFullPathName() }, { "name", folder.getFileName() } });
    }

    juce::var itemForFile (const juce::File& file, const juce::String& audioWildcard)
    {
        if (file.isDirectory())
            return folderItem (file);

        for (auto& pattern : juce::StringArray::fromTokens (audioWildcard, ";", {}))
            if (file.getFileName().matchesWildcard (pattern.trim(), true))
                return sampleItem (file);

        return {};
    }

    bool canDropOnRack (const juce::var& item)
    {
        auto type = typeOf (item);
        return type == "instrument" || type == "sample" || type == "folder" || isInstrumentPreset (item);
    }

    bool canDropOnChannel (const juce::var& item, bool channelIsSampler)
    {
        auto type = typeOf (item);

        if (type == "sample" || type == "folder")
            return channelIsSampler;

        return type == "instrument" || isInstrumentPreset (item);
    }

    bool canDropOnStrip (const juce::var& item)
    {
        return typeOf (item) == "effect" || (typeOf (item) == "preset" && item["kind"] == "effect");
    }

    bool canDropOnTimeline (const juce::var& item)
    {
        return typeOf (item) == "sample" || typeOf (item) == "folder";
    }

    juce::String dropOnRack (CommandDispatcher& dispatcher, const juce::Array<juce::var>& items)
    {
        DropRun drop (dispatcher, "Add from browser");
        juce::Array<juce::var> samples;

        for (auto& it : items)
        {
            auto type = typeOf (it);

            if (type == "sample")
            {
                samples.add (it);
            }
            else if (type == "instrument")
            {
                drop.run ("create_track", makeObject ({ { "name", it["name"] }, { "instrument", it["id"] } }));
            }
            else if (isInstrumentPreset (it))
            {
                auto channel = drop.run ("create_track", makeObject ({ { "name", it["name"] }, { "instrument", "none" } }))["channel"];
                drop.run ("load_preset", makeObject ({ { "preset", it["path"] }, { "channel", channel } }));
            }
            else if (type == "folder")
            {
                auto channel = drop.run ("create_track", makeObject ({ { "name", it["name"] }, { "instrument", "sampler" } }))["channel"];
                drop.run ("load_drum_kit", makeObject ({ { "channel", channel }, { "folder", it["path"] } }));
            }
        }

        // Several samples dropped together go in one sampler, named for their folder.
        if (! samples.isEmpty())
        {
            auto name = samples.size() == 1 ? samples.getFirst()["name"].toString()
                                            : juce::File (samples.getFirst()["path"].toString()).getParentDirectory().getFileName();
            auto channel = drop.run ("create_track", makeObject ({ { "name", name }, { "instrument", "sampler" } }))["channel"];

            for (auto& s : samples)
                drop.run ("add_sample", makeObject ({ { "channel", channel }, { "file", s["path"] } }));
        }

        return drop.error;
    }

    juce::String dropOnChannel (CommandDispatcher& dispatcher, const juce::String& channelId, const juce::Array<juce::var>& items)
    {
        DropRun drop (dispatcher, "Drop on channel");

        for (auto& it : items)
        {
            auto type = typeOf (it);

            if (type == "instrument")
                drop.run ("set_instrument", makeObject ({ { "channel", channelId }, { "instrument", it["id"] } }));
            else if (isInstrumentPreset (it))
                drop.run ("load_preset", makeObject ({ { "preset", it["path"] }, { "channel", channelId } }));
            else if (type == "sample")
                drop.run ("add_sample", makeObject ({ { "channel", channelId }, { "file", it["path"] } }));
            else if (type == "folder")
                drop.run ("load_drum_kit", makeObject ({ { "channel", channelId }, { "folder", it["path"] }, { "replace", false } }));
        }

        return drop.error;
    }

    juce::String dropOnStrip (CommandDispatcher& dispatcher, const juce::String& stripId, const juce::Array<juce::var>& items)
    {
        DropRun drop (dispatcher, "Add effect");

        for (auto& it : items)
        {
            if (typeOf (it) == "effect")
                drop.run ("add_effect", makeObject ({ { "strip", stripId }, { "effect", it["id"] } }));
            else if (typeOf (it) == "preset" && it["kind"] == "effect")
                drop.run ("load_preset", makeObject ({ { "preset", it["path"] }, { "strip", stripId } }));
        }

        return drop.error;
    }

    juce::String dropOnTimeline (CommandDispatcher& dispatcher, const juce::String& audioChannelId, double beat,
                                 const juce::Array<juce::var>& items, const juce::String& audioWildcard)
    {
        // The files, with each folder's audio files (not its subfolders) in name order.
        constexpr int maxFiles = 32;
        juce::Array<juce::File> files;

        for (auto& it : items)
        {
            juce::File file (it["path"].toString());

            if (typeOf (it) == "sample")
            {
                files.add (file);
            }
            else if (typeOf (it) == "folder")
            {
                auto inFolder = file.findChildFiles (juce::File::findFiles, false, audioWildcard);
                inFolder.sort();
                files.addArray (inFolder);
            }
        }

        if (files.isEmpty())
            return "There are no audio files there.";

        if (files.size() > maxFiles)
            return "That's " + juce::String (files.size()) + " audio files; drop up to " + juce::String (maxFiles) + " at a time.";

        DropRun drop (dispatcher, "Import audio");
        auto start = beat;

        for (auto& file : files)
        {
            auto params = makeObject ({ { "file", file.getFullPathName() }, { "start_beat", start } });

            if (audioChannelId.isNotEmpty())
                params.getDynamicObject()->setProperty ("channel", audioChannelId);

            auto clip = drop.run ("import_audio", params);

            if (audioChannelId.isNotEmpty() && clip.isObject())
                start = static_cast<double> (clip["start_beat"]) + static_cast<double> (clip["length_beats"]);
        }

        return drop.error;
    }

    void showDropError (juce::Component* parent, const juce::String& error)
    {
        if (error.isNotEmpty())
            juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsOk (juce::MessageBoxIconType::WarningIcon,
                                                                                 "Couldn't add that", error, {}, parent),
                                          nullptr);
    }

    void savePresetWithName (CommandDispatcher& dispatcher, juce::Component* parent, const juce::String& suggestedName,
                             const juce::var& params, std::function<void()> onSaved)
    {
        auto* window = new juce::AlertWindow ("Save preset", "Name the preset. It goes in the preset folder, "
                                                             "so you can load it from the browser in any project.",
                                              juce::MessageBoxIconType::NoIcon, parent);
        window->addTextEditor ("name", suggestedName);
        window->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
        window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

        window->enterModalState (true, juce::ModalCallbackFunction::create ([window, &dispatcher, params, parent, onSaved] (int button)
        {
            auto name = window->getTextEditorContents ("name").trim();

            if (button != 1 || name.isEmpty())
                return;

            auto p = params.clone();
            p.getDynamicObject()->setProperty ("name", name);
            auto result = dispatcher.execute ("save_preset", p);

            if (! result.ok)
                juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsOk (juce::MessageBoxIconType::WarningIcon,
                                                                                     "Couldn't save the preset", result.error, {}, parent),
                                              nullptr);
            else if (onSaved != nullptr)
                onSaved();
        }), true);
    }
}

//==============================================================================
/** A row in the tree. Sections, plugin groups and folders fill their rows when opened, so a
    big sample folder is only read when you look in it. */
class BrowserPanel::Node  : public juce::TreeViewItem
{
public:
    enum class Style { section, group, folder, item, hint };

    Node (BrowserPanel& o, juce::String t, Style s, juce::var i = {})
        : owner (o), text (std::move (t)), style (s), item (std::move (i)) {}

    std::function<void (Node&)> populate;
    bool isSampleFolder = false;

    Node& add (const juce::String& t, Style s, const juce::var& i = {})
    {
        auto* node = new Node (owner, t, s, i);
        addSubItem (node);
        return *node;
    }

    bool mightContainSubItems() override        { return populate != nullptr || getNumSubItems() > 0; }
    juce::String getUniqueName() const override { return text; }
    int getItemHeight() const override          { return style == Style::section ? 26 : 22; }
    juce::var getDragSourceDescription() override   { return item; }
    bool canBeSelected() const override         { return style != Style::hint; }

    void itemOpennessChanged (bool isNowOpen) override
    {
        if (isNowOpen && populate != nullptr)
        {
            clearSubItems();
            populate (*this);
        }
    }

    void paintItem (juce::Graphics& g, int width, int height) override
    {
        if (isSelected())
            g.fillAll (juce::Colour (0xff2f4f6f));

        auto colour = style == Style::section ? juce::Colours::grey
                    : style == Style::hint    ? juce::Colours::grey.darker (0.3f)
                    : style == Style::group   ? juce::Colours::white.withAlpha (0.85f)
                                              : juce::Colours::white;
        g.setColour (colour);
        g.setFont (style == Style::section ? juce::FontOptions (12.0f, juce::Font::bold)
                                           : juce::FontOptions (style == Style::hint ? 12.0f : 13.5f));

        auto r = juce::Rectangle<int> (2, 0, width - 4, height);

        // A small marker shows what can be dragged: a dot for an item, a square for a folder.
        if (style == Style::item || style == Style::folder)
        {
            auto mark = r.removeFromLeft (12).toFloat().withSizeKeepingCentre (6.0f, 6.0f);
            g.setColour (style == Style::folder ? juce::Colour (0xffd0a040) : markColour());

            if (style == Style::folder)
                g.fillRect (mark);
            else
                g.fillEllipse (mark);

            g.setColour (colour);
        }

        g.drawText (style == Style::section ? text.toUpperCase() : text, r, juce::Justification::centredLeft, true);
    }

    void itemDoubleClicked (const juce::MouseEvent&) override
    {
        if (item.isObject())
            owner.activate (item);
    }

    void itemClicked (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
            owner.showMenu (*this);
    }

    juce::String getTooltip() override
    {
        auto type = item["type"].toString();

        if (type == "instrument")   return "Drag onto the channel rack for a new channel, or onto a channel to swap its instrument";
        if (type == "effect")       return "Drag onto a mixer strip to add it";
        if (type == "sample")       return item["path"].toString() + "\nDouble-click: a loop goes on the timeline, a one-shot gets a sampler channel."
                                                                     "\nOr drag it onto the timeline, the rack, or a sampler channel";
        if (type == "folder")       return item["path"].toString() + "\nDrag onto the rack to load it as a drum kit";
        if (type == "preset")       return item["kind"] == "instrument" ? "Drag onto a channel, or onto the rack for a new channel"
                                                                        : "Drag onto a mixer strip";
        return {};
    }

    /** Fills a folder's row with its folders (each filled when opened, and read again each
        time, so new files show up) and then its audio files. */
    static void fillFolder (Node& node, const juce::File& folder, const juce::String& wildcard)
    {
        auto byName = [] (const juce::File& a, const juce::File& b) { return a.getFileName().compareNatural (b.getFileName()) < 0; };

        auto subfolders = folder.findChildFiles (juce::File::findDirectories, false);
        std::sort (subfolders.begin(), subfolders.end(), byName);

        for (auto& sub : subfolders)
            if (! sub.isHidden())
                node.add (sub.getFileName(), Style::folder, browser::folderItem (sub)).populate
                    = [sub, wildcard] (Node& child) { fillFolder (child, sub, wildcard); };

        auto audio = folder.findChildFiles (juce::File::findFiles, false, wildcard);
        std::sort (audio.begin(), audio.end(), byName);

        for (auto& f : audio)
            node.add (f.getFileName(), Style::item, browser::sampleItem (f));
    }

    BrowserPanel& owner;
    juce::String text;
    Style style;
    juce::var item;

private:
    juce::Colour markColour() const
    {
        auto type = item["type"].toString();

        if (type == "instrument")   return juce::Colour (0xff4fb06f);
        if (type == "effect")       return juce::Colour (0xff3c8ce0);
        if (type == "preset")       return juce::Colour (0xffb070d0);
        return juce::Colour (0xffe0703c);
    }
};

//==============================================================================
BrowserPanel::BrowserPanel (Session& s, CommandDispatcher& d)
    : session (s), dispatcher (d)
{
    title.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    addAndMakeVisible (title);

    search.setTextToShowWhenEmpty ("Search", juce::Colours::grey);
    search.setTooltip ("Find instruments, effects, presets and samples by name");
    search.onTextChange = [this] { startTimer (250); };
    search.onEscapeKey = [this] { search.clear(); refresh(); };
    addAndMakeVisible (search);

    addFolderButton.setTooltip ("Add a folder of samples to the browser");
    addFolderButton.onClick = [this] { addFolder(); };
    addAndMakeVisible (addFolderButton);

    tree.setRootItemVisible (false);
    tree.setDefaultOpenness (false);
    tree.setIndentSize (14);
    tree.setColour (juce::TreeView::backgroundColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (tree);

    session.getEngine().getPluginManager().knownPluginList.addChangeListener (this);
    refresh();
}

BrowserPanel::~BrowserPanel()
{
    session.getEngine().getPluginManager().knownPluginList.removeChangeListener (this);
    tree.setRootItem (nullptr);
}

void BrowserPanel::paint (juce::Graphics& g)
{
    g.setColour (juce::Colour (0xff23272c));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 5.0f);
}

void BrowserPanel::resized()
{
    auto r = getLocalBounds().reduced (6);
    auto top = r.removeFromTop (24);
    addFolderButton.setBounds (top.removeFromRight (70));
    title.setBounds (top);
    r.removeFromTop (4);
    search.setBounds (r.removeFromTop (24));
    r.removeFromTop (6);
    tree.setBounds (r);
}

void BrowserPanel::timerCallback()
{
    refresh();
}

void BrowserPanel::refresh()
{
    stopTimer();
    auto openness = tree.getOpennessState (true);
    auto query = search.getText().trim().toLowerCase();
    auto words = juce::StringArray::fromTokens (query, true);
    words.removeEmptyStrings();

    auto matches = [words] (const juce::String& name)
    {
        auto lower = name.toLowerCase();
        return std::all_of (words.begin(), words.end(), [&] (const juce::String& w) { return lower.contains (w); });
    };

    auto searching = ! words.isEmpty();
    tree.setRootItem (nullptr);
    root = std::make_unique<Node> (*this, "root", Node::Style::group);

    auto& known = session.getEngine().getPluginManager().knownPluginList;
    auto types = known.getTypes();
    std::sort (types.begin(), types.end(), [] (auto& a, auto& b) { return a.name.compareNatural (b.name) < 0; });

    // Instruments: the built-in ones, then the scanned plugins.
    auto& instruments = root->add ("Instruments", Node::Style::section);
    instruments.populate = [types, matches] (Node& n)
    {
        for (auto [id, name] : { std::pair<const char*, const char*> { "4osc", "4OSC synth" }, { "sampler", "Sampler" } })
            if (matches (name))
                n.add (name, Node::Style::item, browser::instrumentItem (id, name));

        for (auto& d : types)
            if (d.isInstrument && matches (d.name))
                n.add (d.name, Node::Style::item, browser::instrumentItem (d.createIdentifierString(), d.name));
    };

    auto& effects = root->add ("Effects", Node::Style::section);
    effects.populate = [types, matches] (Node& n)
    {
        for (auto& e : Session::getBuiltInEffects())
            if (matches (e.name))
                n.add (e.name, Node::Style::item, browser::effectItem (e.id, e.name));

        for (auto& d : types)
            if (! d.isInstrument && matches (d.name))
                n.add (d.name, Node::Style::item, browser::effectItem (d.createIdentifierString(), d.name));
    };

    // Presets, grouped by the plugin they're for.
    auto& presets = root->add ("Presets", Node::Style::section);
    presets.populate = [this, matches, searching] (Node& n)
    {
        auto list = dispatcher.execute ("list_presets").value;
        Node* group = nullptr;
        juce::String groupName;

        if (list.isArray())
        {
            for (auto& p : *list.getArray())
            {
                if (! matches (p["name"].toString() + " " + p["plugin_name"].toString()))
                    continue;

                if (group == nullptr || p["plugin_name"].toString() != groupName)
                {
                    groupName = p["plugin_name"].toString();
                    group = &n.add (groupName, Node::Style::group);
                    group->setOpen (searching);
                }

                group->add (p["name"].toString(), Node::Style::item,
                            browser::presetItem (p["path"].toString(), p["name"].toString(), p["kind"].toString()));
            }
        }

        if (n.getNumSubItems() == 0 && ! searching)
            n.add ("Right-click an effect, or a channel's name, to save a preset", Node::Style::hint);
    };

    // Samples: each folder, read when opened; while searching, the files that match.
    auto& samples = root->add ("Samples", Node::Style::section);
    samples.populate = [this, query, searching] (Node& n)
    {
        auto folders = dispatcher.execute ("get_library").value["sample_folders"];

        if (folders.size() == 0)
        {
            n.add ("Add a folder of samples with + Folder", Node::Style::hint);
            return;
        }

        if (searching)
        {
            auto found = dispatcher.execute ("find_samples", makeObject ({ { "query", query }, { "limit", 200 } })).value;

            for (auto& s : *found["samples"].getArray())
                n.add (juce::File (s["path"].toString()).getFileName(), Node::Style::item, browser::sampleItem (juce::File (s["path"].toString())));

            if (static_cast<bool> (found["truncated"]))
                n.add ("More found: type more to narrow it down", Node::Style::hint);

            return;
        }

        auto wildcard = session.getAudioFileWildcard();

        for (auto& f : *folders.getArray())
        {
            juce::File folder (f.toString());
            auto& node = n.add (folder.getFileName(), Node::Style::folder, browser::folderItem (folder));
            node.isSampleFolder = true;
            node.populate = [folder, wildcard] (Node& c) { Node::fillFolder (c, folder, wildcard); };
        }
    };

    tree.setRootItem (root.get());

    if (searching)
    {
        for (auto* section : { &instruments, &effects, &presets, &samples })
            section->setOpen (true);

        // Only the sections with something found.
        for (int i = root->getNumSubItems(); --i >= 0;)
            if (root->getSubItem (i)->getNumSubItems() == 0)
                root->removeSubItem (i);
    }
    else if (openness != nullptr)
    {
        tree.restoreOpennessState (*openness, true);
    }
    else
    {
        instruments.setOpen (true);
        samples.setOpen (true);
    }
}

void BrowserPanel::addFolder()
{
    chooser = std::make_unique<juce::FileChooser> ("Add a sample folder", juce::File::getSpecialLocation (juce::File::userMusicDirectory));
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this] (const juce::FileChooser& c)
    {
        auto folder = c.getResult();

        if (! folder.isDirectory())
            return;

        auto folders = dispatcher.execute ("get_library").value["sample_folders"].clone();

        if (! folders.isArray())
            folders = juce::Array<juce::var>();

        folders.append (folder.getFullPathName());
        auto result = dispatcher.execute ("set_library", makeObject ({ { "sample_folders", folders } }));
        browser::showDropError (this, result.error);
        refresh();
    });
}

void BrowserPanel::removeFolder (const juce::String& path)
{
    juce::Array<juce::var> kept;

    for (auto& f : *dispatcher.execute ("get_library").value["sample_folders"].getArray())
        if (f.toString() != path)
            kept.add (f);

    dispatcher.execute ("set_library", makeObject ({ { "sample_folders", kept } }));
    refresh();
}

bool BrowserPanel::isLoop (const juce::File& file) const
{
    // Loops are named and filed as loops, or say their tempo; failing that, anything over a
    // couple of bars at 120 is one. Shorter sounds are one-shots, for a sampler channel.
    if (file.getFullPathName().containsIgnoreCase ("loop") || file.getFileName().containsIgnoreCase ("bpm"))
        return true;

    te::AudioFile audio (session.getEngine(), file);
    return audio.isValid() && audio.getLength() >= 4.0;
}

void BrowserPanel::activate (const juce::var& item)
{
    // A loop goes on the timeline as a clip, on its own audio channel, and opens in the sample
    // editor; if it's there already, that clip opens.
    if (browser::typeOf (item) == "sample" && isLoop (juce::File (item["path"].toString())))
    {
        juce::File file (item["path"].toString());

        for (auto channel : session.getChannels())
            for (auto clip : channel->getClips())
                if (auto wave = dynamic_cast<te::WaveAudioClip*> (clip); wave != nullptr && wave->getOriginalFile() == file)
                {
                    if (onOpenClip != nullptr)
                        onOpenClip (wave->itemID.toString());
                    return;
                }

        // At the start of the bar the playhead is in.
        auto& edit = session.getEdit();
        auto& tempo = edit.tempoSequence;
        auto bar = tempo.toBarsAndBeats (edit.getTransport().getPosition()).bars;
        auto beat = std::max (0.0, tempo.toBeats (te::tempo::BarsAndBeats { bar, {} }).inBeats());

        auto result = dispatcher.execute ("import_audio", makeObject ({ { "file", file.getFullPathName() }, { "start_beat", beat } }));

        if (! result.ok)
            browser::showDropError (this, result.error);
        else if (onOpenClip != nullptr)
            onOpenClip (result.value["clip"].toString());

        return;
    }

    if (browser::canDropOnRack (item))
        browser::showDropError (this, browser::dropOnRack (dispatcher, { item }));
}

void BrowserPanel::showMenu (Node& node)
{
    juce::PopupMenu menu;
    auto item = node.item;
    auto path = item["path"].toString();

    if (browser::canDropOnRack (item))
        menu.addItem ("New channel from this", [this, item] { activate (item); });

    if (node.isSampleFolder)
        menu.addItem ("Remove from the browser", [this, path] { removeFolder (path); });

    if (path.isNotEmpty())
        menu.addItem ("Show in folder", [path] { juce::File (path).revealToUser(); });

    if (item["type"] == "preset")
        menu.addItem ("Delete preset...", [this, path, name = item["name"].toString()]
        {
            juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsOkCancel (juce::MessageBoxIconType::QuestionIcon,
                                                                                       "Delete preset", "Delete the preset '" + name + "'?",
                                                                                       "Delete", "Cancel", this),
                                          [safeThis = juce::Component::SafePointer<BrowserPanel> (this), path] (int button)
                                          {
                                              if (button == 1 && safeThis != nullptr)
                                              {
                                                  juce::File (path).deleteFile();
                                                  safeThis->refresh();
                                              }
                                          });
        });

    menu.addSeparator();
    menu.addItem ("Refresh", [this] { refresh(); });
    menu.showMenuAsync (juce::PopupMenu::Options());
}

} // namespace ng
