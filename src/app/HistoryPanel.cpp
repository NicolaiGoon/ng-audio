#include "HistoryPanel.h"

namespace ng
{

HistoryPanel::HistoryPanel (Session& s, CommandDispatcher& d) : session (s), dispatcher (d)
{
    list.setRowHeight (24);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (list);

    hint.setText ("Click a step to go back to just after it. Greyed steps were undone and can be redone.",
                  juce::dontSendNotification);
    hint.setFont (juce::FontOptions (12.0f));
    hint.setColour (juce::Label::textColourId, juce::Colours::grey);
    addAndMakeVisible (hint);

    refresh();
    startTimerHz (4);
    setSize (320, 420);
}

HistoryPanel::~HistoryPanel()
{
    stopTimer();
}

juce::String HistoryPanel::getFriendlyName (const juce::String& undoDescription)
{
    auto name = undoDescription.trim();

    if (name.isEmpty())
        return "Change";

    if (name.containsChar ('_') || name == name.toLowerCase())
        name = name.replaceCharacter ('_', ' ');

    return name.substring (0, 1).toUpperCase() + name.substring (1);
}

void HistoryPanel::resized()
{
    auto r = getLocalBounds().reduced (8);
    hint.setBounds (r.removeFromBottom (36));
    list.setBounds (r);
}

void HistoryPanel::refresh()
{
    auto& um = session.getUndoManager();
    auto undoable = um.getUndoDescriptions();   // newest first
    auto redoable = um.getRedoDescriptions();   // next first

    juce::StringArray newSteps;

    for (int i = undoable.size(); --i >= 0;)
        newSteps.add (undoable[i]);

    newSteps.addArray (redoable);

    if (newSteps == steps && done == undoable.size())
        return;

    steps = newSteps;
    done = undoable.size();
    list.updateContent();
    list.repaint();
    list.scrollToEnsureRowIsOnscreen (done);
}

void HistoryPanel::timerCallback()
{
    refresh();
}

int HistoryPanel::getNumRows()
{
    return steps.size() + 1;
}

void HistoryPanel::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool)
{
    auto current = row == done;
    auto applied = row <= done;

    if (current)
        g.fillAll (juce::Colour (0xff3a6ea5).withAlpha (0.45f));

    auto text = row == 0 ? juce::String ("Opened") : getFriendlyName (steps[row - 1]);
    g.setColour (applied ? juce::Colours::white : juce::Colours::grey);
    g.setFont (juce::FontOptions (14.0f, row == 0 ? juce::Font::italic : juce::Font::plain));
    g.drawText (text, 10, 0, width - 20, height, juce::Justification::centredLeft, true);
}

void HistoryPanel::listBoxItemClicked (int row, const juce::MouseEvent&)
{
    if (row < 0 || row > steps.size())
        return;

    for (auto at = done; at > row && dispatcher.undo();)
        --at;

    for (auto at = done; at < row && dispatcher.redo();)
        ++at;

    refresh();
}

} // namespace ng
