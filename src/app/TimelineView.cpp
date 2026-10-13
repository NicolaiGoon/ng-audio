#include "TimelineView.h"
#include "Widgets.h"

namespace ng
{
namespace
{
    const juce::Colour laneBackground (0xff2b2f35), laneBackgroundAlt (0xff30353b),
                       headerBackground (0xff262a2f), playheadColour (0xffff5a4e);

    constexpr double minPixelsPerBeat = 2.0, maxPixelsPerBeat = 200.0;
}

TimelineView::TimelineView (Session& s, CommandDispatcher& d)
    : session (s), dispatcher (d)
{
    for (auto* bar : { &horizontal, &vertical })
    {
        bar->setAutoHide (false);
        bar->addListener (this);
        addAndMakeVisible (bar);
    }

    vertical.setAutoHide (true);
    startTimerHz (30);
}

TimelineView::~TimelineView()
{
    horizontal.removeListener (this);
    vertical.removeListener (this);
}

//==============================================================================
void TimelineView::update (const juce::var& state)
{
    lanes.clearQuick();

    for (auto& channel : *state["channels"].getArray())
    {
        Lane lane { channel["id"].toString(), channel["name"].toString(), colourFrom (channel["colour"]), {} };

        for (auto& c : *channel["clips"].getArray())
            lane.clips.add ({ c["id"].toString(), c["name"].toString(),
                              static_cast<double> (c["start_beat"]), static_cast<double> (c["length_beats"]),
                              static_cast<bool> (c["pattern"]) });

        lanes.add (lane);
    }

    auto transport = state["transport"];
    looping = transport["looping"];
    loopBeats = { static_cast<double> (transport["loop_start_beat"]), static_cast<double> (transport["loop_end_beat"]) };

    updateScrollBars();
    repaint();
}

void TimelineView::clear()
{
    lanes.clear();
    repaint();
}

//==============================================================================
juce::Rectangle<int> TimelineView::getRulerArea() const
{
    return { headerWidth, 0, getWidth() - headerWidth - scrollBarSize, rulerHeight };
}

juce::Rectangle<int> TimelineView::getLanesArea() const
{
    return { headerWidth, rulerHeight, getWidth() - headerWidth - scrollBarSize,
             getHeight() - rulerHeight - scrollBarSize };
}

double TimelineView::getTotalBeats() const
{
    // The arrangement plus some room to grow, and never less than fills the view.
    double end = std::max (loopBeats.getEnd(), getPlayheadBeat());

    for (auto& lane : lanes)
        for (auto& clip : lane.clips)
            end = std::max (end, clip.startBeat + clip.lengthBeats);

    auto visibleBeats = getLanesArea().getWidth() / pixelsPerBeat;
    return std::max (end + 16.0, std::max (64.0, scrollBeats + visibleBeats));
}

double TimelineView::beatToX (double beat) const
{
    return headerWidth + (beat - scrollBeats) * pixelsPerBeat;
}

double TimelineView::xToBeat (double x) const
{
    return scrollBeats + (x - headerWidth) / pixelsPerBeat;
}

double TimelineView::getPlayheadBeat() const
{
    auto& edit = session.getEdit();
    return edit.tempoSequence.toBeats (edit.getTransport().getPosition()).inBeats();
}

void TimelineView::setPixelsPerBeat (double newPixelsPerBeat, float anchorX)
{
    // Keep the beat under the anchor (usually the mouse) where it is.
    auto anchorBeat = xToBeat (anchorX);
    pixelsPerBeat = juce::jlimit (minPixelsPerBeat, maxPixelsPerBeat, newPixelsPerBeat);
    scrollBeats = std::max (0.0, anchorBeat - (anchorX - headerWidth) / pixelsPerBeat);
    updateScrollBars();
    repaint();
}

void TimelineView::updateScrollBars()
{
    auto lanesArea = getLanesArea();
    auto visibleBeats = lanesArea.getWidth() / pixelsPerBeat;
    horizontal.setRangeLimits (0.0, getTotalBeats(), juce::dontSendNotification);
    horizontal.setCurrentRange (scrollBeats, visibleBeats, juce::dontSendNotification);

    auto contentHeight = lanes.size() * laneHeight;
    scrollY = juce::jlimit (0, std::max (0, contentHeight - lanesArea.getHeight()), scrollY);
    vertical.setRangeLimits (0.0, std::max (contentHeight, lanesArea.getHeight()), juce::dontSendNotification);
    vertical.setCurrentRange (scrollY, lanesArea.getHeight(), juce::dontSendNotification);
}

void TimelineView::scrollBarMoved (juce::ScrollBar* bar, double newRangeStart)
{
    if (bar == &horizontal)
        scrollBeats = std::max (0.0, newRangeStart);
    else
        scrollY = juce::roundToInt (newRangeStart);

    repaint();
}

void TimelineView::timerCallback()
{
    auto beat = getPlayheadBeat();

    if (std::abs (beat - lastPlayheadBeat) < 1.0e-4)
        return;

    lastPlayheadBeat = beat;

    // While playing, page along so the playhead stays in view.
    if (session.getEdit().getTransport().isPlaying())
    {
        auto x = beatToX (beat);
        auto lanesArea = getLanesArea();

        if (x < lanesArea.getX() || x > lanesArea.getRight() - 10)
        {
            scrollBeats = std::max (0.0, beat - 1.0);
            updateScrollBars();
        }
    }

    repaint();
}

//==============================================================================
void TimelineView::resized()
{
    horizontal.setBounds (headerWidth, getHeight() - scrollBarSize, getWidth() - headerWidth - scrollBarSize, scrollBarSize);
    vertical.setBounds (getWidth() - scrollBarSize, rulerHeight, scrollBarSize, getHeight() - rulerHeight - scrollBarSize);
    updateScrollBars();
}

void TimelineView::paint (juce::Graphics& g)
{
    g.fillAll (laneBackground);
    paintLanes (g);
    paintRuler (g);

    // Corner above the lane names.
    g.setColour (headerBackground);
    g.fillRect (0, 0, headerWidth, rulerHeight);
    g.setColour (juce::Colours::grey);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("Bar", 8, 0, headerWidth - 16, rulerHeight, juce::Justification::centredRight);

    // The playhead, over the ruler and the lanes.
    auto x = (float) beatToX (getPlayheadBeat());

    if (x >= headerWidth && x <= getRulerArea().getRight())
    {
        g.setColour (playheadColour);
        g.drawLine (x, 0.0f, x, (float) getLanesArea().getBottom(), 1.5f);

        juce::Path head;
        head.addTriangle (x - 5.0f, 0.0f, x + 5.0f, 0.0f, x, 7.0f);
        g.fillPath (head);
    }
}

void TimelineView::paintRuler (juce::Graphics& g)
{
    auto ruler = getRulerArea();
    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (ruler);
    g.setColour (headerBackground);
    g.fillRect (ruler);

    if (looping && loopBeats.getLength() > 0.0)
    {
        auto loopArea = juce::Rectangle<float> ((float) beatToX (loopBeats.getStart()), (float) ruler.getY() + 3.0f,
                                                (float) (loopBeats.getLength() * pixelsPerBeat), (float) ruler.getHeight() - 6.0f);
        g.setColour (juce::Colour (0xff4a90e2).withAlpha (0.35f));
        g.fillRoundedRectangle (loopArea, 3.0f);
    }

    auto& tempo = session.getEdit().tempoSequence;
    auto firstBar = std::max (0, tempo.toBarsAndBeats (tempo.toTime (te::BeatPosition::fromBeats (scrollBeats))).bars);
    auto endBeat = xToBeat (ruler.getRight());

    // Label fewer bars as they get narrower.
    auto barWidth = tempo.toBeats (te::tempo::BarsAndBeats { firstBar + 1, {} }).inBeats()
                      - tempo.toBeats (te::tempo::BarsAndBeats { firstBar, {} }).inBeats();
    int labelEvery = 1;

    while (labelEvery * barWidth * pixelsPerBeat < 36.0 && labelEvery < 1024)
        labelEvery *= 2;

    g.setFont (juce::FontOptions (12.0f));

    for (int bar = firstBar;; ++bar)
    {
        auto beat = tempo.toBeats (te::tempo::BarsAndBeats { bar, {} }).inBeats();

        if (beat > endBeat)
            break;

        auto x = (float) beatToX (beat);
        auto labelled = bar % labelEvery == 0;
        g.setColour (juce::Colours::grey.withAlpha (labelled ? 0.9f : 0.4f));
        g.drawVerticalLine ((int) x, labelled ? (float) ruler.getY() : (float) ruler.getBottom() - 6.0f, (float) ruler.getBottom());

        if (labelled)
        {
            g.setColour (juce::Colours::lightgrey);
            g.drawText (juce::String (bar + 1), (int) x + 4, ruler.getY(), 40, ruler.getHeight(), juce::Justification::centredLeft);
        }
    }

    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.drawHorizontalLine (ruler.getBottom() - 1, (float) ruler.getX(), (float) ruler.getRight());
}

void TimelineView::paintLanes (juce::Graphics& g)
{
    auto lanesArea = getLanesArea();

    if (lanes.isEmpty())
    {
        g.setColour (juce::Colours::grey);
        g.setFont (juce::FontOptions (14.0f));
        g.drawText ("Add an instrument to get a lane on the timeline", lanesArea, juce::Justification::centred);
    }

    // Lane backgrounds and the beat and bar grid.
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (lanesArea);

        for (int i = 0; i < lanes.size(); ++i)
        {
            auto y = lanesArea.getY() + i * laneHeight - scrollY;
            g.setColour (i % 2 == 0 ? laneBackground : laneBackgroundAlt);
            g.fillRect (lanesArea.getX(), y, lanesArea.getWidth(), laneHeight);
        }

        auto& tempo = session.getEdit().tempoSequence;
        auto endBeat = xToBeat (lanesArea.getRight());

        if (pixelsPerBeat >= 8.0)
        {
            g.setColour (juce::Colours::black.withAlpha (0.15f));

            for (auto beat = std::floor (scrollBeats); beat <= endBeat; beat += 1.0)
                g.drawVerticalLine ((int) beatToX (beat), (float) lanesArea.getY(), (float) lanesArea.getBottom());
        }

        g.setColour (juce::Colours::black.withAlpha (0.4f));
        auto firstBar = std::max (0, tempo.toBarsAndBeats (tempo.toTime (te::BeatPosition::fromBeats (scrollBeats))).bars);

        for (int bar = firstBar;; ++bar)
        {
            auto beat = tempo.toBeats (te::tempo::BarsAndBeats { bar, {} }).inBeats();

            if (beat > endBeat)
                break;

            g.drawVerticalLine ((int) beatToX (beat), (float) lanesArea.getY(), (float) lanesArea.getBottom());
        }

        for (int i = 0; i < lanes.size(); ++i)
        {
            auto y = lanesArea.getY() + i * laneHeight - scrollY;

            for (auto& clip : lanes[i].clips)
            {
                auto area = juce::Rectangle<float> ((float) beatToX (clip.startBeat), (float) y + 3.0f,
                                                    (float) (clip.lengthBeats * pixelsPerBeat), (float) laneHeight - 6.0f);

                if (area.getRight() >= lanesArea.getX() && area.getX() <= lanesArea.getRight())
                    paintClip (g, lanes[i], clip, area);
            }
        }
    }

    // Lane names on the left.
    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (0, lanesArea.getY(), headerWidth, lanesArea.getHeight());
    g.setColour (headerBackground);
    g.fillRect (0, lanesArea.getY(), headerWidth, lanesArea.getHeight());
    g.setFont (juce::FontOptions (13.0f));

    for (int i = 0; i < lanes.size(); ++i)
    {
        auto row = juce::Rectangle<int> (0, lanesArea.getY() + i * laneHeight - scrollY, headerWidth, laneHeight);
        g.setColour (lanes[i].colour);
        g.fillRect (row.getX(), row.getY() + 2, 4, row.getHeight() - 4);
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.drawText (lanes[i].name, row.withTrimmedLeft (12).withTrimmedRight (6), juce::Justification::centredLeft, true);
        g.setColour (juce::Colours::black.withAlpha (0.3f));
        g.drawHorizontalLine (row.getBottom() - 1, 0.0f, (float) getWidth());
    }
}

void TimelineView::paintClip (juce::Graphics& g, const Lane& lane, const Clip& clip, juce::Rectangle<float> area)
{
    g.setColour (lane.colour.withAlpha (0.35f));
    g.fillRoundedRectangle (area, 3.0f);
    g.setColour (lane.colour);
    g.drawRoundedRectangle (area.reduced (0.5f), 3.0f, 1.0f);

    auto title = area.removeFromTop (14.0f);

    if (title.getWidth() > 24.0f)
    {
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (clip.name, title.reduced (4.0f, 0.0f), juce::Justification::centredLeft, true);
    }

    // A small picture of the notes, scaled to the clip's pitch range.
    auto channel = session.findTrack (lane.channelId, Session::Role::channel);

    if (channel == nullptr)
        return;

    for (auto c : channel->getClips())
    {
        auto midi = dynamic_cast<te::MidiClip*> (c);

        if (midi == nullptr || c->itemID.toString() != clip.id)
            continue;

        auto& notes = midi->getSequence().getNotes();

        if (notes.isEmpty())
            return;

        int lowest = 127, highest = 0;

        for (auto n : notes)
        {
            lowest = std::min (lowest, n->getNoteNumber());
            highest = std::max (highest, n->getNoteNumber());
        }

        auto noteArea = area.reduced (2.0f, 3.0f);
        auto rows = (float) std::max (highest - lowest + 1, 6);
        auto rowHeight = noteArea.getHeight() / rows;
        auto offset = midi->getOffsetInBeats().inBeats();
        g.setColour (lane.colour.brighter (0.4f));

        for (auto n : notes)
        {
            auto start = n->getStartBeat().inBeats() - offset;

            if (start >= clip.lengthBeats || start + n->getLengthBeats().inBeats() <= 0.0)
                continue;

            auto row = (float) (highest - n->getNoteNumber()) + (rows - (float) (highest - lowest + 1)) / 2.0f;
            g.fillRect (juce::Rectangle<float> (noteArea.getX() + (float) (start * pixelsPerBeat),
                                                noteArea.getY() + row * rowHeight,
                                                std::max (1.5f, (float) (n->getLengthBeats().inBeats() * pixelsPerBeat) - 1.0f),
                                                std::max (1.5f, rowHeight - 0.5f))
                            .getIntersection (noteArea));
        }

        return;
    }
}

//==============================================================================
void TimelineView::movePlayheadTo (float x)
{
    // Snap to the nearest beat, as a grid would.
    auto beat = std::max (0.0, std::round (xToBeat (x)));
    dispatcher.execute ("set_position", makeObject ({ { "beat", beat } }));
    repaint();
}

void TimelineView::mouseDown (const juce::MouseEvent& e)
{
    if (getRulerArea().contains (e.getPosition()))
        movePlayheadTo (e.position.x);
}

void TimelineView::mouseDrag (const juce::MouseEvent& e)
{
    if (getRulerArea().contains (e.getMouseDownPosition()))
        movePlayheadTo (std::max ((float) headerWidth, e.position.x));
}

void TimelineView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (e.mods.isCommandDown())
    {
        setPixelsPerBeat (pixelsPerBeat * std::pow (2.0, wheel.deltaY * 2.0), std::max ((float) headerWidth, e.position.x));
        return;
    }

    auto sideways = std::abs (wheel.deltaX) > 0.0f ? wheel.deltaX : (e.mods.isShiftDown() ? wheel.deltaY : 0.0f);

    if (std::abs (sideways) > 0.0f)
        scrollBeats = std::max (0.0, scrollBeats - sideways * 300.0 / pixelsPerBeat);
    else
        scrollY -= juce::roundToInt (wheel.deltaY * 200.0f);

    updateScrollBars();
    repaint();
}

} // namespace ng
