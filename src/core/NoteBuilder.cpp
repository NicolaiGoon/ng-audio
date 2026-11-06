#include "NoteBuilder.h"
#include "CommandRegistry.h"

namespace ng::notebuilder
{
namespace
{
    constexpr int maxRepeats = 256;
    constexpr double minRate = 1.0 / 32.0;

    /** The pitch class of a note letter with its sharps and flats, reading from index i
        (which moves past them), or -1. */
    int readPitchClass (const juce::String& s, int& i)
    {
        static const int letters[] = { 9, 11, 0, 2, 4, 5, 7 };   // A to G
        auto c = juce::CharacterFunctions::toUpperCase (s[i]);

        if (c < 'A' || c > 'G')
            return -1;

        auto pc = letters[c - 'A'];
        ++i;

        for (; i < s.length(); ++i)
        {
            if (s[i] == '#')        ++pc;
            else if (s[i] == 'b')   --pc;
            else                    break;
        }

        return pc;   // may be outside 0-11 (Cb, B#); callers add octaves to it
    }

    struct Quality
    {
        const char* name;
        std::initializer_list<int> intervals;
    };

    // Longest names first, so "m7b5" isn't read as "m7".
    const Quality qualities[] = {
        { "maj9", { 0, 4, 7, 11, 14 } }, { "maj7", { 0, 4, 7, 11 } }, { "m7b5", { 0, 3, 6, 10 } },
        { "dim7", { 0, 3, 6, 9 } },      { "add9", { 0, 4, 7, 14 } }, { "sus2", { 0, 2, 7 } },
        { "sus4", { 0, 5, 7 } },         { "min7", { 0, 3, 7, 10 } }, { "M7",   { 0, 4, 7, 11 } },
        { "m9",   { 0, 3, 7, 10, 14 } }, { "m7",   { 0, 3, 7, 10 } }, { "m6",   { 0, 3, 7, 9 } },
        { "sus",  { 0, 5, 7 } },         { "dim",  { 0, 3, 6 } },     { "aug",  { 0, 4, 8 } },
        { "maj",  { 0, 4, 7 } },         { "min",  { 0, 3, 7 } },     { "9",    { 0, 4, 7, 10, 14 } },
        { "7",    { 0, 4, 7, 10 } },     { "6",    { 0, 4, 7, 9 } },  { "5",    { 0, 7 } },
        { "m",    { 0, 3, 7 } },         { "-",    { 0, 3, 7 } },     { "+",    { 0, 4, 8 } },
        { "",     { 0, 4, 7 } },
    };

    const juce::StringArray styles { "block", "arp_up", "arp_down", "arp_updown", "alberti",
                                     "bass_root", "bass_root_octave", "bass_root_fifth" };

    /** The order an arpeggio style plays a chord's pitches in, one per step, repeating. */
    juce::Array<int> arpeggioOrder (const juce::String& style, const juce::Array<int>& pitches)
    {
        juce::Array<int> order;
        auto n = pitches.size();

        if (style == "arp_up")
        {
            order = pitches;
        }
        else if (style == "arp_down")
        {
            for (int i = n; --i >= 0;)
                order.add (pitches[i]);
        }
        else if (style == "arp_updown")
        {
            order = pitches;

            for (int i = n - 2; i > 0; --i)
                order.add (pitches[i]);
        }
        else if (style == "alberti")
        {
            // Low, high, middle, high.
            auto middle = n >= 3 ? pitches[1] : pitches[0];
            order = { pitches[0], pitches[n - 1], middle, pitches[n - 1] };
        }

        return order;
    }
}

int parsePitch (const juce::String& text)
{
    auto s = text.trim();
    int i = 0;
    auto pc = readPitchClass (s, i);

    if (pc < 0)
        return -1;

    auto octaveText = s.substring (i);

    if (octaveText.isEmpty() || ! (octaveText.containsOnly ("-0123456789")) || octaveText.lastIndexOfChar ('-') > 0)
        return -1;

    auto pitch = (octaveText.getIntValue() + 1) * 12 + pc;
    return juce::isPositiveAndBelow (pitch, 128) ? pitch : -1;
}

int pitchFrom (const juce::var& v)
{
    if (params::isNumber (v))
    {
        auto pitch = static_cast<int> (v);
        return juce::isPositiveAndBelow (pitch, 128) && static_cast<double> (v) == pitch ? pitch : -1;
    }

    return v.isString() ? parsePitch (v.toString()) : -1;
}

juce::Array<int> chordPitches (const juce::String& text, int octave)
{
    auto symbol = text.trim();
    auto slash = symbol.indexOfChar ('/');
    auto main = slash >= 0 ? symbol.substring (0, slash) : symbol;

    int i = 0;
    auto rootPc = readPitchClass (main, i);

    if (rootPc < 0)
        return {};

    auto qualityText = main.substring (i);
    const Quality* quality = nullptr;

    for (auto& q : qualities)
    {
        if (qualityText == q.name)
        {
            quality = &q;
            break;
        }
    }

    if (quality == nullptr)
        return {};

    auto root = (octave + 1) * 12 + rootPc;
    juce::Array<int> pitches;

    if (slash >= 0)
    {
        auto bassText = symbol.substring (slash + 1);
        int j = 0;
        auto bassPc = readPitchClass (bassText, j);

        if (bassPc < 0 || j != bassText.length())
            return {};

        // The bass goes below the root (unless it is the root).
        auto below = ((rootPc - bassPc) % 12 + 12) % 12;

        if (below != 0)
            pitches.add (root - below);
    }

    for (auto interval : quality->intervals)
        pitches.add (root + interval);

    for (auto p : pitches)
        if (! juce::isPositiveAndBelow (p, 128))
            return {};

    return pitches;
}

juce::String build (const juce::String& command, const juce::var& p, juce::Array<ClipNote>& out)
{
    juce::Array<ClipNote> part;

    // Plain notes, with pitches as numbers or names. Other checks (range, length) are the caller's.
    if (auto notes = p["notes"]; ! notes.isVoid())
    {
        if (! notes.isArray())
            return command + ": notes is an array of {pitch, start, length, velocity}";

        for (auto& n : *notes.getArray())
        {
            if (! n.isObject())
                return command + ": invalid note " + juce::JSON::toString (n, true);

            ClipNote note;
            auto pitch = n["pitch"];

            if (pitch.isVoid())
                note.pitch = -1;   // the caller decides whether a pitch is needed
            else if ((note.pitch = pitchFrom (pitch)) < 0)
                return command + ": '" + pitch.toString() + "' isn't a pitch (0-127, or a name like \"C4\", \"F#3\", \"Bb5\")";

            note.start = static_cast<double> (n.getProperty ("start", 0.0));
            note.length = static_cast<double> (n.getProperty ("length", 1.0));
            note.velocity = static_cast<int> (n.getProperty ("velocity", 100));
            part.add (note);
        }
    }

    // Chords, written out in a style.
    if (auto chords = p["chords"]; ! chords.isVoid())
    {
        if (! chords.isArray())
            return command + ": chords is an array of {chord, start, length, velocity}";

        auto style = p.getProperty ("chord_style", "block").toString();
        auto octave = p.getProperty ("chord_octave", 4);
        auto rate = p.getProperty ("chord_rate", 0.5);

        if (! styles.contains (style))
            return command + ": chord_style is one of " + styles.joinIntoString (", ");

        if (! params::isNumber (octave) || static_cast<int> (octave) < -1 || static_cast<int> (octave) > 8)
            return command + ": chord_octave is -1 to 8 (4 puts C4, middle C, at the bottom of a C chord)";

        if (! params::isNumber (rate) || static_cast<double> (rate) < minRate)
            return command + ": chord_rate is the beats between arpeggio or bass notes, at least " + juce::String (minRate);

        auto step = static_cast<double> (rate);

        for (auto& c : *chords.getArray())
        {
            auto symbol = c["chord"].toString();
            auto pitches = chordPitches (symbol, static_cast<int> (octave));

            if (! c.isObject() || pitches.isEmpty())
                return command + ": '" + symbol + "' isn't a chord this knows (try \"C\", \"Am\", \"G7\", \"Fmaj7\", "
                                 "\"Dsus4\", \"Bdim\", \"Em/B\")";

            auto start = static_cast<double> (c.getProperty ("start", 0.0));
            auto length = static_cast<double> (c.getProperty ("length", 4.0));
            auto velocity = static_cast<int> (c.getProperty ("velocity", 100));

            if (! (length > 0.0))
                return command + ": chord '" + symbol + "' needs a length above 0";

            auto add = [&] (int pitch, double at, double len)
            {
                if (juce::isPositiveAndBelow (pitch, 128))
                    part.add ({ pitch, start + at, len, velocity });
            };

            if (style == "block")
            {
                for (auto pitch : pitches)
                    add (pitch, 0.0, length);

                continue;
            }

            // Arpeggios and bass lines: one note per step, the last one cut short at the chord's end.
            juce::Array<int> order;
            auto bass = pitches[0];

            if (style == "bass_root")               order = { bass };
            else if (style == "bass_root_octave")   order = { bass, bass + 12 };
            else if (style == "bass_root_fifth")    order = { bass, bass + 7 };
            else                                    order = arpeggioOrder (style, pitches);

            if (style == "bass_root")
            {
                add (bass, 0.0, length);
                continue;
            }

            int i = 0;

            for (double at = 0.0; at < length - 1.0e-9; at += step, ++i)
                add (order[i % order.size()], at, std::min (step, length - at) * 0.95);
        }
    }

    // Repeat the part.
    if (auto repeat = p["repeat"]; ! repeat.isVoid())
    {
        auto times = repeat["times"], every = repeat["every_beats"];

        if (! params::isNumber (times) || static_cast<int> (times) < 1 || static_cast<int> (times) > maxRepeats
              || ! params::isNumber (every) || ! (static_cast<double> (every) > 0.0))
            return command + ": repeat is {times: 1 to " + juce::String (maxRepeats) + " (how often the part plays in all), "
                             "every_beats: above 0}";

        auto copies = part;

        for (int t = 1; t < static_cast<int> (times); ++t)
            for (auto note : copies)
            {
                note.start += t * static_cast<double> (every);
                part.add (note);
            }
    }

    out.addArray (part);
    return {};
}

juce::var chordsSchema()
{
    return params::prop ("array", "Chords to write out, as {chord, start, length, velocity}: a symbol like \"G\", \"Em\", "
                                  "\"Am7\", \"Fmaj7\", \"Dsus4\", \"Bdim\" or \"C/E\", in beats from the clip start "
                                  "(length default 4). Played in chord_style; combine with notes");
}

juce::var chordStyleSchema()
{
    return params::prop ("string", "How chords play: \"block\" (default, held), \"arp_up\", \"arp_down\", \"arp_updown\", "
                                   "\"alberti\" (low-high-mid-high), or a bass line: \"bass_root\", \"bass_root_octave\", "
                                   "\"bass_root_fifth\"");
}

juce::var chordOctaveSchema()
{
    return params::prop ("integer", "Octave of the chords' roots, -1 to 8 (default 4: C4 is middle C); 2 suits a bass line");
}

juce::var chordRateSchema()
{
    return params::prop ("number", "Beats between arpeggio or bass notes (default 0.5, an 8th note)");
}

juce::var repeatSchema()
{
    return params::prop ("object", "{times, every_beats}: play the notes and chords times times in all, every_beats apart, "
                                   "e.g. {\"times\": 4, \"every_beats\": 8} for a 2-bar phrase played 4 times");
}

} // namespace ng::notebuilder
