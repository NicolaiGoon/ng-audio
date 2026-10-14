#include "TimelineView.h"
#include "BrowserPanel.h"
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
    setWantsKeyboardFocus (true);
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
    juce::StringArray files;

    for (auto& channel : *state["channels"].getArray())
    {
        Lane lane { channel["id"].toString(), channel["name"].toString(), colourFrom (channel["colour"]), {},
                    channel["kind"] == "audio" };

        for (auto& c : *channel["clips"].getArray())
        {
            Clip clip { c["id"].toString(), c["name"].toString(),
                        static_cast<double> (c["start_beat"]), static_cast<double> (c["length_beats"]),
                        static_cast<bool> (c["pattern"]) };
            clip.audio = c["type"] == "audio";
            clip.missing = c["missing"];
            clip.maxEndBeat = c.hasProperty ("max_end_beat") ? static_cast<double> (c["max_end_beat"]) : -1.0;
            clip.gainDb = c.getProperty ("gain_db", 0.0);
            clip.fadeInBeats = c.getProperty ("fade_in_beats", 0.0);
            clip.fadeOutBeats = c.getProperty ("fade_out_beats", 0.0);
            clip.followTempo = c.getProperty ("follow_tempo", false);
            clip.sourceBpm = c.getProperty ("source_bpm", 0.0);
            lane.clips.add (clip);

            if (clip.audio)
                files.add (c["file"].toString());
        }

        lanes.add (lane);
    }

    // Let go of waveforms nothing shows any more.
    for (auto it = thumbnails.begin(); it != thumbnails.end();)
        it = files.contains (it->first) ? std::next (it) : thumbnails.erase (it);

    auto transport = state["transport"];
    looping = transport["looping"];
    loopBeats = { static_cast<double> (transport["loop_start_beat"]), static_cast<double> (transport["loop_end_beat"]) };

    updateScrollBars();
    repaint();
}

void TimelineView::clear()
{
    lanes.clear();
    thumbnails.clear();
    selectedClip = {};
    drag = {};
    rulerDrag = RulerDrag::none;
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

    auto loop = getShownLoop();

    if (loop.getLength() > 0.0)
    {
        // Dimmed while looping is off, so the region can still be seen and set up.
        auto loopArea = juce::Rectangle<float> ((float) beatToX (loop.getStart()), (float) ruler.getY() + 3.0f,
                                                (float) (loop.getLength() * pixelsPerBeat), (float) ruler.getHeight() - 6.0f);
        auto on = looping || rulerDrag == RulerDrag::newLoop;
        g.setColour (juce::Colour (0xff4a90e2).withAlpha (on ? 0.35f : 0.12f));
        g.fillRoundedRectangle (loopArea, 3.0f);
        g.setColour (juce::Colour (0xff4a90e2).withAlpha (on ? 0.9f : 0.35f));
        g.fillRect (loopArea.withWidth (2.0f));
        g.fillRect (loopArea.withLeft (loopArea.getRight() - 2.0f));
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
        g.drawText ("Add an instrument to get a lane on the timeline, or drop audio files here", lanesArea, juce::Justification::centred);
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

                if (area.getRight() < lanesArea.getX() || area.getX() > lanesArea.getRight())
                    continue;

                // While dragging, the clip's old place is only a faint outline.
                if (drag.moved && clip.id == drag.clip.id)
                {
                    g.setColour (lanes[i].colour.withAlpha (0.5f));
                    g.drawRoundedRectangle (area.reduced (0.5f), 3.0f, 1.0f);
                    continue;
                }

                paintClip (g, lanes[i], clip, area);

                if (clip.audio)
                    paintFades (g, clip, area, clip.id == selectedClip);

                if (clip.id == selectedClip)
                {
                    g.setColour (juce::Colours::white);
                    g.drawRoundedRectangle (area.reduced (0.5f), 3.0f, 2.0f);
                }
            }
        }

        if (drag.moved && juce::isPositiveAndBelow (drag.targetLane, lanes.size()))
        {
            auto y = lanesArea.getY() + drag.targetLane * laneHeight - scrollY;
            auto area = juce::Rectangle<float> ((float) beatToX (drag.start), (float) y + 3.0f,
                                                (float) ((drag.end - drag.start) * pixelsPerBeat), (float) laneHeight - 6.0f);
            auto preview = drag.clip;
            preview.startBeat = drag.start;
            preview.lengthBeats = drag.end - drag.start;
            preview.fadeInBeats = std::min (drag.fadeIn, preview.lengthBeats);
            preview.fadeOutBeats = std::min (drag.fadeOut, preview.lengthBeats - preview.fadeInBeats);
            // Trimming the start leaves the notes where they are in the song.
            auto extraOffset = drag.mode == DragMode::trimStart ? drag.start - drag.clip.startBeat : 0.0;
            paintClip (g, lanes[drag.lane], preview, area, extraOffset);

            if (preview.audio)
                paintFades (g, preview, area, true);

            g.setColour (juce::Colours::white);
            g.drawRoundedRectangle (area.reduced (0.5f), 3.0f, 2.0f);
        }
    }

    // Where dropped audio would go: the audio lane, or new channels below the last lane.
    if (dropActive)
    {
        juce::Graphics::ScopedSaveState dropSave (g);
        g.reduceClipRegion (lanesArea);
        auto x = (float) beatToX (dropBeat);
        auto blue = juce::Colour (0xff3c8ce0);
        juce::Rectangle<float> target;

        if (dropLane >= 0)
            target = { (float) lanesArea.getX(), (float) (lanesArea.getY() + dropLane * laneHeight - scrollY),
                       (float) lanesArea.getWidth(), (float) laneHeight };
        else
            target = juce::Rectangle<float> ((float) lanesArea.getX(), (float) (lanesArea.getY() + lanes.size() * laneHeight - scrollY),
                                             (float) lanesArea.getWidth(), (float) laneHeight)
                         .getIntersection (lanesArea.toFloat());

        g.setColour (blue.withAlpha (0.15f));
        g.fillRect (target);
        g.setColour (blue);
        g.drawRect (target, 2.0f);
        g.fillRect (x - 1.0f, target.getY(), 2.0f, target.getHeight());
        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (dropLane >= 0 ? "Drop to add here" : "Drop for a new audio channel",
                    target.withTrimmedLeft (x - target.getX() + 6.0f).toNearestInt(), juce::Justification::centredLeft, false);
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

void TimelineView::paintClip (juce::Graphics& g, const Lane& lane, const Clip& clip, juce::Rectangle<float> area,
                              double extraOffsetBeats)
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
        // An audio clip's fade handles sit in the corners, so its name starts clear of them.
        g.drawText (clip.name, title.reduced (4.0f, 0.0f).withTrimmedLeft (clip.audio ? 6.0f : 0.0f),
                    juce::Justification::centredLeft, true);
    }

    if (clip.audio)
    {
        paintWaveform (g, lane, clip, area, extraOffsetBeats);
        return;
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
        auto offset = midi->getOffsetInBeats().inBeats() + extraOffsetBeats;
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

te::SmartThumbnail* TimelineView::getThumbnail (const juce::String& file)
{
    auto& thumb = thumbnails[file];

    if (thumb == nullptr)
        thumb = std::make_unique<te::SmartThumbnail> (session.getEngine(), te::AudioFile (session.getEngine(), juce::File (file)),
                                                      *this, nullptr);

    return thumb.get();
}

void TimelineView::paintWaveform (juce::Graphics& g, const Lane& lane, const Clip& clip, juce::Rectangle<float> area,
                                  double extraOffsetBeats)
{
    auto wave = dynamic_cast<te::WaveAudioClip*> (session.findClip (clip.id));

    if (wave == nullptr)
        return;

    auto waveArea = area.reduced (1.0f, 2.0f);

    if (clip.missing)
    {
        g.setColour (juce::Colour (0xffff8a80));
        g.setFont (juce::FontOptions (11.0f));
        g.drawText ("File missing", waveArea, juce::Justification::centredLeft, true);
        return;
    }

    // Only the part on screen: a long clip zoomed in would otherwise draw thousands of pixels.
    auto visible = waveArea.getIntersection (getLanesArea().toFloat());

    if (visible.isEmpty())
        return;

    // Where the file starts in the song, in beats. Trimming the start leaves the audio in place.
    auto& tempo = session.getEdit().tempoSequence;
    auto offsetBeats = tempo.toBeats (wave->getPosition().getStart()).inBeats() - session.getSourceStartBeat (*wave);
    auto sourceStartBeat = clip.startBeat - offsetBeats - extraOffsetBeats;
    auto sourceStart = tempo.toTime (te::BeatPosition::fromBeats (sourceStartBeat));

    // A clip that follows the tempo plays its file at a steady rate of beats; otherwise it plays
    // in real time.
    auto timeAt = [&] (float x)
    {
        if (clip.followTempo && clip.sourceBpm > 0.0)
            return te::TimeDuration::fromSeconds ((xToBeat (x) - sourceStartBeat) * 60.0 / clip.sourceBpm);

        return tempo.toTime (te::BeatPosition::fromBeats (xToBeat (x))) - sourceStart;
    };

    if (auto thumb = getThumbnail (wave->getOriginalFile().getFullPathName()))
    {
        // Taller or shorter with the clip's gain, so a quiet take can be seen.
        auto zoom = juce::jlimit (0.05f, 4.0f, juce::Decibels::decibelsToGain ((float) clip.gainDb));
        g.setColour (lane.colour.brighter (0.5f).withAlpha (0.9f));
        thumb->drawChannels (g, visible.toNearestInt(),
                             { te::TimePosition() + timeAt (visible.getX()), te::TimePosition() + timeAt (visible.getRight()) }, zoom);
    }
}

void TimelineView::paintFades (juce::Graphics& g, const Clip& clip, juce::Rectangle<float> area, bool showHandles)
{
    // A fade shades the part of the clip above its ramp.
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    auto inX = area.getX() + (float) (clip.fadeInBeats * pixelsPerBeat);
    auto outX = area.getRight() - (float) (clip.fadeOutBeats * pixelsPerBeat);

    if (clip.fadeInBeats > 0.0)
    {
        juce::Path shade;
        shade.addTriangle (area.getX(), area.getY(), inX, area.getY(), area.getX(), area.getBottom());
        g.fillPath (shade);
    }

    if (clip.fadeOutBeats > 0.0)
    {
        juce::Path shade;
        shade.addTriangle (outX, area.getY(), area.getRight(), area.getY(), area.getRight(), area.getBottom());
        g.fillPath (shade);
    }

    g.setColour (juce::Colours::white.withAlpha (0.7f));

    if (clip.fadeInBeats > 0.0)
        g.drawLine (area.getX(), area.getBottom(), inX, area.getY(), 1.0f);

    if (clip.fadeOutBeats > 0.0)
        g.drawLine (outX, area.getY(), area.getRight(), area.getBottom(), 1.0f);

    if (! showHandles || area.getWidth() < 24.0f)
        return;

    g.setColour (juce::Colours::white);
    g.fillRect (getFadeHandle (area, clip.fadeInBeats, true, pixelsPerBeat));
    g.fillRect (getFadeHandle (area, clip.fadeOutBeats, false, pixelsPerBeat));
}

juce::Rectangle<float> TimelineView::getFadeHandle (juce::Rectangle<float> clipArea, double beatsFromEdge, bool fadeIn,
                                                    double pixelsPerBeat)
{
    auto size = 7.0f;
    auto x = fadeIn ? clipArea.getX() + (float) (beatsFromEdge * pixelsPerBeat)
                    : clipArea.getRight() - (float) (beatsFromEdge * pixelsPerBeat);
    x = juce::jlimit (clipArea.getX(), clipArea.getRight() - size, x - size / 2.0f);
    return { x, clipArea.getY(), size, size };
}

juce::Rectangle<float> TimelineView::getClipArea (int lane, double startBeat, double lengthBeats) const
{
    auto y = getLanesArea().getY() + lane * laneHeight - scrollY;
    return { (float) beatToX (startBeat), (float) y + 3.0f, (float) (lengthBeats * pixelsPerBeat), (float) laneHeight - 6.0f };
}

//==============================================================================
void TimelineView::movePlayheadTo (float x)
{
    // Snap to the nearest beat, as a grid would.
    auto beat = std::max (0.0, std::round (xToBeat (x)));
    dispatcher.execute ("set_position", makeObject ({ { "beat", beat } }));
    repaint();
}

juce::Range<double> TimelineView::getShownLoop() const
{
    auto dragging = rulerDrag == RulerDrag::loopStart || rulerDrag == RulerDrag::loopEnd || rulerDrag == RulerDrag::newLoop;
    return dragging ? loopPreview : loopBeats;
}

TimelineView::RulerDrag TimelineView::rulerHitTest (float x) const
{
    // The loop's edges are handles; anywhere else in the ruler moves the playhead.
    if (loopBeats.getLength() > 0.0)
    {
        auto start = (float) beatToX (loopBeats.getStart()), end = (float) beatToX (loopBeats.getEnd());
        auto reach = std::min (6.0f, (end - start) / 3.0f);

        if (std::abs (x - end) <= reach)
            return RulerDrag::loopEnd;

        if (std::abs (x - start) <= reach)
            return RulerDrag::loopStart;
    }

    return RulerDrag::playhead;
}

void TimelineView::showRulerMenu (double beat)
{
    auto& tempo = session.getEdit().tempoSequence;
    auto bar = tempo.toBarsAndBeats (tempo.toTime (te::BeatPosition::fromBeats (std::max (0.0, beat)))).bars;
    auto barStart = tempo.toBeats (te::tempo::BarsAndBeats { bar, {} }).inBeats();
    auto barEnd = tempo.toBeats (te::tempo::BarsAndBeats { bar + 1, {} }).inBeats();

    juce::PopupMenu menu;
    menu.addItem ("Loop bar " + juce::String (bar + 1), [this, barStart, barEnd]
    {
        dispatcher.execute ("set_loop", makeObject ({ { "enabled", true }, { "start_beat", barStart }, { "end_beat", barEnd } }));
    });
    menu.addItem ("Loop the whole project", [this]
    {
        dispatcher.execute ("set_loop", makeObject ({ { "enabled", true }, { "whole_project", true } }));
    });
    menu.addSeparator();
    menu.addItem ("Looping", true, looping, [this]
    {
        dispatcher.execute ("set_loop", makeObject ({ { "enabled", ! looping } }));
    });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

int TimelineView::laneAt (float y) const
{
    auto lanesArea = getLanesArea();
    return (int) std::floor ((y - (float) lanesArea.getY() + (float) scrollY) / (float) laneHeight);
}

TimelineView::Hit TimelineView::hitTestLanes (juce::Point<float> pos) const
{
    Hit hit;

    if (! getLanesArea().toFloat().contains (pos))
        return hit;

    hit.lane = laneAt (pos.y);

    if (! juce::isPositiveAndBelow (hit.lane, lanes.size()))
    {
        hit.lane = -1;
        return hit;
    }

    auto& clips = lanes.getReference (hit.lane).clips;

    // Later clips are drawn on top, so look at them first.
    for (int i = clips.size(); --i >= 0;)
    {
        auto& clip = clips.getReference (i);
        auto left = (float) beatToX (clip.startBeat);
        auto right = (float) beatToX (clip.startBeat + clip.lengthBeats);

        if (pos.x < left || pos.x > right)
            continue;

        hit.clip = i;
        auto edge = std::min (6.0f, (right - left) / 4.0f);
        auto area = getClipArea (hit.lane, clip.startBeat, clip.lengthBeats);

        // An audio clip's fade handles sit in its top corners, over the trim edges.
        if (clip.audio && area.getWidth() >= 24.0f)
        {
            if (getFadeHandle (area, clip.fadeInBeats, true, pixelsPerBeat).expanded (3.0f).contains (pos))
            {
                hit.mode = DragMode::fadeIn;
                break;
            }

            if (getFadeHandle (area, clip.fadeOutBeats, false, pixelsPerBeat).expanded (3.0f).contains (pos))
            {
                hit.mode = DragMode::fadeOut;
                break;
            }
        }

        if (! clip.pattern && pos.x < left + edge)
            hit.mode = DragMode::trimStart;
        else if (! clip.pattern && pos.x > right - edge)
            hit.mode = DragMode::trimEnd;
        else
            hit.mode = DragMode::move;

        break;
    }

    return hit;
}

double TimelineView::snap (double beat, const juce::ModifierKeys& mods) const
{
    // Alt places freely (to a 64th); otherwise beats, or 16ths once there's room for them.
    auto grid = mods.isAltDown() ? 1.0 / 16.0 : (pixelsPerBeat >= 60.0 ? 0.25 : 1.0);
    return std::round (beat / grid) * grid;
}

const TimelineView::Clip* TimelineView::findSelectedClip (int* laneIndex) const
{
    for (int i = 0; i < lanes.size(); ++i)
        for (auto& clip : lanes.getReference (i).clips)
            if (clip.id == selectedClip)
            {
                if (laneIndex != nullptr)
                    *laneIndex = i;

                return &clip;
            }

    return nullptr;
}

void TimelineView::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();

    rulerDrag = RulerDrag::none;

    if (getRulerArea().contains (e.getPosition()))
    {
        auto beat = std::max (0.0, snap (xToBeat (e.position.x), e.mods));

        if (e.mods.isPopupMenu())
        {
            showRulerMenu (xToBeat (e.position.x));
            return;
        }

        rulerDrag = e.mods.isShiftDown() ? RulerDrag::newLoop : rulerHitTest (e.position.x);
        loopPreview = rulerDrag == RulerDrag::newLoop ? juce::Range<double> (beat, beat) : loopBeats;
        loopAnchor = beat;

        if (rulerDrag == RulerDrag::playhead)
            movePlayheadTo (e.position.x);

        return;
    }

    auto hit = hitTestLanes (e.position);
    drag = {};

    if (hit.clip < 0)
    {
        selectedClip = {};
        repaint();
        return;
    }

    auto& clip = lanes.getReference (hit.lane).clips.getReference (hit.clip);
    selectedClip = clip.id;
    repaint();

    if (e.mods.isPopupMenu())
    {
        showClipMenu();
        return;
    }

    drag.mode = hit.mode;
    drag.lane = drag.targetLane = hit.lane;
    drag.clip = clip;
    drag.start = clip.startBeat;
    drag.end = clip.startBeat + clip.lengthBeats;
    drag.fadeIn = clip.fadeInBeats;
    drag.fadeOut = clip.fadeOutBeats;
}

void TimelineView::mouseDrag (const juce::MouseEvent& e)
{
    if (rulerDrag != RulerDrag::none)
    {
        auto beat = std::max (0.0, snap (xToBeat (std::max ((float) headerWidth, e.position.x)), e.mods));
        auto shortest = 1.0 / 16.0;

        switch (rulerDrag)
        {
            case RulerDrag::playhead:   movePlayheadTo (std::max ((float) headerWidth, e.position.x)); break;
            case RulerDrag::loopStart:  loopPreview.setStart (std::min (beat, loopPreview.getEnd() - shortest)); break;
            case RulerDrag::loopEnd:    loopPreview.setEnd (std::max (beat, loopPreview.getStart() + shortest)); break;
            case RulerDrag::newLoop:    loopPreview = { std::min (loopAnchor, beat), std::max (loopAnchor, beat) }; break;
            case RulerDrag::none:       break;
        }

        repaint();
        return;
    }

    if (drag.mode == DragMode::none || (! drag.moved && e.getDistanceFromDragStart() < 4))
        return;

    drag.moved = true;
    auto delta = (e.position.x - e.mouseDownPosition.x) / pixelsPerBeat;
    auto originalStart = drag.clip.startBeat, originalEnd = drag.clip.startBeat + drag.clip.lengthBeats;
    auto minLength = 1.0 / 16.0;

    switch (drag.mode)
    {
        case DragMode::move:
        {
            drag.start = std::max (0.0, snap (originalStart + delta, e.mods));
            drag.end = drag.start + drag.clip.lengthBeats;

            // Step patterns stay on their own channel, and audio and MIDI clips on their own kind.
            if (! drag.clip.pattern)
            {
                auto lane = juce::jlimit (0, lanes.size() - 1, laneAt (e.position.y));

                if (lanes[lane].audio == drag.clip.audio)
                    drag.targetLane = lane;
            }

            break;
        }

        case DragMode::trimStart:
            drag.start = juce::jlimit (0.0, originalEnd - minLength, snap (originalStart + delta, e.mods));
            break;

        case DragMode::trimEnd:
            drag.end = std::max (originalStart + minLength, snap (originalEnd + delta, e.mods));

            if (drag.clip.maxEndBeat > 0.0)
                drag.end = std::min (drag.end, drag.clip.maxEndBeat);
            break;

        // Fades move freely, to a 32nd, and can't overlap each other.
        case DragMode::fadeIn:
            drag.fadeIn = juce::jlimit (0.0, drag.clip.lengthBeats - drag.clip.fadeOutBeats,
                                        std::round ((drag.clip.fadeInBeats + delta) * 32.0) / 32.0);
            break;

        case DragMode::fadeOut:
            drag.fadeOut = juce::jlimit (0.0, drag.clip.lengthBeats - drag.clip.fadeInBeats,
                                         std::round ((drag.clip.fadeOutBeats - delta) * 32.0) / 32.0);
            break;

        case DragMode::none:
            break;
    }

    repaint();
}

void TimelineView::mouseUp (const juce::MouseEvent&)
{
    auto finishedRulerDrag = std::exchange (rulerDrag, RulerDrag::none);

    if (finishedRulerDrag == RulerDrag::loopStart || finishedRulerDrag == RulerDrag::loopEnd
         || finishedRulerDrag == RulerDrag::newLoop)
    {
        // A new loop turns looping on; moving an edge leaves it as it was.
        if (loopPreview.getLength() > 0.0 && loopPreview != loopBeats)
        {
            auto params = makeObject ({ { "start_beat", loopPreview.getStart() }, { "end_beat", loopPreview.getEnd() } });

            if (finishedRulerDrag == RulerDrag::newLoop)
                params.getDynamicObject()->setProperty ("enabled", true);

            dispatcher.execute ("set_loop", params);
        }

        repaint();
        return;
    }

    auto finished = drag;
    drag = {};

    if (! finished.moved)
        return;

    auto clipId = finished.clip.id;
    auto changedLane = finished.targetLane != finished.lane;
    auto changedStart = std::abs (finished.start - finished.clip.startBeat) > 1.0e-6;
    auto changedEnd = std::abs (finished.end - (finished.clip.startBeat + finished.clip.lengthBeats)) > 1.0e-6;

    if (finished.mode == DragMode::move && (changedLane || changedStart))
    {
        auto params = makeObject ({ { "clip", clipId }, { "start_beat", finished.start } });

        if (changedLane)
            params.getDynamicObject()->setProperty ("channel", lanes[finished.targetLane].channelId);

        dispatcher.execute ("move_clip", params);
    }
    else if (finished.mode == DragMode::trimStart && changedStart)
    {
        dispatcher.execute ("resize_clip", makeObject ({ { "clip", clipId }, { "start_beat", finished.start } }));
    }
    else if (finished.mode == DragMode::trimEnd && changedEnd)
    {
        dispatcher.execute ("resize_clip", makeObject ({ { "clip", clipId }, { "end_beat", finished.end } }));
    }
    else if (finished.mode == DragMode::fadeIn && std::abs (finished.fadeIn - finished.clip.fadeInBeats) > 1.0e-6)
    {
        dispatcher.execute ("set_audio_clip", makeObject ({ { "clip", clipId }, { "fade_in_beats", finished.fadeIn } }));
    }
    else if (finished.mode == DragMode::fadeOut && std::abs (finished.fadeOut - finished.clip.fadeOutBeats) > 1.0e-6)
    {
        dispatcher.execute ("set_audio_clip", makeObject ({ { "clip", clipId }, { "fade_out_beats", finished.fadeOut } }));
    }

    repaint();
}

void TimelineView::mouseMove (const juce::MouseEvent& e)
{
    if (getRulerArea().contains (e.getPosition()))
    {
        auto onEdge = rulerHitTest (e.position.x) != RulerDrag::playhead;
        setMouseCursor (onEdge ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
        return;
    }

    switch (hitTestLanes (e.position).mode)
    {
        case DragMode::trimStart:
        case DragMode::trimEnd:  setMouseCursor (juce::MouseCursor::LeftRightResizeCursor); break;
        case DragMode::fadeIn:
        case DragMode::fadeOut:  setMouseCursor (juce::MouseCursor::PointingHandCursor); break;
        case DragMode::move:     setMouseCursor (juce::MouseCursor::DraggingHandCursor); break;
        case DragMode::none:     setMouseCursor (juce::MouseCursor::NormalCursor); break;
    }
}

void TimelineView::mouseDoubleClick (const juce::MouseEvent& e)
{
    auto hit = hitTestLanes (e.position);

    if (hit.clip >= 0)
    {
        selectedClip = lanes[hit.lane].clips[hit.clip].id;
        openSelected();
        return;
    }

    if (hit.lane < 0)
        return;

    // A new empty clip, one bar long, at the start of the bar that was clicked (or, on an
    // audio lane, files from there).
    auto& tempo = session.getEdit().tempoSequence;
    auto clicked = std::max (0.0, xToBeat (e.position.x));
    auto bar = tempo.toBarsAndBeats (tempo.toTime (te::BeatPosition::fromBeats (clicked))).bars;
    auto barStart = tempo.toBeats (te::tempo::BarsAndBeats { bar, {} }).inBeats();
    auto barEnd = tempo.toBeats (te::tempo::BarsAndBeats { bar + 1, {} }).inBeats();

    if (lanes[hit.lane].audio)
    {
        chooseAudioFiles (lanes[hit.lane].channelId, barStart);
        return;
    }

    auto result = dispatcher.execute ("add_midi_clip", makeObject ({ { "channel", lanes[hit.lane].channelId },
                                                                     { "start_beat", barStart },
                                                                     { "length_beats", barEnd - barStart } }));
    if (result.ok)
        selectedClip = result.value["clip"].toString();
}

bool TimelineView::keyPressed (const juce::KeyPress& key)
{
    auto command = juce::ModifierKeys::commandModifier;

    if (key == juce::KeyPress ('v', command, 0) && copiedClip.isNotEmpty())
    {
        pasteAtPlayhead();
        return true;
    }

    if (selectedClip.isEmpty())
        return false;

    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        deleteSelected();
        return true;
    }

    if (key == juce::KeyPress::returnKey)
    {
        openSelected();
        return true;
    }

    if (key == juce::KeyPress ('d', command, 0))
    {
        duplicateSelected();
        return true;
    }

    if (key == juce::KeyPress ('c', command, 0))
    {
        copiedClip = selectedClip;
        return true;
    }

    if (key == juce::KeyPress ('s', juce::ModifierKeys::noModifiers, 0))
    {
        splitSelectedAtPlayhead();
        return true;
    }

    if (key == juce::KeyPress::escapeKey)
    {
        selectedClip = {};
        repaint();
        return true;
    }

    // Arrows move the clip: by a beat, or a bar with Shift, and up or down a lane.
    if (! key.getModifiers().isCommandDown() && ! key.getModifiers().isAltDown())
    {
        auto& timeSig = session.getEdit().tempoSequence.getTimeSigAt (te::TimePosition());
        auto step = key.getModifiers().isShiftDown() ? timeSig.numerator.get() * 4.0 / timeSig.denominator.get() : 1.0;
        auto code = key.getKeyCode();

        if (code == juce::KeyPress::leftKey)    { moveSelected (-step, 0); return true; }
        if (code == juce::KeyPress::rightKey)   { moveSelected (step, 0); return true; }
        if (code == juce::KeyPress::upKey)      { moveSelected (0.0, -1); return true; }
        if (code == juce::KeyPress::downKey)    { moveSelected (0.0, 1); return true; }
    }

    return false;
}

void TimelineView::showClipMenu()
{
    auto clip = findSelectedClip();

    if (clip == nullptr)
        return;

    auto playhead = getPlayheadBeat();
    auto canSplit = ! clip->pattern && playhead > clip->startBeat && playhead < clip->startBeat + clip->lengthBeats;

    juce::PopupMenu menu;
    if (! clip->audio)
        menu.addItem ("Edit notes (Enter)", ! clip->pattern, false, [this] { openSelected(); });

    if (clip->audio && ! clip->missing)
    {
        juce::PopupMenu gain;

        for (auto db : { 6.0, 3.0, 0.0, -3.0, -6.0, -12.0 })
            gain.addItem ((db > 0.0 ? "+" : "") + juce::String (db, 0) + " dB", true, std::abs (clip->gainDb - db) < 0.01,
                          [this, db] { setSelectedAudio ("gain_db", db); });

        auto current = juce::String (clip->gainDb, 1);
        menu.addSubMenu ("Gain (" + (clip->gainDb > 0.0 ? "+" + current : current) + " dB)", gain);
        menu.addItem ("Follow tempo", true, clip->followTempo,
                      [this, on = ! clip->followTempo] { setSelectedAudio ("follow_tempo", on); });
        menu.addItem ("Remove fades", clip->fadeInBeats > 0.0 || clip->fadeOutBeats > 0.0, false, [this]
        {
            dispatcher.execute ("set_audio_clip", makeObject ({ { "clip", selectedClip }, { "fade_in_beats", 0.0 },
                                                                { "fade_out_beats", 0.0 } }));
        });
        menu.addSeparator();
    }

    menu.addItem ("Split at playhead (S)", canSplit, false, [this] { splitSelectedAtPlayhead(); });
    menu.addItem ("Duplicate (Ctrl+D)", [this] { duplicateSelected(); });
    menu.addItem ("Copy (Ctrl+C)", [this] { copiedClip = selectedClip; });
    menu.addSeparator();
    menu.addItem ("Delete (Del)", [this] { deleteSelected(); });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

void TimelineView::setSelectedAudio (const juce::String& property, const juce::var& value)
{
    auto params = makeObject ({ { "clip", selectedClip } });
    params.getDynamicObject()->setProperty (property, value);
    dispatcher.execute ("set_audio_clip", params);
}

void TimelineView::openSelected()
{
    auto clip = findSelectedClip();

    if (clip != nullptr && ! clip->pattern && ! clip->audio && onOpenClip != nullptr)
        onOpenClip (clip->id);
}

void TimelineView::deleteSelected()
{
    if (dispatcher.execute ("delete_clip", makeObject ({ { "clip", selectedClip } })).ok)
        selectedClip = {};
}

void TimelineView::duplicateSelected()
{
    auto result = dispatcher.execute ("duplicate_clip", makeObject ({ { "clip", selectedClip } }));

    if (result.ok)
        selectedClip = result.value["clip"].toString();
}

void TimelineView::splitSelectedAtPlayhead()
{
    auto result = dispatcher.execute ("split_clip", makeObject ({ { "clip", selectedClip }, { "beat", getPlayheadBeat() } }));

    // Keep the right-hand half selected, so pressing S again keeps chopping forwards.
    if (result.ok)
        selectedClip = result.value[1]["clip"].toString();
}

void TimelineView::pasteAtPlayhead()
{
    auto result = dispatcher.execute ("duplicate_clip", makeObject ({ { "clip", copiedClip }, { "start_beat", getPlayheadBeat() } }));

    if (result.ok)
        selectedClip = result.value["clip"].toString();
    else
        copiedClip = {};   // it's been deleted since
}

void TimelineView::moveSelected (double beats, int lanesDown)
{
    int lane = -1;
    auto clip = findSelectedClip (&lane);

    if (clip == nullptr)
        return;

    // Up and down skip lanes of the other kind (audio or instrument).
    auto target = lane;

    if (lanesDown != 0)
    {
        do
            target += lanesDown > 0 ? 1 : -1;
        while (juce::isPositiveAndBelow (target, lanes.size()) && lanes[target].audio != clip->audio);
    }

    if (! juce::isPositiveAndBelow (target, lanes.size()))
        return;

    // Step to the next beat (or bar) line that way, so a clip that's off the grid lands on it.
    auto start = clip->startBeat;

    if (beats > 0.0)
        start = std::floor (start / beats + 1.0 + 1.0e-6) * beats;
    else if (beats < 0.0)
        start = std::max (0.0, std::ceil (start / -beats - 1.0 - 1.0e-6) * -beats);

    auto result = dispatcher.execute ("move_clip", makeObject ({ { "clip", selectedClip },
                                                                 { "start_beat", start },
                                                                 { "channel", lanes[target].channelId } }));
    if (result.ok)
        selectedClip = result.value["clip"].toString();
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

//==============================================================================
bool TimelineView::isInterestedInDragSource (const SourceDetails& details)
{
    return browser::canDropOnTimeline (details.description);
}

void TimelineView::itemDragMove (const SourceDetails& details)    { showDropTarget (details.localPosition.toFloat()); }

void TimelineView::itemDragExit (const SourceDetails&)
{
    dropActive = false;
    repaint();
}

void TimelineView::itemDropped (const SourceDetails& details)
{
    dropItems ({ details.description }, details.localPosition.toFloat());
}

juce::Array<juce::var> TimelineView::itemsForFiles (const juce::StringArray& files) const
{
    juce::Array<juce::var> items;

    for (auto& f : files)
        if (auto item = browser::itemForFile (juce::File (f), session.getAudioFileWildcard()); item.isObject())
            items.add (item);

    return items;
}

bool TimelineView::isInterestedInFileDrag (const juce::StringArray& files)
{
    return ! itemsForFiles (files).isEmpty();
}

void TimelineView::fileDragMove (const juce::StringArray&, int x, int y)    { showDropTarget ({ (float) x, (float) y }); }

void TimelineView::fileDragExit (const juce::StringArray&)
{
    dropActive = false;
    repaint();
}

void TimelineView::filesDropped (const juce::StringArray& files, int x, int y)
{
    dropItems (itemsForFiles (files), { (float) x, (float) y });
}

void TimelineView::showDropTarget (juce::Point<float> pos)
{
    auto lane = laneAt (pos.y);
    dropActive = true;
    dropLane = juce::isPositiveAndBelow (lane, lanes.size()) && lanes[lane].audio ? lane : -1;
    dropBeat = std::max (0.0, snap (xToBeat (std::max ((float) headerWidth, pos.x)), juce::ModifierKeys::getCurrentModifiers()));
    repaint();
}

void TimelineView::dropItems (const juce::Array<juce::var>& items, juce::Point<float> pos)
{
    showDropTarget (pos);
    dropActive = false;
    repaint();

    auto channel = dropLane >= 0 ? lanes[dropLane].channelId : juce::String();
    browser::showDropError (this, browser::dropOnTimeline (dispatcher, channel, dropBeat, items, session.getAudioFileWildcard()));
}

void TimelineView::chooseAudioFiles (const juce::String& channelId, double beat)
{
    chooser = std::make_unique<juce::FileChooser> ("Import audio", juce::File(), session.getAudioFileWildcard());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                            | juce::FileBrowserComponent::canSelectMultipleItems,
                          [this, channelId, beat] (const juce::FileChooser& fc)
                          {
                              juce::Array<juce::var> items;

                              for (auto& file : fc.getResults())
                                  items.add (browser::sampleItem (file));

                              if (! items.isEmpty())
                                  browser::showDropError (this, browser::dropOnTimeline (dispatcher, channelId, beat, items,
                                                                                        session.getAudioFileWildcard()));
                          });
}

} // namespace ng
