#pragma once

#include "core/CommandDispatcher.h"

namespace ng
{

/**
    What AI clients (and the console) did to the project, newest first: each command or batch,
    who sent it, and whether it worked. Undo takes back the newest one; an older entry can be
    undone once everything after it has been.
*/
class CommandLogPanel  : public juce::Component,
                         private juce::ListBoxModel
{
public:
    explicit CommandLogPanel (CommandDispatcher&);

    void add (const CommandDispatcher::LogEntry&);

    /** Re-checks which entries can be undone, e.g. after any change to the project. */
    void refresh();

    int getNumEntries() const noexcept      { return static_cast<int> (entries.size()); }

    void resized() override;

private:
    class Row;

    int getNumRows() override       { return getNumEntries(); }
    void paintListBoxItem (int, juce::Graphics&, int, int, bool) override {}
    juce::Component* refreshComponentForRow (int row, bool selected, juce::Component* existing) override;

    const CommandDispatcher::LogEntry& getEntryForRow (int row) const   { return entries[entries.size() - 1 - static_cast<size_t> (row)]; }

    CommandDispatcher& dispatcher;
    std::vector<CommandDispatcher::LogEntry> entries;
    juce::Label header { {}, "AI command log" }, empty;
    juce::TextButton clearButton { "Clear" };
    juce::ListBox list;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CommandLogPanel)
};

} // namespace ng
