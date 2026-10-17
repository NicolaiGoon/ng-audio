#pragma once

#include "AppActions.h"

namespace ng
{

/**
    A searchable list of the app's actions (Ctrl+Shift+P). Typing filters by words in an
    action's name, description or keys; Up and Down pick one, Enter or a click runs it, and
    Esc or clicking elsewhere closes the palette. Each row shows the action's keys, so it
    also teaches the shortcuts.
*/
class CommandPalette  : public juce::Component,
                        private juce::ListBoxModel,
                        private juce::KeyListener
{
public:
    explicit CommandPalette (const AppActions&);
    ~CommandPalette() override;

    /** Shows the palette with an empty search, ready to type. */
    void open();
    void close();

    /** The actions matching a search, best first: those whose name starts with the search,
        then those with every word somewhere in their name, description or keys. Disabled
        actions are included (the palette shows them dimmed). */
    static juce::Array<const AppAction*> match (const AppActions&, const juce::String& search);

    void paint (juce::Graphics&) override;
    void resized() override;
    void focusOfChildComponentChanged (FocusChangeType) override;

private:
    const AppActions& actions;
    juce::TextEditor search;
    juce::ListBox list;
    juce::Array<const AppAction*> shown;

    void refilter();
    void runSelected();
    void run (int row);

    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    using juce::Component::keyPressed;
    bool keyPressed (const juce::KeyPress&, juce::Component*) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CommandPalette)
};

} // namespace ng
