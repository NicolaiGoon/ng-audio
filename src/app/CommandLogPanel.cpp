#include "CommandLogPanel.h"

namespace ng
{
namespace
{
    // The log only keeps this many entries; the oldest go first.
    constexpr size_t maxEntries = 500;
}

class CommandLogPanel::Row  : public juce::Component,
                             public juce::SettableTooltipClient
{
public:
    explicit Row (CommandLogPanel& p) : panel (p)
    {
        text.setFont (juce::FontOptions (13.0f));
        text.setMinimumHorizontalScale (0.7f);
        text.setInterceptsMouseClicks (false, false);
        undoButton.onClick = [this] { panel.dispatcher.undo(); };
        addAndMakeVisible (text);
        addAndMakeVisible (undoButton);
    }

    void update (const CommandDispatcher::LogEntry& entry)
    {
        auto what = entry.label;

        if (entry.commands.size() > 1 || (entry.commands.size() == 1 && entry.commands[0] != entry.label))
            what << " (" << entry.commands.size() << (entry.commands.size() == 1 ? " command)" : " commands)");

        auto line = entry.time.formatted ("%H:%M:%S") + "  " + entry.source + "  " + what;

        if (! entry.ok)
            line << ": " << entry.error;

        text.setText (line, juce::dontSendNotification);
        text.setColour (juce::Label::textColourId, entry.ok ? juce::Colours::white.withAlpha (0.85f) : juce::Colours::orangered);
        setTooltip (entry.ok ? entry.commands.joinIntoString (", ") : entry.error);

        auto latest = panel.dispatcher.isLatestUndoStep (entry);
        undoButton.setVisible (entry.madeUndoStep);
        undoButton.setEnabled (latest);
        undoButton.setTooltip (latest ? "Undo this, as one step" : "Undo what came after it first (or it's been undone)");
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (4, 2);
        undoButton.setBounds (r.removeFromRight (56));
        r.removeFromRight (6);
        text.setBounds (r);
    }

private:
    CommandLogPanel& panel;
    juce::Label text;
    juce::TextButton undoButton { "Undo" };
};

CommandLogPanel::CommandLogPanel (CommandDispatcher& d) : dispatcher (d)
{
    header.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    empty.setText ("Changes made by Claude (through the MCP server) or the console show up here.", juce::dontSendNotification);
    empty.setColour (juce::Label::textColourId, juce::Colours::grey);
    empty.setFont (juce::FontOptions (12.0f));
    empty.setJustificationType (juce::Justification::topLeft);
    empty.setInterceptsMouseClicks (false, false);

    clearButton.setTooltip ("Clear the log (the changes stay, and Undo still works)");
    clearButton.onClick = [this]
    {
        entries.clear();
        list.updateContent();
        empty.setVisible (true);
    };

    list.setModel (this);
    list.setRowHeight (26);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colours::black.withAlpha (0.2f));

    addAndMakeVisible (header);
    addAndMakeVisible (clearButton);
    addAndMakeVisible (list);
    addAndMakeVisible (empty);
}

void CommandLogPanel::add (const CommandDispatcher::LogEntry& entry)
{
    entries.push_back (entry);

    if (entries.size() > maxEntries)
        entries.erase (entries.begin());

    empty.setVisible (false);
    list.updateContent();
    list.scrollToEnsureRowIsOnscreen (0);
    refresh();
}

void CommandLogPanel::refresh()
{
    for (int row = 0; row < getNumRows(); ++row)
        if (auto* r = dynamic_cast<Row*> (list.getComponentForRowNumber (row)))
            r->update (getEntryForRow (row));
}

juce::Component* CommandLogPanel::refreshComponentForRow (int row, bool, juce::Component* existing)
{
    if (row < 0 || row >= getNumRows())
    {
        delete existing;
        return nullptr;
    }

    auto* r = dynamic_cast<Row*> (existing);

    if (r == nullptr)
    {
        delete existing;
        r = new Row (*this);
    }

    r->update (getEntryForRow (row));
    return r;
}

void CommandLogPanel::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (24);
    clearButton.setBounds (top.removeFromRight (56).reduced (0, 2));
    header.setBounds (top);
    list.setBounds (r);
    empty.setBounds (r.reduced (6));
}

} // namespace ng
