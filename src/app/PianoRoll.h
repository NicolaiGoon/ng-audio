#pragma once

#include "core/CommandDispatcher.h"

namespace ng
{

/**
    Edits one MIDI clip's notes: a keyboard down the left, a bar ruler on top, and a grid
    with a row per pitch.

    Click an empty spot to draw a note (drag to set its length), drag a note to move it,
    and drag its right edge to resize it. Right-click a note or press Delete to remove it.
    Ctrl+click adds to the selection and Ctrl+drag selects with a box; Ctrl+A selects all.
    The arrow keys move the selection (up and down by a semitone, Shift for an octave;
    left and right by the grid). Everything snaps to the grid; hold Alt to place freely.

    Under the grid, the velocity lane shows how hard each note is played. Click or drag
    across it to set the velocity of the notes there (only the selected ones, if any are).
    Quantize (or Q) moves the selected notes, or all of them, onto the grid.
    Ctrl+C and Ctrl+X copy and cut the selected notes, Ctrl+V pastes them at the playhead
    (or straight after where they were copied from, when the playhead is outside the
    clip), and Ctrl+D duplicates them straight after themselves.

    Every edit replaces the clip's notes with one set_notes command, so it's one undo step.
    Mouse wheel scrolls pitch, Shift + wheel scrolls time and Ctrl + wheel zooms.
*/
class PianoRoll  : public juce::Component,
                   private juce::ScrollBar::Listener,
                   private juce::Timer
{
public:
    PianoRoll (Session&, CommandDispatcher&);
    ~PianoRoll() override;

    /** Opens a clip, scrolled to its notes. */
    void setClip (const juce::String& clipId);
    juce::String getClip() const                { return clipId; }

    /** Re-reads the clip after a command. Returns false if the clip is gone. */
    bool update();

    /** Lets go of the clip, e.g. before the project is replaced. */
    void clear();

    /** The snap grid in beats (0.25 is a 16th note). */
    void setGrid (double beats);
    double getGrid() const noexcept             { return grid; }

    const juce::Array<ClipNote>& getNotes() const noexcept    { return notes; }

    /** Moves the selected notes, or every note if none is selected, onto the grid. */
    void quantize();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

    static constexpr int keyboardWidth = 56, rulerHeight = 22, rowHeight = 14, scrollBarSize = 12, velocityHeight = 64;

private:
    enum class DragMode { none, move, resizeEnd, select, velocity };

    struct Drag
    {
        DragMode mode = DragMode::none;
        int anchor = -1;                    // the note under the mouse
        juce::Array<ClipNote> original;     // the notes before the drag
        bool moved = false, created = false;
        juce::Rectangle<float> box;         // for DragMode::select
        int velocity = 0;                   // for DragMode::velocity, the last value set
    };

    juce::Rectangle<int> getRulerArea() const;
    juce::Rectangle<int> getGridArea() const;
    juce::Rectangle<int> getVelocityArea() const;
    double getTotalBeats() const;
    double beatToX (double beat) const;
    double xToBeat (double x) const;
    float pitchToY (int pitch) const;
    int yToPitch (float y) const;
    juce::Rectangle<float> getNoteBounds (const ClipNote&) const;
    int noteAt (juce::Point<float>, bool* onRightEdge = nullptr) const;
    double snapBeat (double beat, const juce::ModifierKeys&, bool roundDown) const;
    double minLength (const juce::ModifierKeys&) const;
    double getPlayheadBeatInClip() const;

    int countSelected() const;
    void selectOnly (int index);
    void commit (const juce::Array<ClipNote>& newNotes, const juce::Array<bool>& newSelection);
    void deleteSelected();
    void nudgeSelected (int semitones, double beats);
    void copySelected();
    void paste();
    void duplicateSelected();
    double getSelectionEnd (double start) const;
    void addNotes (const juce::Array<ClipNote>& added, double offset);
    void setVelocityAt (juce::Point<float>);

    void fitToClip();
    void updateScrollBars();
    void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;
    void timerCallback() override;

    void paintGrid (juce::Graphics&);
    void paintKeyboard (juce::Graphics&);
    void paintRuler (juce::Graphics&);
    void paintVelocities (juce::Graphics&);

    Session& session;
    CommandDispatcher& dispatcher;

    juce::String clipId;
    juce::Colour colour { 0xff4a90e2 };
    double clipStartBeat = 0.0, clipLengthBeats = 4.0;
    juce::Array<ClipNote> notes;
    juce::Array<bool> selected;
    juce::Array<ClipNote> clipboard;    // starts are from the first copied note
    double clipboardNext = 0.0;         // where a paste goes when the playhead is outside the clip

    /** Notes to select once the next update() has read them back (otherwise it keeps the selection). */
    std::optional<juce::Array<ClipNote>> toSelect;

    double grid = 0.25, lastLength = 1.0;
    double pixelsPerBeat = 60.0, scrollBeats = 0.0;
    int scrollY = 0;
    double lastPlayheadBeat = -1.0;
    bool needsFit = false;
    Drag drag;

    juce::ScrollBar horizontal { false }, vertical { true };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoRoll)
};

} // namespace ng
