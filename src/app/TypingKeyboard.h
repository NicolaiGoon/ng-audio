#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace ng
{

/**
    FL Studio's "typing keyboard to piano": with it on, two rows of the computer keyboard play
    the armed channel like a piano. Z S X D C V G B H N J M is the octave from middle C (the
    comma row carries on), and Q 2 W 3 E R 5 T 6 Y 7 U I 9 O 0 P [ ] the octave above.

    While it's on those keys play notes rather than run their shortcuts (as in FL Studio), so
    panels ask isNoteKey() before handling a key and leave note keys to the main window.
*/
namespace typingKeyboard
{
    /** Whether the typing keyboard is on. Set by the main window. */
    inline bool enabled = false;

    /** The lowest note: Z plays it, and Q an octave above. */
    inline int baseNote = 60;

    /** The key's distance in semitones from baseNote, or -1 if it doesn't play a note. */
    inline int getOffset (int keyCode)
    {
        static const char* lower = "zsxdcvgbhnjm,l.;/";
        static const char* upper = "q2w3er5t6y7ui9o0p[]";

        auto c = (char) juce::CharacterFunctions::toLowerCase ((juce::juce_wchar) keyCode);

        if (keyCode <= 0 || keyCode > 127)
            return -1;

        for (int i = 0; lower[i] != 0; ++i)
            if (lower[i] == c)
                return i;

        for (int i = 0; upper[i] != 0; ++i)
            if (upper[i] == c)
                return 12 + i;

        return -1;
    }

    /** The key a key press is, as the keyboard's own key code (lower case for letters). */
    inline int getKeyCode (const juce::KeyPress& key)
    {
        auto text = key.getTextCharacter();

        if (text > 0 && text < 128 && getOffset ((int) text) >= 0)
            return (int) juce::CharacterFunctions::toLowerCase (text);

        return (int) juce::CharacterFunctions::toLowerCase ((juce::juce_wchar) key.getKeyCode());
    }

    /** True if the key plays a note right now: the typing keyboard is on, and it's one of its
        keys without Ctrl, Alt or Cmd. */
    inline bool isNoteKey (const juce::KeyPress& key)
    {
        auto mods = key.getModifiers();

        return enabled && ! (mods.isCommandDown() || mods.isCtrlDown() || mods.isAltDown())
                 && getOffset (getKeyCode (key)) >= 0;
    }
}

} // namespace ng
