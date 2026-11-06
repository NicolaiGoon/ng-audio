#include "CommandPalette.h"
#include "Theme.h"

namespace ng
{

CommandPalette::CommandPalette (const AppActions& a)
    : actions (a)
{
    search.setTextToShowWhenEmpty ("Type to search actions", juce::Colours::grey);
    search.setFont (juce::FontOptions (16.0f));
    search.setIndents (10, 8);
    search.onTextChange = [this] { refilter(); };
    search.onReturnKey = [this] { runSelected(); };
    search.onEscapeKey = [this] { close(); };
    search.addKeyListener (this);
    addAndMakeVisible (search);

    list.setModel (this);
    list.setRowHeight (30);
    list.setWantsKeyboardFocus (false);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (list);
}

CommandPalette::~CommandPalette()
{
    search.removeKeyListener (this);
    list.setModel (nullptr);
}

void CommandPalette::open()
{
    search.clear();
    refilter();
    setVisible (true);
    toFront (false);
    search.grabKeyboardFocus();
}

void CommandPalette::close()
{
    if (! isVisible())
        return;

    setVisible (false);

    if (auto* p = getParentComponent())
        p->grabKeyboardFocus();
}

juce::Array<const AppAction*> CommandPalette::match (const AppActions& actions, const juce::String& text)
{
    auto words = juce::StringArray::fromTokens (text.toLowerCase(), " ", "");
    words.removeEmptyStrings();
    auto query = words.joinIntoString (" ");

    juce::Array<const AppAction*> starts, contains;

    for (auto& a : actions.getAll())
    {
        if (a.id == "commandPalette")
            continue;

        auto name = a.name.toLowerCase();
        auto haystack = name + " " + a.description.toLowerCase() + " " + AppActions::describeKeys (a).toLowerCase();

        auto all = true;

        for (auto& w : words)
            all = all && haystack.contains (w);

        if (! all)
            continue;

        if (query.isNotEmpty() && name.startsWith (query))
            starts.add (&a);
        else
            contains.add (&a);
    }

    starts.addArray (contains);
    return starts;
}

void CommandPalette::refilter()
{
    shown = match (actions, search.getText());
    list.updateContent();
    list.repaint();

    // Start on the first action that can run now.
    auto first = 0;

    for (int i = 0; i < shown.size(); ++i)
    {
        if (shown[i]->isEnabled == nullptr || shown[i]->isEnabled())
        {
            first = i;
            break;
        }
    }

    list.selectRow (first);
}

void CommandPalette::runSelected()
{
    run (list.getSelectedRow());
}

void CommandPalette::run (int row)
{
    if (! juce::isPositiveAndBelow (row, shown.size()))
        return;

    auto* action = shown[row];

    if (action->isEnabled != nullptr && ! action->isEnabled())
        return;

    // Close first, so the action gets the focus back (and can open its own window on top).
    auto runIt = action->run;
    close();

    if (runIt)
        runIt();
}

bool CommandPalette::keyPressed (const juce::KeyPress& key, juce::Component*)
{
    auto rows = shown.size();

    if (rows == 0)
        return false;

    auto row = list.getSelectedRow();
    auto page = juce::jmax (1, list.getHeight() / list.getRowHeight() - 1);

    if (key == juce::KeyPress::upKey)               row = (row + rows - 1) % rows;
    else if (key == juce::KeyPress::downKey)        row = (row + 1) % rows;
    else if (key == juce::KeyPress::pageUpKey)      row = juce::jmax (0, row - page);
    else if (key == juce::KeyPress::pageDownKey)    row = juce::jmin (rows - 1, row + page);
    else
        return false;

    list.selectRow (row);
    return true;
}

void CommandPalette::focusOfChildComponentChanged (FocusChangeType)
{
    // Clicking anywhere outside closes it.
    if (isVisible() && ! hasKeyboardFocus (true))
        close();
}

int CommandPalette::getNumRows()
{
    return shown.size();
}

void CommandPalette::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (! juce::isPositiveAndBelow (row, shown.size()))
        return;

    auto& a = *shown[row];
    auto enabled = a.isEnabled == nullptr || a.isEnabled();
    auto text = findColour (juce::Label::textColourId);
    auto r = juce::Rectangle<int> (width, height).reduced (6, 2);

    if (selected)
    {
        g.setColour (theme::accent.withAlpha (0.3f));
        g.fillRoundedRectangle (r.toFloat(), 4.0f);
    }

    r.reduce (8, 0);
    auto alpha = enabled ? 1.0f : 0.4f;

    if (a.isTicked != nullptr && a.isTicked())
    {
        g.setColour (theme::accent.withMultipliedAlpha (alpha));
        g.fillEllipse (r.removeFromLeft (8).withSizeKeepingCentre (6, 6).toFloat());
    }
    else
    {
        r.removeFromLeft (8);
    }

    r.removeFromLeft (8);

    auto keys = AppActions::describeKeys (a);
    g.setFont (juce::FontOptions (13.0f));
    auto keysWidth = keys.isEmpty() ? 0 : juce::roundToInt (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), keys)) + 12;
    g.setColour (text.withAlpha (0.55f * alpha));
    g.drawText (keys, r.removeFromRight (keysWidth), juce::Justification::centredRight);

    g.setFont (juce::FontOptions (14.5f));
    auto nameWidth = juce::roundToInt (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), a.name)) + 14;
    g.setColour (text.withAlpha (alpha));
    g.drawText (a.name, r.removeFromLeft (nameWidth), juce::Justification::centredLeft, true);

    if (a.description != a.name)
    {
        g.setFont (juce::FontOptions (13.0f));
        g.setColour (text.withAlpha (0.5f * alpha));
        g.drawText (a.description, r, juce::Justification::centredLeft, true);
    }
}

void CommandPalette::listBoxItemClicked (int row, const juce::MouseEvent&)
{
    run (row);
}

void CommandPalette::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (r.translated (0, 3), 8.0f);
    g.setColour (theme::panel);
    g.fillRoundedRectangle (r.reduced (1.0f), 8.0f);
    g.setColour (theme::outline);
    g.drawRoundedRectangle (r.reduced (1.5f), 8.0f, 1.0f);
}

void CommandPalette::resized()
{
    auto r = getLocalBounds().reduced (8);
    search.setBounds (r.removeFromTop (36));
    r.removeFromTop (6);
    list.setBounds (r);
}

} // namespace ng
