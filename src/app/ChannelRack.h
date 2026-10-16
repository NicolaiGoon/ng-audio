#pragma once

#include "core/CommandDispatcher.h"

namespace ng
{

/**
    The channel rack: a row per channel with its name, colour, instrument, the strip it plays
    into and its step pattern. Click a step to switch it, or drag across steps to paint them
    on (or off, if the first was on); right-click a step to turn it off.

    The rack scrolls when there are more channels than fit. Drop instruments, presets, samples
    or kit folders on it (from the browser or the desktop) for a new channel, or on a channel
    to change what it plays.
*/
class ChannelRack  : public juce::Component,
                     public juce::DragAndDropTarget,
                     public juce::FileDragAndDropTarget
{
public:
    ChannelRack (Session&, CommandDispatcher&);
    ~ChannelRack() override;

    /** Rebuilds the rows if the channels (or what they can pick from) changed. */
    void update (const juce::var& state);

    /** Drops every row, e.g. before the project is replaced. */
    void clear();

    /** The 16th note under the playhead, or -1 when stopped; the rows outline it. */
    void setPlayStep (int step);

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails&) override     { setHighlight (true); }
    void itemDragExit (const SourceDetails&) override      { setHighlight (false); }
    void itemDropped (const SourceDetails&) override;
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int, int) override  { setHighlight (true); }
    void fileDragExit (const juce::StringArray&) override             { setHighlight (false); }
    void filesDropped (const juce::StringArray&, int x, int y) override;

    static constexpr int rowHeight = 60;

private:
    juce::Array<juce::var> itemsForFiles (const juce::StringArray&) const;
    void setHighlight (bool);

    Session& session;
    CommandDispatcher& dispatcher;

    juce::Viewport viewport;
    juce::Component holder;
    juce::OwnedArray<juce::Component> rows;
    juce::String key;
    int playStep = -1;
    bool highlight = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelRack)
};

} // namespace ng
