#include "PianoRoll.h"
#include "TypingKeyboard.h"
#include "Widgets.h"

namespace ng
{
namespace
{
    const juce::Colour whiteRow (0xff30353b), blackRow (0xff282c31), keyboardBackground (0xff262a2f),
                       playheadColour (0xffff5a4e);

    constexpr double minPixelsPerBeat = 10.0, maxPixelsPerBeat = 400.0, freeStep = 1.0 / 64.0;

    bool isBlackKey (int pitch)
    {
        return juce::MidiMessage::isMidiNoteBlack (pitch);
    }

    bool sameNote (const ClipNote& a, const ClipNote& b)
    {
        return a.pitch == b.pitch && a.velocity == b.velocity
                 && std::abs (a.start - b.start) < 1.0e-6 && std::abs (a.length - b.length) < 1.0e-6;
    }
}

PianoRoll::PianoRoll (Session& s, CommandDispatcher& d)
    : session (s), dispatcher (d)
{
    for (auto* bar : { &horizontal, &vertical })
    {
        bar->setAutoHide (false);
        bar->addListener (this);
        addAndMakeVisible (bar);
    }

    setWantsKeyboardFocus (true);
}

PianoRoll::~PianoRoll()
{
    stopPreview();
    horizontal.removeListener (this);
    vertical.removeListener (this);
}

//==============================================================================
void PianoRoll::setClip (const juce::String& newClipId)
{
    clipId = newClipId;
    drag = {};
    notes.clearQuick();
    selected.clearQuick();
    toSelect.reset();

    if (! update())
        return;

    needsFit = true;

    if (getWidth() > 0)
        fitToClip();
}

void PianoRoll::fitToClip()
{
    // Fit the clip across the view, and centre the notes (or middle C) vertically.
    needsFit = false;
    auto gridArea = getGridArea();
    pixelsPerBeat = juce::jlimit (minPixelsPerBeat, 200.0, (gridArea.getWidth() - 20) / std::max (1.0, clipLengthBeats));
    scrollBeats = 0.0;

    int lowest = 60, highest = 60;

    if (! notes.isEmpty())
    {
        lowest = 127;
        highest = 0;

        for (auto& n : notes)
        {
            lowest = std::min (lowest, n.pitch);
            highest = std::max (highest, n.pitch);
        }
    }

    auto middle = (lowest + highest) / 2;
    scrollY = (127 - middle) * rowHeight + rowHeight / 2 - gridArea.getHeight() / 2;
    updateScrollBars();
    repaint();
}

bool PianoRoll::update()
{
    if (clipId.isEmpty())
        return false;

    auto midi = dynamic_cast<te::MidiClip*> (session.findClip (clipId));

    if (midi == nullptr)
    {
        clear();
        return false;
    }

    // Mid-drag the preview is the truth; the drag's own command re-reads everything after.
    if (drag.mode != DragMode::none)
        return true;

    auto& tempo = session.getEdit().tempoSequence;
    clipStartBeat = tempo.toBeats (midi->getPosition().getStart()).inBeats();
    clipLengthBeats = tempo.toBeats (midi->getPosition().getEnd()).inBeats() - clipStartBeat;

    if (auto track = midi->getTrack())
        colour = track->getColour();

    juce::Array<ClipNote> keep;

    if (toSelect)
        keep = *toSelect;
    else
        for (int i = 0; i < notes.size(); ++i)
            if (selected[i])
                keep.add (notes[i]);

    toSelect.reset();
    notes = Session::getNotes (*midi);
    selected.clearQuick();

    for (auto& n : notes)
        selected.add (std::any_of (keep.begin(), keep.end(), [&n] (const ClipNote& k) { return sameNote (n, k); }));

    updateScrollBars();
    repaint();
    return true;
}

void PianoRoll::clear()
{
    stopPreview();
    clipId = {};
    notes.clear();
    selected.clear();
    toSelect.reset();
    drag = {};
    repaint();
}

void PianoRoll::setGrid (double beats)
{
    grid = beats;
    repaint();
}

//==============================================================================
juce::Rectangle<int> PianoRoll::getRulerArea() const
{
    return { keyboardWidth, 0, getWidth() - keyboardWidth - scrollBarSize, rulerHeight };
}

juce::Rectangle<int> PianoRoll::getGridArea() const
{
    return { keyboardWidth, rulerHeight, getWidth() - keyboardWidth - scrollBarSize,
             getHeight() - rulerHeight - scrollBarSize - velocityHeight };
}

juce::Rectangle<int> PianoRoll::getVelocityArea() const
{
    return { keyboardWidth, getHeight() - scrollBarSize - velocityHeight, getWidth() - keyboardWidth - scrollBarSize, velocityHeight };
}

double PianoRoll::getTotalBeats() const
{
    auto end = clipLengthBeats;

    for (auto& n : notes)
        end = std::max (end, n.start + n.length);

    return std::max (end + 4.0, scrollBeats + getGridArea().getWidth() / pixelsPerBeat);
}

double PianoRoll::beatToX (double beat) const   { return keyboardWidth + (beat - scrollBeats) * pixelsPerBeat; }
double PianoRoll::xToBeat (double x) const      { return scrollBeats + (x - keyboardWidth) / pixelsPerBeat; }

float PianoRoll::pitchToY (int pitch) const
{
    return (float) (rulerHeight + (127 - pitch) * rowHeight - scrollY);
}

int PianoRoll::yToPitch (float y) const
{
    return 127 - (int) std::floor ((y - (float) rulerHeight + (float) scrollY) / (float) rowHeight);
}

juce::Rectangle<float> PianoRoll::getNoteBounds (const ClipNote& n) const
{
    return { (float) beatToX (n.start), pitchToY (n.pitch) + 1.0f,
             std::max (3.0f, (float) (n.length * pixelsPerBeat)), (float) rowHeight - 1.0f };
}

int PianoRoll::noteAt (juce::Point<float> pos, bool* onRightEdge) const
{
    // Later notes are drawn on top, so look at them first.
    for (int i = notes.size(); --i >= 0;)
    {
        auto bounds = getNoteBounds (notes.getReference (i));

        if (bounds.contains (pos))
        {
            if (onRightEdge != nullptr)
                *onRightEdge = pos.x > bounds.getRight() - std::min (6.0f, bounds.getWidth() / 3.0f);

            return i;
        }
    }

    return -1;
}

double PianoRoll::snapBeat (double beat, const juce::ModifierKeys& mods, bool roundDown) const
{
    auto step = mods.isAltDown() ? freeStep : grid;
    return (roundDown ? std::floor (beat / step + 1.0e-9) : std::round (beat / step)) * step;
}

double PianoRoll::minLength (const juce::ModifierKeys& mods) const
{
    return mods.isAltDown() ? freeStep : grid;
}

double PianoRoll::getPlayheadBeatInClip() const
{
    auto& edit = session.getEdit();
    return edit.tempoSequence.toBeats (edit.getTransport().getPosition()).inBeats() - clipStartBeat;
}

//==============================================================================
int PianoRoll::countSelected() const
{
    return (int) std::count (selected.begin(), selected.end(), true);
}

void PianoRoll::selectOnly (int index)
{
    for (int i = 0; i < selected.size(); ++i)
        selected.set (i, i == index);

    repaint();
}

void PianoRoll::commit (const juce::Array<ClipNote>& newNotes, const juce::Array<bool>& newSelection)
{
    juce::Array<juce::var> list;
    juce::Array<ClipNote> keep;

    for (int i = 0; i < newNotes.size(); ++i)
    {
        auto& n = newNotes.getReference (i);
        list.add (makeObject ({ { "pitch", n.pitch }, { "start", n.start }, { "length", n.length }, { "velocity", n.velocity } }));

        if (newSelection[i])
            keep.add (n);
    }

    notes = newNotes;
    selected = newSelection;
    toSelect = keep;

    if (! dispatcher.execute ("set_notes", makeObject ({ { "clip", clipId }, { "notes", list } })).ok)
    {
        toSelect.reset();
        update();
    }

    repaint();
}

void PianoRoll::deleteSelected()
{
    juce::Array<ClipNote> remaining;

    for (int i = 0; i < notes.size(); ++i)
        if (! selected[i])
            remaining.add (notes[i]);

    if (remaining.size() != notes.size())
    {
        juce::Array<bool> none;
        none.insertMultiple (0, false, remaining.size());
        commit (remaining, none);
    }
}

void PianoRoll::setVelocityAt (juce::Point<float> pos)
{
    auto area = getVelocityArea().toFloat();
    auto top = area.getY() + 6.0f, bottom = area.getBottom() - 2.0f;
    auto velocity = juce::jlimit (1, 127, juce::roundToInt ((bottom - pos.y) / (bottom - top) * 127.0f));
    auto beat = xToBeat (pos.x);
    auto onlySelected = countSelected() > 0;

    // Every note playing at that point in time (just the selected ones, if there are any).
    for (int i = 0; i < notes.size(); ++i)
    {
        auto& n = notes.getReference (i);

        if ((! onlySelected || selected[i]) && beat >= n.start && beat < n.start + std::max (n.length, 4.0 / pixelsPerBeat))
            n.velocity = velocity;
    }

    drag.velocity = velocity;
    repaint();
}

void PianoRoll::quantize()
{
    if (clipId.isEmpty() || notes.isEmpty())
        return;

    auto params = makeObject ({ { "clip", clipId }, { "grid", grid } });
    auto anySelected = countSelected() > 0;

    if (anySelected)
    {
        juce::Array<juce::var> indexes;

        for (int i = 0; i < notes.size(); ++i)
            if (selected[i])
                indexes.add (i);

        params.getDynamicObject()->setProperty ("notes", indexes);
    }

    auto result = dispatcher.execute ("quantize_notes", params);

    // Keep the same notes selected where they landed.
    if (result.ok && anySelected)
    {
        juce::Array<ClipNote> keep;

        for (auto& i : *result.value["quantized"].getArray())
        {
            auto n = result.value["notes"][static_cast<int> (i)];
            keep.add ({ n["pitch"], n["start"], n["length"], n["velocity"] });
        }

        toSelect = keep;
    }
}

void PianoRoll::nudgeSelected (int semitones, double beats)
{
    auto moved = notes;

    for (int i = 0; i < moved.size(); ++i)
    {
        if (! selected[i])
            continue;

        auto& n = moved.getReference (i);
        n.pitch += semitones;
        n.start += beats;

        // The whole selection moves together or not at all.
        if (! juce::isPositiveAndBelow (n.pitch, 128) || n.start < -1.0e-9 || n.start >= clipLengthBeats)
            return;
    }

    if (countSelected() > 0)
        commit (moved, selected);
}

void PianoRoll::copySelected()
{
    clipboard.clearQuick();
    auto first = std::numeric_limits<double>::max();

    for (int i = 0; i < notes.size(); ++i)
    {
        if (selected[i])
        {
            clipboard.add (notes[i]);
            first = std::min (first, notes[i].start);
        }
    }

    for (auto& n : clipboard)
        n.start -= first;

    clipboardNext = getSelectionEnd (first);
}

void PianoRoll::paste()
{
    auto at = getPlayheadBeatInClip();

    if (at >= 0.0 && at < clipLengthBeats)
        at = std::floor (at / grid + 1.0e-6) * grid;
    else
        at = clipboardNext;

    addNotes (clipboard, at);
}

void PianoRoll::duplicateSelected()
{
    juce::Array<ClipNote> copies;
    auto start = std::numeric_limits<double>::max();

    for (int i = 0; i < notes.size(); ++i)
    {
        if (selected[i])
        {
            copies.add (notes[i]);
            start = std::min (start, notes[i].start);
        }
    }

    if (! copies.isEmpty())
        addNotes (copies, getSelectionEnd (start) - start);
}

double PianoRoll::getSelectionEnd (double start) const
{
    auto end = start;

    for (int i = 0; i < notes.size(); ++i)
        if (selected[i])
            end = std::max (end, notes[i].start + notes[i].length);

    // Rounded up to the grid from where the selection starts, so a copy stays in time.
    return start + std::ceil ((end - start) / grid - 1.0e-6) * grid;
}

void PianoRoll::addNotes (const juce::Array<ClipNote>& added, double offset)
{
    auto newNotes = notes;
    juce::Array<bool> newSelection;
    newSelection.insertMultiple (0, false, notes.size());

    // Notes that would start past the end of the clip wouldn't play, so they're left out.
    for (auto n : added)
    {
        n.start += offset;

        if (n.start >= 0.0 && n.start < clipLengthBeats)
        {
            newNotes.add (n);
            newSelection.add (true);
        }
    }

    if (newNotes.size() > notes.size())
        commit (newNotes, newSelection);
}

//==============================================================================
void PianoRoll::updateScrollBars()
{
    auto gridArea = getGridArea();
    horizontal.setRangeLimits (0.0, getTotalBeats(), juce::dontSendNotification);
    horizontal.setCurrentRange (scrollBeats, gridArea.getWidth() / pixelsPerBeat, juce::dontSendNotification);

    auto contentHeight = 128 * rowHeight;
    scrollY = juce::jlimit (0, std::max (0, contentHeight - gridArea.getHeight()), scrollY);
    vertical.setRangeLimits (0.0, contentHeight, juce::dontSendNotification);
    vertical.setCurrentRange (scrollY, gridArea.getHeight(), juce::dontSendNotification);
}

void PianoRoll::scrollBarMoved (juce::ScrollBar* bar, double newRangeStart)
{
    if (bar == &horizontal)
        scrollBeats = std::max (0.0, newRangeStart);
    else
        scrollY = juce::roundToInt (newRangeStart);

    repaint();
}

void PianoRoll::repaintPlayhead (double beat)
{
    auto x = juce::roundToInt (beatToX (beat));

    if (x >= keyboardWidth - 8 && x <= getWidth())
        repaint (x - 8, 0, 16, getVelocityArea().getBottom());
}

void PianoRoll::onVBlank()
{
    if (clipId.isEmpty())
        return;

    auto beat = getPlayheadBeatInClip();

    // Only the strips the playhead left and arrived at need drawing again.
    if (std::abs (beat - lastPlayheadBeat) > 1.0e-5)
    {
        repaintPlayhead (lastPlayheadBeat);
        repaintPlayhead (beat);
        lastPlayheadBeat = beat;
    }
}

void PianoRoll::zoomHorizontally (double factor)
{
    auto anchorX = (float) getGridArea().getCentreX();
    auto anchorBeat = xToBeat (anchorX);
    pixelsPerBeat = juce::jlimit (minPixelsPerBeat, maxPixelsPerBeat, pixelsPerBeat * factor);
    scrollBeats = std::max (0.0, anchorBeat - (anchorX - keyboardWidth) / pixelsPerBeat);
    updateScrollBars();
    repaint();
}

void PianoRoll::zoomVertically (double factor)
{
    setRowHeight (juce::roundToInt (rowHeight * factor), (float) getGridArea().getCentreY());
}

void PianoRoll::setRowHeight (int newHeight, float anchorY)
{
    // Keep the pitch under the anchor where it is.
    auto anchorRow = ((double) anchorY - rulerHeight + scrollY) / rowHeight;
    rowHeight = juce::jlimit (6, 40, newHeight);
    scrollY = juce::roundToInt (anchorRow * rowHeight - (anchorY - rulerHeight));
    updateScrollBars();
    repaint();
}

void PianoRoll::previewNote (int pitch, int velocity)
{
    if (pitch == previewPitch)
        return;

    stopPreview();

    if (! juce::isPositiveAndBelow (pitch, 128))
        return;

    if (auto clip = session.findClip (clipId))
        if (auto track = dynamic_cast<te::AudioTrack*> (clip->getTrack()))
        {
            track->playGuideNote (pitch, te::MidiChannel (1), juce::jlimit (1, 127, velocity), true, true, false);
            previewPitch = pitch;
        }
}

void PianoRoll::stopPreview()
{
    if (previewPitch < 0)
        return;

    previewPitch = -1;

    if (auto clip = session.findClip (clipId))
        if (auto track = dynamic_cast<te::AudioTrack*> (clip->getTrack()))
            track->turnOffGuideNotes();
}

void PianoRoll::resized()
{
    horizontal.setBounds (keyboardWidth, getHeight() - scrollBarSize, getWidth() - keyboardWidth - scrollBarSize, scrollBarSize);
    vertical.setBounds (getWidth() - scrollBarSize, rulerHeight, scrollBarSize, getGridArea().getHeight());

    if (needsFit && getWidth() > 0)
        fitToClip();
    else
        updateScrollBars();
}

//==============================================================================
void PianoRoll::paint (juce::Graphics& g)
{
    g.fillAll (whiteRow);
    paintGrid (g);
    paintVelocities (g);
    paintKeyboard (g);
    paintRuler (g);

    g.setColour (keyboardBackground);
    g.fillRect (0, 0, keyboardWidth, rulerHeight);
    g.fillRect (getWidth() - scrollBarSize, getHeight() - scrollBarSize, scrollBarSize, scrollBarSize);

    auto x = (float) beatToX (getPlayheadBeatInClip());

    if (x >= keyboardWidth && x <= getRulerArea().getRight())
    {
        g.setColour (playheadColour);
        g.drawLine (x, 0.0f, x, (float) getVelocityArea().getBottom(), 1.5f);

        juce::Path head;
        head.addTriangle (x - 5.0f, 0.0f, x + 5.0f, 0.0f, x, 7.0f);
        g.fillPath (head);
    }
}

void PianoRoll::paintGrid (juce::Graphics& g)
{
    auto area = getGridArea();
    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (area);

    // A row per pitch, darker for the black keys, with a line under each C.
    auto top = std::min (127, yToPitch ((float) area.getY()));
    auto bottom = std::max (0, yToPitch ((float) area.getBottom()));

    for (int pitch = top; pitch >= bottom; --pitch)
    {
        auto y = pitchToY (pitch);
        g.setColour (isBlackKey (pitch) ? blackRow : whiteRow);
        g.fillRect ((float) area.getX(), y, (float) area.getWidth(), (float) rowHeight);

        g.setColour (juce::Colours::black.withAlpha (pitch % 12 == 0 ? 0.45f : 0.12f));
        g.drawHorizontalLine ((int) (y + (float) rowHeight) - 1, (float) area.getX(), (float) area.getRight());
    }

    // Grid lines, beats, then bars (which follow the song's bars, not the clip's start).
    auto endBeat = xToBeat (area.getRight());

    if (grid * pixelsPerBeat >= 6.0)
    {
        g.setColour (juce::Colours::black.withAlpha (0.12f));

        for (auto b = std::floor (scrollBeats / grid) * grid; b <= endBeat; b += grid)
            g.drawVerticalLine ((int) beatToX (b), (float) area.getY(), (float) area.getBottom());
    }

    g.setColour (juce::Colours::black.withAlpha (0.25f));

    for (auto b = std::floor (scrollBeats); b <= endBeat; b += 1.0)
        g.drawVerticalLine ((int) beatToX (b), (float) area.getY(), (float) area.getBottom());

    auto& tempo = session.getEdit().tempoSequence;
    auto firstBar = std::max (0, tempo.toBarsAndBeats (tempo.toTime (te::BeatPosition::fromBeats (clipStartBeat + scrollBeats))).bars);
    g.setColour (juce::Colours::black.withAlpha (0.6f));

    for (int bar = firstBar;; ++bar)
    {
        auto beat = tempo.toBeats (te::tempo::BarsAndBeats { bar, {} }).inBeats() - clipStartBeat;

        if (beat > endBeat)
            break;

        g.drawVerticalLine ((int) beatToX (beat), (float) area.getY(), (float) area.getBottom());
    }

    // Past the clip's end nothing plays.
    auto endX = (float) beatToX (clipLengthBeats);

    if (endX < (float) area.getRight())
    {
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRect (juce::Rectangle<float> (endX, (float) area.getY(), (float) area.getRight() - endX, (float) area.getHeight()));
        g.setColour (juce::Colours::white.withAlpha (0.5f));
        g.drawVerticalLine ((int) endX, (float) area.getY(), (float) area.getBottom());
    }

    // The notes.
    g.setFont (juce::FontOptions (10.0f));

    for (int i = 0; i < notes.size(); ++i)
    {
        auto& n = notes.getReference (i);
        auto bounds = getNoteBounds (n);

        if (! bounds.intersects (area.toFloat()))
            continue;

        auto isSelected = selected[i];
        auto fill = isSelected ? colour.brighter (0.6f) : colour;
        g.setColour (fill.withAlpha (0.55f + 0.45f * (float) n.velocity / 127.0f));
        g.fillRoundedRectangle (bounds, 2.0f);
        g.setColour (isSelected ? juce::Colours::white : colour.darker (0.6f));
        g.drawRoundedRectangle (bounds.reduced (0.5f), 2.0f, isSelected ? 1.5f : 1.0f);

        if (bounds.getWidth() > 30.0f)
        {
            g.setColour (juce::Colours::black.withAlpha (0.75f));
            g.drawText (juce::MidiMessage::getMidiNoteName (n.pitch, true, true, 4),
                        bounds.reduced (3.0f, 0.0f), juce::Justification::centredLeft, false);
        }
    }

    if (drag.mode == DragMode::select)
    {
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.fillRect (drag.box);
        g.setColour (juce::Colours::white.withAlpha (0.7f));
        g.drawRect (drag.box, 1.0f);
    }

    if (notes.isEmpty())
    {
        g.setColour (juce::Colours::white.withAlpha (0.5f));
        g.setFont (juce::FontOptions (14.0f));
        g.drawText ("Click to draw a note", area, juce::Justification::centred);
    }
}

void PianoRoll::paintVelocities (juce::Graphics& g)
{
    auto area = getVelocityArea();
    juce::Graphics::ScopedSaveState save (g);

    // The label sits under the keyboard.
    g.setColour (keyboardBackground);
    g.fillRect (0, area.getY(), getWidth(), area.getHeight() + scrollBarSize);
    g.setColour (juce::Colours::grey);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("Velocity", 0, area.getY(), keyboardWidth - 4, 18, juce::Justification::centredRight);

    g.reduceClipRegion (area);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawHorizontalLine (area.getY(), (float) area.getX(), (float) area.getRight());

    auto endX = (float) beatToX (clipLengthBeats);

    if (endX < (float) area.getRight())
    {
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRect (juce::Rectangle<float> (endX, (float) area.getY(), (float) area.getRight() - endX, (float) area.getHeight()));
    }

    // A stem per note, as tall as it is loud, with a line across the note's length.
    auto top = (float) area.getY() + 6.0f, bottom = (float) area.getBottom() - 2.0f;

    for (int pass = 0; pass < 2; ++pass)   // selected notes on top
    {
        for (int i = 0; i < notes.size(); ++i)
        {
            if (selected[i] != (pass == 1))
                continue;

            auto& n = notes.getReference (i);
            auto x = (float) beatToX (n.start);
            auto width = std::max (3.0f, (float) (n.length * pixelsPerBeat));

            if (x + width < (float) area.getX() || x > (float) area.getRight())
                continue;

            auto y = bottom - (bottom - top) * (float) n.velocity / 127.0f;
            auto c = selected[i] ? colour.brighter (0.6f) : colour.withAlpha (0.8f);
            g.setColour (c.withAlpha (0.35f));
            g.drawHorizontalLine ((int) y, x, x + width - 1.0f);
            g.setColour (c);
            g.fillRect (x, y, 2.0f, bottom - y);
            g.fillEllipse (x - 2.5f, y - 3.0f, 7.0f, 7.0f);
        }
    }

    if (drag.mode == DragMode::velocity && drag.velocity > 0)
    {
        g.setColour (juce::Colours::white.withAlpha (0.8f));
        g.drawText (juce::String (drag.velocity), area.getX() + 6, area.getY() + 2, 60, 16, juce::Justification::centredLeft);
    }
}

void PianoRoll::paintKeyboard (juce::Graphics& g)
{
    auto area = juce::Rectangle<int> (0, rulerHeight, keyboardWidth, getGridArea().getHeight());
    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (area);
    g.setColour (keyboardBackground);
    g.fillRect (area);

    auto top = std::min (127, yToPitch ((float) area.getY()));
    auto bottom = std::max (0, yToPitch ((float) area.getBottom()));
    g.setFont (juce::FontOptions (10.0f));

    for (int pitch = top; pitch >= bottom; --pitch)
    {
        auto key = juce::Rectangle<float> (0.0f, pitchToY (pitch), (float) keyboardWidth - 1.0f, (float) rowHeight);

        if (isBlackKey (pitch))
        {
            g.setColour (juce::Colour (0xff1b1d20));
            g.fillRect (key.withWidth ((float) keyboardWidth * 0.62f).reduced (0.0f, 0.5f));
        }
        else
        {
            g.setColour (juce::Colour (0xffe8e8e8));
            g.fillRect (key.reduced (0.0f, 0.5f));
        }

        if (pitch % 12 == 0)
        {
            g.setColour (juce::Colours::black.withAlpha (0.8f));
            g.drawText (juce::MidiMessage::getMidiNoteName (pitch, true, true, 4), key.reduced (4.0f, 0.0f),
                        juce::Justification::centredRight, false);
        }
    }
}

void PianoRoll::paintRuler (juce::Graphics& g)
{
    auto ruler = getRulerArea();
    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (ruler);
    g.setColour (keyboardBackground);
    g.fillRect (ruler);

    auto& tempo = session.getEdit().tempoSequence;
    auto endBeat = xToBeat (ruler.getRight());
    auto firstBar = std::max (0, tempo.toBarsAndBeats (tempo.toTime (te::BeatPosition::fromBeats (clipStartBeat + scrollBeats))).bars);
    g.setFont (juce::FontOptions (12.0f));

    for (auto b = std::floor (scrollBeats); b <= endBeat; b += 1.0)
    {
        g.setColour (juce::Colours::grey.withAlpha (0.5f));
        g.drawVerticalLine ((int) beatToX (b), (float) ruler.getBottom() - 5.0f, (float) ruler.getBottom());
    }

    for (int bar = firstBar;; ++bar)
    {
        auto beat = tempo.toBeats (te::tempo::BarsAndBeats { bar, {} }).inBeats() - clipStartBeat;

        if (beat > endBeat)
            break;

        auto x = (int) beatToX (beat);
        g.setColour (juce::Colours::grey.withAlpha (0.9f));
        g.drawVerticalLine (x, (float) ruler.getY(), (float) ruler.getBottom());
        g.setColour (juce::Colours::lightgrey);
        g.drawText (juce::String (bar + 1), x + 4, ruler.getY(), 40, ruler.getHeight(), juce::Justification::centredLeft);
    }

    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.drawHorizontalLine (ruler.getBottom() - 1, (float) ruler.getX(), (float) ruler.getRight());
}

//==============================================================================
void PianoRoll::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    drag = {};

    if (clipId.isEmpty())
        return;

    if (getRulerArea().contains (e.getPosition()))
    {
        auto beat = clipStartBeat + std::max (-clipStartBeat, snapBeat (xToBeat (e.position.x), e.mods, false));
        dispatcher.execute ("set_position", makeObject ({ { "beat", beat } }));
        return;
    }

    // The keyboard on the left plays its keys (drag for a glissando).
    if (e.position.x < (float) keyboardWidth && e.position.y >= (float) rulerHeight
         && e.position.y < (float) getGridArea().getBottom() && ! e.mods.isPopupMenu())
    {
        playingKeys = true;
        previewNote (yToPitch (e.position.y), juce::jlimit (30, 127, juce::roundToInt (40.0f + 87.0f * e.position.x / (float) keyboardWidth)));
        return;
    }

    if (getVelocityArea().contains (e.getPosition()))
    {
        drag.mode = DragMode::velocity;
        drag.original = notes;
        drag.moved = true;
        setVelocityAt (e.position);
        return;
    }

    if (! getGridArea().contains (e.getPosition()))
        return;

    auto onEdge = false;
    auto index = noteAt (e.position, &onEdge);

    if (e.mods.isPopupMenu())
    {
        if (index >= 0)
        {
            if (! selected[index])
                selectOnly (index);

            deleteSelected();
        }

        return;
    }

    if (index >= 0)
    {
        if (e.mods.isCommandDown())
        {
            selected.set (index, ! selected[index]);
            repaint();

            if (! selected[index])
                return;
        }
        else if (! selected[index])
        {
            selectOnly (index);
        }

        drag.mode = onEdge ? DragMode::resizeEnd : DragMode::move;
        drag.anchor = index;
        drag.original = notes;
        previewNote (notes[index].pitch, notes[index].velocity);
        return;
    }

    if (e.mods.isCommandDown())
    {
        drag.mode = DragMode::select;
        drag.box = { e.position, e.position };
        drag.original = notes;
        return;
    }

    // Draw a new note in the grid cell that was clicked, as long as the last one drawn.
    auto start = snapBeat (xToBeat (e.position.x), e.mods, true);
    auto pitch = yToPitch (e.position.y);

    if (start < 0.0 || start >= clipLengthBeats || ! juce::isPositiveAndBelow (pitch, 128))
        return;

    notes.add ({ pitch, start, std::max (minLength (e.mods), std::min (lastLength, clipLengthBeats - start)), 100 });
    selected.clearQuick();
    selected.insertMultiple (0, false, notes.size() - 1);
    selected.add (true);

    drag.mode = DragMode::resizeEnd;
    drag.anchor = notes.size() - 1;
    drag.original = notes;
    drag.created = true;
    previewNote (pitch);
    repaint();
}

void PianoRoll::mouseDrag (const juce::MouseEvent& e)
{
    if (clipId.isEmpty())
        return;

    if (playingKeys)
    {
        previewNote (yToPitch (juce::jlimit ((float) rulerHeight, (float) getGridArea().getBottom() - 1.0f, e.position.y)));
        return;
    }

    if (getRulerArea().contains (e.getMouseDownPosition()))
    {
        auto beat = clipStartBeat + std::max (-clipStartBeat, snapBeat (xToBeat (std::max ((float) keyboardWidth, e.position.x)), e.mods, false));
        dispatcher.execute ("set_position", makeObject ({ { "beat", beat } }));
        return;
    }

    if (drag.mode == DragMode::none || (! drag.moved && e.getDistanceFromDragStart() < 3))
        return;

    drag.moved = true;
    auto deltaBeats = (e.position.x - e.mouseDownPosition.x) / pixelsPerBeat;

    switch (drag.mode)
    {
        case DragMode::velocity:
            setVelocityAt (e.position);
            break;

        case DragMode::select:
        {
            drag.box = juce::Rectangle<float> (e.mouseDownPosition, e.position);

            for (int i = 0; i < notes.size(); ++i)
                selected.set (i, getNoteBounds (notes.getReference (i)).intersects (drag.box));

            break;
        }

        case DragMode::move:
        {
            auto& anchor = drag.original.getReference (drag.anchor);
            auto delta = snapBeat (anchor.start + deltaBeats, e.mods, false) - anchor.start;
            auto semitones = yToPitch (e.position.y) - yToPitch (e.mouseDownPosition.y);

            // Keep the whole selection inside the clip and the keyboard.
            for (int i = 0; i < drag.original.size(); ++i)
            {
                if (! selected[i])
                    continue;

                auto& n = drag.original.getReference (i);
                delta = juce::jlimit (-n.start, clipLengthBeats - freeStep - n.start, delta);
                semitones = juce::jlimit (-n.pitch, 127 - n.pitch, semitones);
            }

            notes = drag.original;

            for (int i = 0; i < notes.size(); ++i)
            {
                if (selected[i])
                {
                    notes.getReference (i).start += delta;
                    notes.getReference (i).pitch += semitones;
                }
            }

            // Hear the note at its new pitch.
            previewNote (notes[drag.anchor].pitch, notes[drag.anchor].velocity);
            break;
        }

        case DragMode::resizeEnd:
        {
            auto& anchor = drag.original.getReference (drag.anchor);
            auto shortest = minLength (e.mods);

            // A note being drawn ends under the mouse; an existing one grows by how far its edge was dragged.
            auto end = drag.created ? snapBeat (xToBeat (e.position.x), e.mods, false)
                                    : snapBeat (anchor.start + anchor.length + deltaBeats, e.mods, false);
            auto change = std::max (shortest, end - anchor.start) - anchor.length;

            notes = drag.original;

            for (int i = 0; i < notes.size(); ++i)
                if (selected[i])
                    notes.getReference (i).length = std::max (shortest, drag.original.getReference (i).length + change);

            break;
        }

        case DragMode::none:
            break;
    }

    repaint();
}

void PianoRoll::mouseUp (const juce::MouseEvent&)
{
    stopPreview();
    playingKeys = false;
    auto finished = drag;
    drag = {};

    if (finished.mode == DragMode::select)
    {
        repaint();
        return;
    }

    if (finished.mode == DragMode::none || ! (finished.moved || finished.created))
        return;

    // The next note drawn is as long as the one just drawn, resized or moved.
    if (juce::isPositiveAndBelow (finished.anchor, notes.size()))
        lastLength = notes[finished.anchor].length;

    auto changed = finished.created || notes.size() != finished.original.size();

    for (int i = 0; ! changed && i < notes.size(); ++i)
        changed = ! sameNote (notes.getReference (i), finished.original.getReference (i));

    if (changed)
        commit (notes, selected);
    else
        repaint();
}

void PianoRoll::mouseMove (const juce::MouseEvent& e)
{
    auto onEdge = false;

    if (! getGridArea().contains (e.getPosition()) || noteAt (e.position, &onEdge) < 0)
        setMouseCursor (juce::MouseCursor::NormalCursor);
    else
        setMouseCursor (onEdge ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::DraggingHandCursor);
}

void PianoRoll::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (e.mods.isAltDown())
    {
        setRowHeight (juce::roundToInt (rowHeight * std::pow (2.0, wheel.deltaY)) + (wheel.deltaY > 0 ? 1 : -1), e.position.y);
        return;
    }

    if (e.mods.isCommandDown())
    {
        auto anchorX = std::max ((float) keyboardWidth, e.position.x);
        auto anchorBeat = xToBeat (anchorX);
        pixelsPerBeat = juce::jlimit (minPixelsPerBeat, maxPixelsPerBeat, pixelsPerBeat * std::pow (2.0, wheel.deltaY * 2.0));
        scrollBeats = std::max (0.0, anchorBeat - (anchorX - keyboardWidth) / pixelsPerBeat);
    }
    else
    {
        auto sideways = std::abs (wheel.deltaX) > 0.0f ? wheel.deltaX : (e.mods.isShiftDown() ? wheel.deltaY : 0.0f);

        if (std::abs (sideways) > 0.0f)
            scrollBeats = std::max (0.0, scrollBeats - sideways * 300.0 / pixelsPerBeat);
        else
            scrollY -= juce::roundToInt (wheel.deltaY * 300.0f);
    }

    updateScrollBars();
    repaint();
}

bool PianoRoll::keyPressed (const juce::KeyPress& key)
{
    if (clipId.isEmpty() || typingKeyboard::isNoteKey (key))
        return false;

    if (key == juce::KeyPress ('q', juce::ModifierKeys::noModifiers, 0))
    {
        quantize();
        return true;
    }

    if (key == juce::KeyPress ('a', juce::ModifierKeys::commandModifier, 0))
    {
        for (int i = 0; i < selected.size(); ++i)
            selected.set (i, true);

        repaint();
        return true;
    }

    auto command = juce::ModifierKeys::commandModifier;

    if (key == juce::KeyPress ('v', command, 0) && ! clipboard.isEmpty())
    {
        paste();
        return true;
    }

    if (countSelected() == 0)
        return false;

    if (key == juce::KeyPress ('c', command, 0))
    {
        copySelected();
        return true;
    }

    if (key == juce::KeyPress ('x', command, 0))
    {
        copySelected();
        deleteSelected();
        return true;
    }

    if (key == juce::KeyPress ('d', command, 0))
    {
        duplicateSelected();
        return true;
    }

    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        deleteSelected();
        return true;
    }

    if (key == juce::KeyPress::escapeKey)
    {
        selectOnly (-1);
        return true;
    }

    auto octave = key.getModifiers().isShiftDown() ? 12 : 1;
    auto code = key.getKeyCode();

    if (code == juce::KeyPress::upKey)      { nudgeSelected (octave, 0.0); return true; }
    if (code == juce::KeyPress::downKey)    { nudgeSelected (-octave, 0.0); return true; }
    if (code == juce::KeyPress::rightKey)   { nudgeSelected (0, grid); return true; }
    if (code == juce::KeyPress::leftKey)    { nudgeSelected (0, -grid); return true; }

    return false;
}

} // namespace ng
