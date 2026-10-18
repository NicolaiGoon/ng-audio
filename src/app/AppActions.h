#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace ng
{

/** Something the app can do from anywhere: a name for menus, a description for the shortcuts
    list, the keys that run it (if any), and whether it's available or switched on right now. */
struct AppAction
{
    juce::String id, name, description;
    juce::Array<juce::KeyPress> keys;
    std::function<void()> run;
    std::function<bool()> isEnabled, isTicked;

    /** False for keys the shortcuts list already covers elsewhere (e.g. the timeline's tools). */
    bool listed = true;
};

/**
    The app-wide actions, defined once: keyPressed() runs the one bound to a key, menus show
    each action's name and keys, and the shortcuts list (F1) is built from them, so a key
    never says one thing in a menu and do another.
*/
class AppActions
{
public:
    void add (AppAction);

    const AppAction* find (const juce::String& id) const;
    const std::vector<AppAction>& getAll() const noexcept     { return actions; }

    /** Runs the enabled action bound to the key. Returns false if there isn't one. */
    bool keyPressed (const juce::KeyPress&) const;

    /** A menu item for the action: its name, first key, and whether it's enabled and ticked now. */
    juce::PopupMenu::Item menuItem (const juce::String& id) const;

    /** A key as the app writes it, e.g. "Ctrl+Shift+K" ("Cmd" on macOS), and all of an
        action's keys, e.g. "Ctrl+Y, Ctrl+Shift+Z". */
    static juce::String describe (const juce::KeyPress&);
    static juce::String describeKeys (const AppAction&);

private:
    std::vector<AppAction> actions;
};

} // namespace ng
