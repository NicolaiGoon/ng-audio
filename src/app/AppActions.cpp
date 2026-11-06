#include "AppActions.h"

namespace ng
{

void AppActions::add (AppAction action)
{
    jassert (find (action.id) == nullptr);
    actions.push_back (std::move (action));
}

const AppAction* AppActions::find (const juce::String& id) const
{
    for (auto& a : actions)
        if (a.id == id)
            return &a;

    return nullptr;
}

bool AppActions::keyPressed (const juce::KeyPress& key) const
{
    for (auto& a : actions)
    {
        if (! a.keys.contains (key))
            continue;

        if (a.isEnabled == nullptr || a.isEnabled())
            a.run();

        return true;
    }

    return false;
}

juce::PopupMenu::Item AppActions::menuItem (const juce::String& id) const
{
    auto action = find (id);
    jassert (action != nullptr);

    if (action == nullptr)
        return juce::PopupMenu::Item (id);

    juce::PopupMenu::Item item (action->name);
    item.shortcutKeyDescription = action->keys.isEmpty() ? juce::String() : describe (action->keys.getFirst());
    item.action = action->run;
    item.isEnabled = action->isEnabled == nullptr || action->isEnabled();
    item.isTicked = action->isTicked != nullptr && action->isTicked();
    return item;
}

juce::String AppActions::describe (const juce::KeyPress& key)
{
    juce::String text;
    auto mods = key.getModifiers();

    if (mods.isCommandDown())
       #if JUCE_MAC
        text << "Cmd+";
       #else
        text << "Ctrl+";
       #endif

   #if JUCE_MAC
    if (mods.isCtrlDown())
        text << "Ctrl+";
   #endif

    if (mods.isAltDown())
        text << "Alt+";

    if (mods.isShiftDown())
        text << "Shift+";

    auto code = key.getKeyCode();

    if (code == juce::KeyPress::spaceKey)               text << "Space";
    else if (code == juce::KeyPress::homeKey)           text << "Home";
    else if (code == juce::KeyPress::endKey)            text << "End";
    else if (code == juce::KeyPress::escapeKey)         text << "Esc";
    else if (code == juce::KeyPress::returnKey)         text << "Enter";
    else if (code == juce::KeyPress::deleteKey)         text << "Del";
    else if (code >= juce::KeyPress::F1Key && code <= juce::KeyPress::F12Key)
        text << "F" << (code - juce::KeyPress::F1Key + 1);
    else
        text << juce::String::charToString ((juce::juce_wchar) code).toUpperCase();

    return text;
}

juce::String AppActions::describeKeys (const AppAction& action)
{
    juce::StringArray keys;

    for (auto& k : action.keys)
        keys.add (describe (k));

    return keys.joinIntoString (", ");
}

} // namespace ng
