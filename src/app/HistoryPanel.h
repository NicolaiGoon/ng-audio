#pragma once

#include "core/CommandDispatcher.h"

namespace ng
{

/**
    The undo history: every change since the project was opened, oldest at the top, with the
    ones that were undone (and can be redone) greyed out below. Clicking a step undoes or
    redoes back to just after it; "Opened" goes back to the start.
*/
class HistoryPanel  : public juce::Component,
                      private juce::ListBoxModel,
                      private juce::Timer
{
public:
    HistoryPanel (Session&, CommandDispatcher&);
    ~HistoryPanel() override;

    void resized() override;

    /** "create_track" reads as "Create track"; names that are already words stay as they are. */
    static juce::String getFriendlyName (const juce::String& undoDescription);

private:
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void timerCallback() override;
    void refresh();

    Session& session;
    CommandDispatcher& dispatcher;
    juce::StringArray steps;    // oldest first
    int done = 0;               // how many of them are applied
    juce::ListBox list { {}, this };
    juce::Label hint;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HistoryPanel)
};

} // namespace ng
