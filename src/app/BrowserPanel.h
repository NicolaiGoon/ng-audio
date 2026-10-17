#pragma once

#include "core/CommandDispatcher.h"

namespace ng
{

/**
    What the browser hands out when something is dragged from it (or a file is dragged in from
    the desktop): a var object with a "type" of instrument, effect, preset, sample or folder,
    plus the id, path, name and (for a preset) kind that the commands need.

    The rack, the channel rows and the mixer strips use these to accept drops, and the
    browser's double-click uses dropOnRack. Each drop runs commands as one undo step and
    returns an error, or an empty string.
*/
namespace browser
{
    juce::var instrumentItem (const juce::String& id, const juce::String& name);
    juce::var effectItem (const juce::String& id, const juce::String& name);
    juce::var presetItem (const juce::String& path, const juce::String& name, const juce::String& kind);
    juce::var sampleItem (const juce::File&);
    juce::var folderItem (const juce::File&);

    /** A file or folder dragged in from outside the app, as an item; void if it's neither an
        audio file the sampler reads nor a folder. */
    juce::var itemForFile (const juce::File&, const juce::String& audioWildcard);

    /** Where an item can go. A sample or kit folder goes on a channel only if it plays the sampler. */
    bool canDropOnRack (const juce::var& item);
    bool canDropOnChannel (const juce::var& item, bool channelIsSampler);
    bool canDropOnStrip (const juce::var& item);
    bool canDropOnTimeline (const juce::var& item);

    /** New channels from instruments, instrument presets, samples (several samples make one
        sampler channel) and kit folders. */
    juce::String dropOnRack (CommandDispatcher&, const juce::Array<juce::var>& items);
    juce::String dropOnChannel (CommandDispatcher&, const juce::String& channelId, const juce::Array<juce::var>& items);
    juce::String dropOnStrip (CommandDispatcher&, const juce::String& stripId, const juce::Array<juce::var>& items);

    /** Samples (and the audio files in kit folders) as audio clips from the beat: one after another
        on the audio channel, or with no channel, each on a new audio channel. */
    juce::String dropOnTimeline (CommandDispatcher&, const juce::String& audioChannelId, double beat,
                                 const juce::Array<juce::var>& items, const juce::String& audioWildcard);

    /** Asks for a preset name, then runs save_preset with it added to the params. */
    void savePresetWithName (CommandDispatcher&, juce::Component* parent, const juce::String& suggestedName,
                             const juce::var& params, std::function<void()> onSaved = {});

    /** Shows a drop's error, if there was one. */
    void showDropError (juce::Component* parent, const juce::String& error);
}

/**
    The browser: instruments, effects, presets and sample folders in a tree, with a search
    box. Drag an item onto the channel rack, a channel or a mixer strip; double-click one to
    make a new channel from it (a loop goes on the timeline and opens in the sample editor). "+ Folder" adds a sample folder (right-click one to remove it).
    The folders and presets are the app's (set_library), so they're the same in every project.
*/
class BrowserPanel  : public juce::Component,
                      private juce::ChangeListener,
                      private juce::Timer
{
public:
    BrowserPanel (Session&, CommandDispatcher&);
    ~BrowserPanel() override;

    /** Reads the plugins, presets and folders again, keeping what's open. */
    void refresh();

    /** Called with an audio clip to show, after double-clicking a loop. */
    std::function<void (const juce::String& clipId)> onOpenClip;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Node;

    void changeListenerCallback (juce::ChangeBroadcaster*) override     { refresh(); }
    void timerCallback() override;
    void addFolder();
    void removeFolder (const juce::String& path);
    void activate (const juce::var& item);
    bool isLoop (const juce::File&) const;
    void showMenu (Node&);

    Session& session;
    CommandDispatcher& dispatcher;

    juce::Label title { {}, "Browser" };
    juce::TextEditor search;
    juce::TextButton addFolderButton { "+ Folder" };
    juce::TreeView tree;
    std::unique_ptr<Node> root;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BrowserPanel)
};

} // namespace ng
