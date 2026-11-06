#pragma once

#include "Session.h"

namespace ng
{

/**
    Turns the shorthand add_midi_clip and set_notes accept into plain notes, so a part can be
    written in a few lines instead of one entry per note:

    - notes: [{pitch, start, length, velocity}], where pitch is a MIDI number or a name like
      "C4" (middle C, 60), "F#3" or "Bb5".
    - chords: [{chord, start, length, velocity}] with a symbol like "G", "Em", "Am7", "Dsus4",
      "F#dim" or "C/E", played in chord_style ("block", "arp_up", "arp_down", "arp_updown",
      "alberti", "bass_root", "bass_root_octave" or "bass_root_fifth") from chord_octave
      (default 4, so C4 is middle C), one arpeggio or bass note every chord_rate beats.
    - repeat: {times, every_beats}, which plays all of the above again every every_beats.

    All three can be combined; notes and chords together make the part, which repeat then
    copies. Times are in beats from the clip's start.
*/
namespace notebuilder
{
    /** A note name such as "C4", "F#3" or "Bb5" as a MIDI pitch (C4 = 60), or -1. */
    int parsePitch (const juce::String& name);

    /** A pitch given as a number or a name, or -1 if it's neither. */
    int pitchFrom (const juce::var&);

    /** The pitches of a chord symbol, from its root in the given octave, lowest first;
        empty if the symbol isn't one this understands. A slash bass ("C/E") goes below. */
    juce::Array<int> chordPitches (const juce::String& symbol, int octave);

    /** The notes params describe, appended to out. Returns an error message, or "" if all is
        well; command names the command in errors. */
    juce::String build (const juce::String& command, const juce::var& params, juce::Array<ClipNote>& out);

    /** The schema properties the shorthand adds, for add_midi_clip and set_notes. */
    juce::var chordsSchema();
    juce::var chordStyleSchema();
    juce::var chordOctaveSchema();
    juce::var chordRateSchema();
    juce::var repeatSchema();
}

} // namespace ng
