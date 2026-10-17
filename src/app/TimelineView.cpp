#include "TimelineView.h"
#include "BrowserPanel.h"
#include "TypingKeyboard.h"
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
    erased.clear();
    juce::StringArray files;

    // The MIDI clips, to read their notes from (looked up once here, not on every repaint).
    std::map<juce::String, te::MidiClip*> midiClips;

    for (auto channel : session.getChannels())
        for (auto c : channel->getClips())
            if (auto midi = dynamic_cast<te::MidiClip*> (c))
                midiClips[midi->itemID.toString()] = midi;

    for (auto& channel : *state["channels"].getArray())
    {
        Lane lane { channel["id"].toString(), channel["name"].toString(), channel["output"].toString(),
                    colourFrom (channel["colour"]), {}, channel["kind"] == "audio" };

        for (auto& c : *channel["clips"].getArray())
        {
            Clip clip;
            clip.id = c["id"].toString();
            clip.name = c["name"].toString();
            clip.startBeat = c["start_beat"];
            clip.lengthBeats = c["length_beats"];
            clip.pattern = c["pattern"];
            clip.audio = c["type"] == "audio";
            clip.missing = c["missing"];
            clip.maxEndBeat = c.hasProperty ("max_end_beat") ? static_cast<double> (c["max_end_beat"]) : -1.0;
            clip.gainDb = c.getProperty ("gain_db", 0.0);
            clip.fadeInBeats = c.getProperty ("fade_in_beats", 0.0);
            clip.fadeOutBeats = c.getProperty ("fade_out_beats", 0.0);
            clip.followTempo = c.getProperty ("follow_tempo", false);
            clip.muted = c.getProperty ("muted", false);
            clip.sourceBpm = c.getProperty ("source_bpm", 0.0);
            clip.itemID = te::EditItemID::fromString (clip.id);

            if (c.hasProperty ("colour"))
                clip.colour = colourFrom (c["colour"]);

            if (auto found = midiClips.find (clip.id); found != midiClips.end())
            {
                auto midi = found->second;
                auto offset = midi->getOffsetInBeats().inBeats();

                for (auto n : midi->getSequence().getNotes())
                {
                    clip.notes.add ({ n->getStartBeat().inBeats() - offset, n->getLengthBeats().inBeats(), n->getNoteNumber() });
                    clip.lowestNote = std::min (clip.lowestNote, n->getNoteNumber());
                    clip.highestNote = std::max (clip.highestNote, n->getNoteNumber());
                }
            }

            lane.clips.add (clip);

            if (clip.audio)
                files.add (c["file"].toString());
        }

        lanes.add (lane);
    }

    // Selected clips that are gone are forgotten.
    auto exists = [this] (const juce::String& id)
    {
        return std::any_of (lanes.begin(), lanes.end(), [&id] (const Lane& l)
        {
            return std::any_of (l.clips.begin(), l.clips.end(), [&id] (const Clip& c) { return c.id == id; });
        });
    };

    for (int i = selection.size(); --i >= 0;)
        if (! exists (selection[i]))
            selection.remove (i);

    if (! selection.contains (selectedClip))
        selectedClip = selection.isEmpty() ? juce::String() : selection[0];

    // Lane heights and brushes of channels that are gone go too.
    for (auto it = laneHeights.begin(); it != laneHeights.end();)
        it = std::any_of (lanes.begin(), lanes.end(), [&] (const Lane& l) { return l.channelId == it->first; })
                 ? std::next (it) : laneHeights.erase (it);

    for (auto it = brushes.begin(); it != brushes.end();)
        it = std::any_of (lanes.begin(), lanes.end(), [&] (const Lane& l) { return l.channelId == it->first; })
                 ? std::next (it) : brushes.erase (it);

    // Automation lanes follow their channels, and forget the ones that are gone.
    for (auto it = automationLanes.begin(); it != automationLanes.end();)
    {
        auto found = std::find_if (lanes.begin(), lanes.end(), [&] (const Lane& l) { return l.channelId == it->first; });

        if (found == lanes.end())
        {
            it = automationLanes.erase (it);
            continue;
        }

        refreshAutomation (*found, it->second);
        ++it;
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
    automationLanes.clear();
    automationDragLane = automationDragPoint = -1;
    thumbnails.clear();
    laneHeights.clear();
    brushes.clear();
    erased.clear();
    selectOnly ({});
    copiedClips.clear();
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

    auto contentHeight = getContentHeight();
    scrollY = juce::jlimit (0, std::max (0, contentHeight - lanesArea.getHeight()), scrollY);
    vertical.setRangeLimits (0.0, std::max (contentHeight, lanesArea.getHeight()), juce::dontSendNotification);
    vertical.setCurrentRange (scrollY, lanesArea.getHeight(), juce::dontSendNotification);
}

void TimelineView::scrollBarMoved (juce::ScrollBar* bar, double newRangeStart)
{
    if (bar == &horizontal)
    {
        scrollBeats = std::max (0.0, newRangeStart);
        glideTarget = -1.0;
    }
    else
    {
        scrollY = juce::roundToInt (newRangeStart);
    }

    repaint();
}

void TimelineView::zoomHorizontally (double factor)
{
    glideTarget = -1.0;
    setPixelsPerBeat (pixelsPerBeat * factor, (float) getLanesArea().getCentreX());
}

void TimelineView::zoomVertically (double factor)
{
    auto scale = [factor] (int height) { return juce::jlimit (24, 400, juce::roundToInt (height * factor)); };
    baseLaneHeight = juce::jlimit (24, 240, juce::roundToInt (baseLaneHeight * factor));

    for (auto& [id, height] : laneHeights)
        height = scale (height);

    updateScrollBars();
    repaint();
}

void TimelineView::zoomToFit()
{
    double end = loopBeats.getEnd();

    for (auto& lane : lanes)
        for (auto& clip : lane.clips)
            end = std::max (end, clip.startBeat + clip.lengthBeats);

    glideTarget = -1.0;
    pixelsPerBeat = juce::jlimit (minPixelsPerBeat, maxPixelsPerBeat, (getLanesArea().getWidth() - 20) / std::max (4.0, end));
    scrollBeats = 0.0;
    scrollY = 0;

    // And every lane in view, as tall as there's room for.
    if (auto content = getContentHeight(); content > 0 && ! lanes.isEmpty())
        zoomVertically ((double) getLanesArea().getHeight() / (double) content);

    updateScrollBars();
    repaint();
}

void TimelineView::repaintPlayhead (double beat)
{
    auto x = juce::roundToInt (beatToX (beat));

    if (x >= headerWidth - 8 && x <= getWidth())
        repaint (x - 8, 0, 16, getLanesArea().getBottom());
}

void TimelineView::onVBlank()
{
    auto beat = getPlayheadBeat();
    auto playing = session.getEdit().getTransport().isPlaying();
    auto lanesArea = getLanesArea();
    auto visibleBeats = lanesArea.getWidth() / pixelsPerBeat;

    // While playing, glide along so the playhead stays in view, a little in from the left.
    if (playing && drag.mode == DragMode::none && rulerDrag == RulerDrag::none)
    {
        auto x = beatToX (beat);

        if (x < lanesArea.getX() || x > lanesArea.getX() + lanesArea.getWidth() * 0.85)
            glideTarget = std::max (0.0, beat - visibleBeats * 0.1);
    }
    else if (! playing)
    {
        glideTarget = -1.0;
    }

    if (glideTarget >= 0.0)
    {
        auto distance = glideTarget - scrollBeats;

        // A long way (like the loop jumping back) is a jump; a short one a glide.
        if (std::abs (distance) > visibleBeats * 3.0 || std::abs (distance) * pixelsPerBeat < 0.5)
        {
            scrollBeats = glideTarget;
            glideTarget = -1.0;
        }
        else
        {
            scrollBeats += distance * 0.22;
        }

        lastPlayheadBeat = beat;
        updateScrollBars();
        repaint();
        return;
    }

    if (std::abs (beat - lastPlayheadBeat) < 1.0e-5)
        return;

    // Only the strips the playhead left and arrived at need drawing again.
    repaintPlayhead (lastPlayheadBeat);
    repaintPlayhead (beat);
    lastPlayheadBeat = beat;
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
            g.setColour (i % 2 == 0 ? laneBackground : laneBackgroundAlt);
            g.fillRect (lanesArea.getX(), getLaneTop (i), lanesArea.getWidth(), getLaneSpan (i));
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
            paintAutomation (g, i);

            for (auto& clip : lanes[i].clips)
            {
                if (erased.contains (clip.id))
                    continue;

                auto area = getClipArea (i, clip.startBeat, clip.lengthBeats);

                if (area.getRight() < lanesArea.getX() || area.getX() > lanesArea.getRight())
                    continue;

                // While dragging, the clip's old place (or the group's) is only a faint outline.
                if (drag.moved && (clip.id == drag.clip.id || (drag.group && isSelected (clip.id))))
                {
                    g.setColour (lanes[i].colour.withAlpha (0.5f));
                    g.drawRoundedRectangle (area.reduced (0.5f), 3.0f, 1.0f);
                    continue;
                }

                paintClip (g, lanes[i], clip, area);

                if (clip.audio)
                    paintFades (g, clip, area, clip.id == selectedClip);

                // A muted clip is greyed out.
                if (clip.muted)
                {
                    g.setColour (juce::Colour (0xff1d2025).withAlpha (0.7f));
                    g.fillRoundedRectangle (area, 3.0f);
                    g.setColour (juce::Colours::white.withAlpha (0.6f));
                    g.setFont (juce::FontOptions (10.0f));
                    g.drawText ("muted", area.reduced (4.0f, 1.0f), juce::Justification::bottomRight, false);
                }

                if (isSelected (clip.id))
                {
                    g.setColour (juce::Colours::white);
                    g.drawRoundedRectangle (area.reduced (0.5f), 3.0f, 2.0f);
                }
            }
        }

        // The rest of a group being dragged, where it would go.
        if (drag.moved && drag.group)
        {
            auto beats = drag.start - drag.clip.startBeat;
            auto lanesDown = drag.targetLane - drag.lane;

            for (auto [lane, clip] : getSelectionWithLanes())
            {
                if (clip->id == drag.clip.id || ! juce::isPositiveAndBelow (lane + lanesDown, lanes.size()))
                    continue;

                auto preview = *clip;
                preview.startBeat += beats;
                auto area = getClipArea (lane + lanesDown, preview.startBeat, preview.lengthBeats);
                paintClip (g, lanes[lane], preview, area);
                g.setColour (juce::Colours::white);
                g.drawRoundedRectangle (area.reduced (0.5f), 3.0f, 2.0f);
            }
        }

        if (drag.mode == DragMode::select && ! fitOnClick)
        {
            g.setColour (juce::Colours::white.withAlpha (0.1f));
            g.fillRect (drag.box);
            g.setColour (juce::Colours::white.withAlpha (0.7f));
            g.drawRect (drag.box, 1.0f);
        }

        if (drag.moved && juce::isPositiveAndBelow (drag.targetLane, lanes.size()))
        {
            auto area = getClipArea (drag.targetLane, drag.start, drag.end - drag.start);
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
            target = { (float) lanesArea.getX(), (float) getLaneTop (dropLane),
                       (float) lanesArea.getWidth(), (float) getLaneHeight (dropLane) };
        else
            target = juce::Rectangle<float> ((float) lanesArea.getX(), (float) (lanesArea.getY() + getContentHeight() - scrollY),
                                             (float) lanesArea.getWidth(), (float) baseLaneHeight)
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

    auto armed = session.getArmedChannel();
    auto armedId = armed != nullptr ? armed->itemID.toString() : juce::String();

    for (int i = 0; i < lanes.size(); ++i)
    {
        auto row = juce::Rectangle<int> (0, getLaneTop (i), headerWidth, getLaneSpan (i));
        auto shown = isShowingAutomation (lanes[i].channelId);

        // The picked channel (the one the keys play) is lit, as in the channel rack.
        if (lanes[i].channelId == armedId)
        {
            g.setColour (lanes[i].colour.withAlpha (0.16f));
            g.fillRect (row.withHeight (getLaneHeight (i)));
        }

        g.setColour (lanes[i].colour);
        g.fillRect (row.getX(), row.getY() + 2, 4, row.getHeight() - 4);

        // The A button shows or hides the lane's automation.
        auto button = getAutomationButton (i).toFloat();
        g.setColour (shown ? lanes[i].colour : juce::Colours::white.withAlpha (0.08f));
        g.fillRoundedRectangle (button, 3.0f);
        g.setColour (shown ? juce::Colours::black : juce::Colours::white.withAlpha (0.6f));
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText ("A", button, juce::Justification::centred);

        g.setFont (juce::FontOptions (13.0f));
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.drawText (lanes[i].name, row.withHeight (getLaneHeight (i)).withTrimmedLeft (12).withTrimmedRight (30),
                    juce::Justification::centredLeft, true);

        if (shown)
        {
            auto& automation = automationLanes.at (lanes[i].channelId);
            auto label = row.withTrimmedTop (getLaneHeight (i)).withTrimmedLeft (12).withTrimmedRight (6);
            g.setColour (juce::Colours::white.withAlpha (0.65f));
            g.setFont (juce::FontOptions (12.0f));
            g.drawFittedText ((automation.valid ? automation.name : juce::String ("No parameter")) + juce::String::fromUTF8 (" \xe2\x96\xbe"),
                              label.removeFromTop (20), juce::Justification::centredLeft, 1, 0.7f);
            g.setColour (juce::Colours::grey);
            g.setFont (juce::FontOptions (11.0f));
            g.drawText (automation.strip == "master" ? "on master" : "on its strip", label.removeFromTop (16), juce::Justification::centredLeft);
        }

        g.setColour (juce::Colours::black.withAlpha (0.3f));
        g.drawHorizontalLine (row.getBottom() - 1, 0.0f, (float) getWidth());
    }
}

juce::Rectangle<float> TimelineView::getClipMenuButton (juce::Rectangle<float> clipArea) const
{
    // On the name bar, clear of an audio clip's fade-in handle in the corner.
    if (clipArea.getWidth() < 34.0f)
        return {};

    return { clipArea.getX() + 9.0f, clipArea.getY() + 1.0f, 12.0f, 12.0f };
}

void TimelineView::paintClip (juce::Graphics& g, const Lane& lane, const Clip& clip, juce::Rectangle<float> area,
                              double extraOffsetBeats)
{
    // A clip shows in its channel's colour unless it has one of its own.
    auto colour = clip.colour.isTransparent() ? lane.colour : clip.colour;

    // Audio clips are darker, so their waveform stands out.
    g.setColour (clip.audio ? colour.darker (0.9f).withAlpha (0.92f) : colour.withAlpha (0.35f));
    g.fillRoundedRectangle (area, 3.0f);

    auto title = area.removeFromTop (14.0f);
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (title.toNearestInt());
        g.setColour (colour.withAlpha (clip.audio ? 0.75f : 0.55f));
        g.fillRoundedRectangle (title.withHeight (title.getHeight() + 4.0f), 3.0f);
    }

    g.setColour (colour);
    g.drawRoundedRectangle (area.getUnion (title).reduced (0.5f), 3.0f, 1.0f);

    if (title.getWidth() > 24.0f)
    {
        auto nameArea = title.reduced (4.0f, 0.0f);

        if (auto menu = getClipMenuButton (area.getUnion (title)); ! menu.isEmpty())
        {
            juce::Path arrow;
            auto a = menu.reduced (3.0f, 4.0f);
            arrow.addTriangle (a.getX(), a.getY(), a.getRight(), a.getY(), a.getCentreX(), a.getBottom());
            g.setColour (juce::Colours::white.withAlpha (0.8f));
            g.fillPath (arrow);
            nameArea = nameArea.withLeft (menu.getRight() + 2.0f);
        }

        g.setColour (juce::Colours::white.withAlpha (0.95f));
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (clip.name, nameArea, juce::Justification::centredLeft, true);
    }

    if (clip.audio)
    {
        paintWaveform (g, lane, clip, area, extraOffsetBeats);
        return;
    }

    // A small picture of the notes, scaled to the clip's pitch range.
    if (clip.notes.isEmpty())
        return;

    auto lowest = clip.lowestNote, highest = clip.highestNote;
    auto noteArea = area.reduced (2.0f, 3.0f);
    auto rows = (float) std::max (highest - lowest + 1, 6);
    auto rowHeight = noteArea.getHeight() / rows;
    g.setColour (colour.brighter (0.4f));

    for (auto& n : clip.notes)
    {
        auto start = n.start - extraOffsetBeats;

        if (start >= clip.lengthBeats || start + n.length <= 0.0)
            continue;

        auto row = (float) (highest - n.pitch) + (rows - (float) (highest - lowest + 1)) / 2.0f;
        g.fillRect (juce::Rectangle<float> (noteArea.getX() + (float) (start * pixelsPerBeat),
                                            noteArea.getY() + row * rowHeight,
                                            std::max (1.5f, (float) (n.length * pixelsPerBeat) - 1.0f),
                                            std::max (1.5f, rowHeight - 0.5f))
                        .getIntersection (noteArea));
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
    auto wave = dynamic_cast<te::WaveAudioClip*> (te::findClipForID (session.getEdit(), clip.itemID));

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
    // in real time, at its own speed (faster when resampled up or squeezed).
    auto speed = wave->getAutoTempo() ? 1.0 : wave->getSpeedRatio();
    auto reversed = wave->getIsReversed();
    auto timeAt = [&] (float x)
    {
        if (clip.followTempo && clip.sourceBpm > 0.0)
            return te::TimeDuration::fromSeconds ((xToBeat (x) - sourceStartBeat) * 60.0 / clip.sourceBpm);

        return (tempo.toTime (te::BeatPosition::fromBeats (xToBeat (x))) - sourceStart) * speed;
    };

    if (auto thumb = getThumbnail (wave->getOriginalFile().getFullPathName()))
    {
        // Taller or shorter with the clip's gain, so a quiet take can be seen.
        auto zoom = juce::jlimit (0.05f, 4.0f, juce::Decibels::decibelsToGain ((float) clip.gainDb));
        te::TimeRange range { te::TimePosition() + timeAt (visible.getX()), te::TimePosition() + timeAt (visible.getRight()) };
        auto drawArea = visible.toNearestInt();
        juce::Graphics::ScopedSaveState saved (g);

        // Played backwards, the file is drawn from its end, mirrored.
        if (reversed)
        {
            auto end = te::TimePosition() + te::TimeDuration::fromSeconds (thumb->getTotalLength());
            range = { te::TimePosition() + (end - range.getEnd()), te::TimePosition() + (end - range.getStart()) };
            g.addTransform (juce::AffineTransform::scale (-1.0f, 1.0f, visible.getCentreX(), 0.0f));
        }

        // A centre line, then the waveform bright on the dark clip.
        auto colour = clip.colour.isTransparent() ? lane.colour : clip.colour;
        g.setColour (colour.brighter (0.6f).withAlpha (0.35f));
        g.fillRect (visible.withSizeKeepingCentre (visible.getWidth(), 1.0f));
        g.setColour (colour.brighter (1.2f).interpolatedWith (juce::Colours::white, 0.35f));

        // In a short lane both channels share the whole height, so the shape is big enough to read.
        if (drawArea.getHeight() < 70 || thumb->getNumChannels() < 2)
        {
            for (int ch = 0; ch < std::max (1, thumb->getNumChannels()); ++ch)
                thumb->drawChannel (g, drawArea, range, ch, zoom);
        }
        else
        {
            thumb->drawChannels (g, drawArea, range, zoom);
        }
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
    auto y = getLaneTop (lane);
    return { (float) beatToX (startBeat), (float) y + 3.0f, (float) (lengthBeats * pixelsPerBeat), (float) getLaneHeight (lane) - 6.0f };
}

//==============================================================================
int TimelineView::getLaneSpan (int lane) const
{
    return getLaneHeight (lane) + (juce::isPositiveAndBelow (lane, lanes.size()) && isShowingAutomation (lanes[lane].channelId) ? automationHeight : 0);
}

int TimelineView::getLaneTop (int lane) const
{
    auto y = getLanesArea().getY() - scrollY;

    for (int i = 0; i < lane; ++i)
        y += getLaneSpan (i);

    return y;
}

int TimelineView::getContentHeight() const
{
    auto height = 0;

    for (int i = 0; i < lanes.size(); ++i)
        height += getLaneSpan (i);

    return height;
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
    // A lane's automation counts as part of it; past the last lane, lanes are baseLaneHeight tall.
    for (int i = 0; i < lanes.size(); ++i)
        if (y < (float) (getLaneTop (i) + getLaneSpan (i)))
            return y < (float) getLaneTop (0) ? -1 : i;

    auto end = (float) (getLanesArea().getY() + getContentHeight() - scrollY);
    return lanes.size() + (int) std::floor ((y - end) / (float) baseLaneHeight);
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

    // Clips are only in the top part of a lane, above its automation.
    if (pos.y >= (float) (getLaneTop (hit.lane) + getLaneHeight (hit.lane)))
        return hit;

    auto& clips = lanes.getReference (hit.lane).clips;

    // Later clips are drawn on top, so look at them first.
    for (int i = clips.size(); --i >= 0;)
    {
        auto& clip = clips.getReference (i);

        if (erased.contains (clip.id))
            continue;

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

        if (pos.x < left + edge)
            hit.mode = DragMode::trimStart;
        else if (pos.x > right - edge)
            hit.mode = DragMode::trimEnd;
        else
            hit.mode = DragMode::move;

        break;
    }

    return hit;
}

double TimelineView::snap (double beat, const juce::ModifierKeys& mods) const
{
    // Alt (or snap off) places freely, to a 64th.
    if (mods.isAltDown() || snapTo == Snap::off)
        return std::round (beat * 16.0) / 16.0;

    if (snapTo == Snap::bar)
    {
        // Bars follow the time signature, so find the bar starts either side.
        auto& tempo = session.getEdit().tempoSequence;
        auto bar = tempo.toBarsAndBeats (tempo.toTime (te::BeatPosition::fromBeats (std::max (0.0, beat)))).bars;
        auto before = tempo.toBeats (te::tempo::BarsAndBeats { bar, {} }).inBeats();
        auto after = tempo.toBeats (te::tempo::BarsAndBeats { bar + 1, {} }).inBeats();
        return beat - before <= after - beat ? before : after;
    }

    // Otherwise beats, or 16ths once there's room for them, unless a grid is picked.
    auto grid = snapTo == Snap::beat ? 1.0 : snapTo == Snap::eighth ? 0.5 : snapTo == Snap::sixteenth ? 0.25
              : (pixelsPerBeat >= 60.0 ? 0.25 : 1.0);
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

int TimelineView::getLaneHeight (int lane) const
{
    if (juce::isPositiveAndBelow (lane, lanes.size()))
        if (auto it = laneHeights.find (lanes[lane].channelId); it != laneHeights.end())
            return it->second;

    return baseLaneHeight;
}

int TimelineView::laneEdgeAt (juce::Point<float> pos) const
{
    // The bottom edge of a lane's clips, in the lane names, can be dragged.
    if (pos.x >= (float) headerWidth || pos.y < (float) getLanesArea().getY())
        return -1;

    for (int i = 0; i < lanes.size(); ++i)
        if (std::abs (pos.y - (float) (getLaneTop (i) + getLaneHeight (i))) <= 3.0f)
            return i;

    return -1;
}

void TimelineView::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();

    rulerDrag = RulerDrag::none;
    glideTarget = -1.0;

    // Ctrl + left click on a clip adds it to the selection (or takes it out). Anywhere else,
    // Ctrl + drag selects the clips in a box, and a Ctrl + click that doesn't drag fits the
    // whole song in view, as in FL Studio.
    fitOnClick = false;

    if (e.mods.isCommandDown() && e.mods.isLeftButtonDown())
    {
        drag = {};
        auto hit = getRulerArea().contains (e.getPosition()) ? Hit() : hitTestLanes (e.position);

        if (hit.clip >= 0)
        {
            toggleSelected (lanes[hit.lane].clips[hit.clip].id);
            return;
        }

        fitOnClick = true;

        if (getLanesArea().contains (e.getPosition()))
        {
            drag.mode = DragMode::select;
            drag.box = { e.position, e.position };
        }

        return;
    }

    if (getRulerArea().contains (e.getPosition()))
    {
        auto beat = std::max (0.0, snap (xToBeat (e.position.x), e.mods));

        if (e.mods.isPopupMenu())
        {
            showRulerMenu (xToBeat (e.position.x));
            return;
        }

        // A click moves the playhead; dragging from there draws a new loop (see mouseDrag).
        rulerDrag = e.mods.isShiftDown() ? RulerDrag::newLoop : rulerHitTest (e.position.x);
        loopPreview = rulerDrag == RulerDrag::newLoop ? juce::Range<double> (beat, beat) : loopBeats;
        loopAnchor = beat;

        if (rulerDrag == RulerDrag::playhead)
            movePlayheadTo (e.position.x);

        return;
    }

    drag = {};

    if (auto edge = laneEdgeAt (e.position); edge >= 0 && ! e.mods.isPopupMenu())
    {
        drag.mode = DragMode::laneHeight;
        drag.lane = edge;
        laneHeightDragStart = getLaneHeight (edge);
        return;
    }

    if (automationMouseDown (e))
        return;

    auto hit = hitTestLanes (e.position);

    // Right-click deletes the clip under the mouse, and right-dragging deletes every clip it
    // passes over, as in FL Studio. Shift+right-click opens a clip's menu instead.
    if (e.mods.isPopupMenu())
    {
        if (hit.clip >= 0 && e.mods.isShiftDown())
        {
            selectOnly (lanes[hit.lane].clips[hit.clip].id);
            repaint();
            showClipMenu();
            return;
        }

        if (hit.lane >= 0)
        {
            dispatcher.beginGesture ("Delete clips");
            drag.mode = DragMode::erase;
            erased.clear();
            eraseAt (e.position);
        }

        return;
    }

    // The slice, delete and mute tools.
    if (tool == Tool::slice)
    {
        sliceAt (e.position, e.mods);
        return;
    }

    if (tool == Tool::erase || tool == Tool::mute)
    {
        if (hit.lane >= 0)
        {
            dispatcher.beginGesture (tool == Tool::erase ? "Delete clips" : "Mute clips");
            drag.mode = tool == Tool::erase ? DragMode::erase : DragMode::mute;
            erased.clear();
            toggled.clear();

            if (tool == Tool::erase)
                eraseAt (e.position);
            else
                muteAt (e.position);
        }

        return;
    }

    if (hit.clip < 0)
    {
        if (hit.lane < 0)
        {
            selectOnly ({});
            repaint();
            return;
        }

        // Paint a copy of the lane's clip here (or a new one); dragging paints more in a row.
        auto& tempo = session.getEdit().tempoSequence;
        auto clicked = std::max (0.0, xToBeat (e.position.x));
        auto beat = clicked;

        if (e.mods.isAltDown())
            beat = std::floor (clicked * 16.0) / 16.0;
        else if (pixelsPerBeat >= 60.0)
            beat = std::floor (clicked * 4.0) / 4.0;
        else if (pixelsPerBeat >= 24.0)
            beat = std::floor (clicked);
        else
            beat = tempo.toBeats (te::tempo::BarsAndBeats { tempo.toBarsAndBeats (tempo.toTime (te::BeatPosition::fromBeats (clicked))).bars, {} }).inBeats();

        dispatcher.beginGesture ("Paint clips");
        drag.mode = DragMode::paint;
        drag.lane = hit.lane;
        paintAt (hit.lane, beat, e.mods.isShiftDown());
        repaint();
        return;
    }

    auto& clip = lanes.getReference (hit.lane).clips.getReference (hit.clip);

    // Dragging one of several selected clips moves them all; a click that doesn't drag picks
    // just that one (see finishMouseDrag).
    drag.group = selection.size() > 1 && isSelected (clip.id) && hit.mode == DragMode::move;

    if (drag.group)
        selectedClip = clip.id;
    else
        selectOnly (clip.id);

    brushes[lanes[hit.lane].channelId] = clip.id;

    // Picking a clip on its own (not the second click on one just painted) means a double-click can open it.
    if (e.getNumberOfClicks() < 2)
        lastPainted = {};
    repaint();

    auto menuButton = getClipMenuButton (getClipArea (hit.lane, clip.startBeat, clip.lengthBeats));

    if (hit.mode != DragMode::fadeIn && hit.mode != DragMode::fadeOut && menuButton.expanded (2.0f).contains (e.position))
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

        // Dragging on from a click in the ruler draws a new loop from where the click was.
        if (rulerDrag == RulerDrag::playhead && e.getDistanceFromDragStartX() != 0 && std::abs (e.getDistanceFromDragStartX()) > 4)
        {
            rulerDrag = RulerDrag::newLoop;
            loopPreview = { loopAnchor, loopAnchor };
        }

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

    if (drag.mode == DragMode::select)
    {
        if (e.getDistanceFromDragStart() > 4)
            fitOnClick = false;

        if (! fitOnClick)
        {
            drag.box = juce::Rectangle<float> (e.mouseDownPosition, e.position);
            selectInBox (drag.box);
        }

        return;
    }

    if (drag.mode == DragMode::laneHeight)
    {
        if (juce::isPositiveAndBelow (drag.lane, lanes.size()))
        {
            laneHeights[lanes[drag.lane].channelId] = juce::jlimit (24, 400, laneHeightDragStart + e.getDistanceFromDragStartY());
            updateScrollBars();
            repaint();
        }

        return;
    }

    if (drag.mode == DragMode::erase)
    {
        eraseAt (e.position);
        return;
    }

    if (drag.mode == DragMode::mute)
    {
        muteAt (e.position);
        return;
    }

    if (drag.mode == DragMode::paint)
    {
        // Copies go one after another as the mouse moves right.
        for (int i = 0; i < 64 && juce::isPositiveAndBelow (drag.lane, lanes.size())
                          && xToBeat (e.position.x) >= paintNextBeat; ++i)
            paintAt (drag.lane, paintNextBeat, false);

        repaint();
        return;
    }

    if (automationDragPoint >= 0)
    {
        if (auto automation = getAutomation (automationDragLane); automation != nullptr && automation->valid
             && juce::isPositiveAndBelow (automationDragPoint, automation->points.size()))
        {
            // A point moves between its neighbours, so the points keep their order.
            auto points = automation->points;
            auto lower = automationDragPoint > 0 ? points[automationDragPoint - 1].x : 0.0;
            auto upper = automationDragPoint < points.size() - 1 ? points[automationDragPoint + 1].x : 1.0e9;
            auto beat = juce::jlimit (lower, upper, snap (xToBeat (e.position.x), e.mods));
            auto value = yToAutomationValue (*automation, e.position.y, getAutomationArea (automationDragLane));
            points.set (automationDragPoint, { beat, value });
            setAutomationPoints (automationDragLane, points);
        }

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

            // A group moves together, so none of it can go before the start.
            if (drag.group)
                drag.start = std::max (drag.start, originalStart - getSelectionRange().getStart());

            drag.end = drag.start + drag.clip.lengthBeats;

            // Step patterns stay on their own channel, and audio and MIDI clips on their own kind.
            if (! drag.clip.pattern)
            {
                auto lane = juce::jlimit (0, lanes.size() - 1, laneAt (e.position.y));

                if (lanes[lane].audio == drag.clip.audio && (! drag.group || canMoveSelectionByLanes (lane - drag.lane)))
                    drag.targetLane = lane;
            }

            break;
        }

        case DragMode::trimStart:
            drag.start = juce::jlimit (0.0, originalEnd - minLength, snap (originalStart + delta, e.mods));
            break;

        case DragMode::trimEnd:
            drag.end = std::max (originalStart + minLength, snap (originalEnd + delta, e.mods));

            // Shift-dragging an audio clip's end stretches it (FL Studio style); otherwise it trims,
            // as far as its file goes.
            drag.stretch = drag.clip.audio && e.mods.isShiftDown();

            if (drag.stretch)
                drag.end = juce::jlimit (originalStart + drag.clip.lengthBeats / 4.0, originalStart + drag.clip.lengthBeats * 4.0, drag.end);
            else if (drag.clip.maxEndBeat > 0.0)
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
        case DragMode::select:
        case DragMode::paint:
        case DragMode::erase:
        case DragMode::mute:
        case DragMode::laneHeight:
            break;
    }

    repaint();
}

void TimelineView::mouseUp (const juce::MouseEvent&)
{
    if (std::exchange (fitOnClick, false))
    {
        drag = {};
        zoomToFit();
        return;
    }

    finishMouseDrag();
}

void TimelineView::finishMouseDrag()
{
    if (automationDragPoint >= 0)
    {
        automationDragLane = automationDragPoint = -1;
        dispatcher.endGesture();
        return;
    }

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

    if (finished.mode == DragMode::paint || finished.mode == DragMode::erase || finished.mode == DragMode::mute)
    {
        dispatcher.endGesture();
        repaint();
        return;
    }

    if (finished.mode == DragMode::select)
    {
        repaint();
        return;
    }

    if (! finished.moved)
    {
        if (finished.group)
            selectOnly (finished.clip.id);

        return;
    }

    if (finished.group && finished.mode == DragMode::move)
    {
        moveSelection (finished.start - finished.clip.startBeat, finished.targetLane - finished.lane);
        repaint();
        return;
    }

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
    else if (finished.mode == DragMode::trimEnd && changedEnd && finished.stretch)
    {
        dispatcher.execute ("set_audio_clip", makeObject ({ { "clip", clipId }, { "stretch_to_beats", finished.end - finished.start } }));
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

    if (laneEdgeAt (e.position) >= 0)
    {
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
        return;
    }

    auto hit = hitTestLanes (e.position);

    if (tool != Tool::paint)
    {
        setMouseCursor (hit.clip < 0 ? juce::MouseCursor::NormalCursor
                                     : tool == Tool::slice ? juce::MouseCursor::IBeamCursor
                                                           : juce::MouseCursor::PointingHandCursor);
        return;
    }

    if (hit.clip >= 0 && hit.mode != DragMode::fadeIn && hit.mode != DragMode::fadeOut)
    {
        auto& clip = lanes.getReference (hit.lane).clips.getReference (hit.clip);

        if (getClipMenuButton (getClipArea (hit.lane, clip.startBeat, clip.lengthBeats)).expanded (2.0f).contains (e.position))
        {
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
            return;
        }
    }

    switch (hit.mode)
    {
        case DragMode::trimStart:
        case DragMode::trimEnd:  setMouseCursor (juce::MouseCursor::LeftRightResizeCursor); break;
        case DragMode::fadeIn:
        case DragMode::fadeOut:  setMouseCursor (juce::MouseCursor::PointingHandCursor); break;
        case DragMode::move:     setMouseCursor (juce::MouseCursor::DraggingHandCursor); break;
        case DragMode::none:     setMouseCursor (hit.lane >= 0 ? juce::MouseCursor::CopyingCursor : juce::MouseCursor::NormalCursor); break;
        case DragMode::paint:
        case DragMode::erase:
        case DragMode::mute:
        case DragMode::laneHeight:
        case DragMode::select:   break;
    }
}

void TimelineView::mouseDoubleClick (const juce::MouseEvent& e)
{
    // On a lane's bottom edge, a double-click puts it back to the common height.
    if (auto edge = laneEdgeAt (e.position); edge >= 0)
    {
        laneHeights.erase (lanes[edge].channelId);
        updateScrollBars();
        repaint();
        return;
    }

    // In an automation lane, a double-click removes a point (the first click added or grabbed it).
    if (auto lane = automationLaneAt (e.position); lane >= 0)
    {
        if (auto point = automationPointAt (lane, e.position); point >= 0)
        {
            auto points = getAutomation (lane)->points;
            points.remove (point);
            setAutomationPoints (lane, points);
        }

        return;
    }

    auto hit = hitTestLanes (e.position);

    if (hit.clip >= 0)
    {
        auto id = lanes[hit.lane].clips[hit.clip].id;

        // The first click just painted this one; a double-click there isn't meant to open it.
        // The other tools act on each click instead.
        if (id == lastPainted || tool != Tool::paint)
            return;

        selectOnly (id);
        openSelected();
        return;
    }

    // An empty audio lane takes files: pick some to put from the bar clicked.
    if (hit.lane < 0 || ! lanes[hit.lane].audio)
        return;

    auto& tempo = session.getEdit().tempoSequence;
    auto clicked = std::max (0.0, xToBeat (e.position.x));
    auto bar = tempo.toBarsAndBeats (tempo.toTime (te::BeatPosition::fromBeats (clicked))).bars;
    chooseAudioFiles (lanes[hit.lane].channelId, tempo.toBeats (te::tempo::BarsAndBeats { bar, {} }).inBeats());
}

bool TimelineView::keyPressed (const juce::KeyPress& key)
{
    // With the typing keyboard on, letter keys play notes.
    if (typingKeyboard::isNoteKey (key))
        return false;

    auto command = juce::ModifierKeys::commandModifier;

    if (key == juce::KeyPress ('v', command, 0) && ! copiedClips.isEmpty())
    {
        pasteAtPlayhead();
        return true;
    }

    if (key == juce::KeyPress ('a', command, 0))
    {
        selectAll();
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

    if (key == juce::KeyPress::F2Key)
    {
        renameSelected();
        return true;
    }

    if (key == juce::KeyPress ('d', command, 0))
    {
        duplicateSelected();
        return true;
    }

    if (key == juce::KeyPress ('c', command, 0))
    {
        copiedClips = selection;
        return true;
    }

    if (key == juce::KeyPress ('s', juce::ModifierKeys::noModifiers, 0))
    {
        splitSelectedAtPlayhead();
        return true;
    }

    if (key == juce::KeyPress::escapeKey)
    {
        selectOnly ({});
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
    else
        menu.addItem ("Edit sample: pitch, stretch, tempo (Enter)", ! clip->missing, false, [this] { openSelected(); });

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

    juce::PopupMenu colours;
    auto ownColour = clip->colour;
    colours.addItem ("Channel's colour", true, ownColour.isTransparent(), [this]
    {
        dispatcher.execute ("set_clip", makeObject ({ { "clip", selectedClip }, { "colour", "" } }));
    });

    for (auto& c : Session::getPalette())
    {
        juce::PopupMenu::Item swatch (juce::String::fromUTF8 ("\xe2\x96\xa0  ") + "#" + c.toDisplayString (false));
        swatch.setColour (c).setTicked (c == ownColour).setAction ([this, c]
        {
            dispatcher.execute ("set_clip", makeObject ({ { "clip", selectedClip }, { "colour", "#" + c.toDisplayString (false) } }));
        });
        colours.addItem (swatch);
    }

    menu.addItem ("Rename... (F2)", [this] { renameSelected(); });
    menu.addSubMenu ("Colour", colours);
    menu.addSeparator();
    menu.addItem ("Split at playhead (S)", canSplit, false, [this] { splitSelectedAtPlayhead(); });
    menu.addItem ("Duplicate (Ctrl+D)", [this] { duplicateSelected(); });
    menu.addItem ("Copy (Ctrl+C)", [this] { copiedClips = selection; });
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

    if (clip != nullptr && ! clip->pattern && onOpenClip != nullptr)
        onOpenClip (clip->id);
}

//==============================================================================
// The selection: selectedClip is the clip picked last (what the menu, Enter, F2 and painting
// act on), and selection holds it and every other selected clip.

void TimelineView::selectOnly (const juce::String& clipId)
{
    selection.clearQuick();

    if (clipId.isNotEmpty())
        selection.add (clipId);

    selectedClip = clipId;
}

void TimelineView::toggleSelected (const juce::String& clipId)
{
    if (isSelected (clipId))
    {
        selection.removeString (clipId);

        if (selectedClip == clipId)
            selectedClip = selection.isEmpty() ? juce::String() : selection[selection.size() - 1];
    }
    else
    {
        selection.add (clipId);
        selectedClip = clipId;
    }

    repaint();
}

void TimelineView::selectInBox (juce::Rectangle<float> box)
{
    selection.clearQuick();

    for (int i = 0; i < lanes.size(); ++i)
        for (auto& clip : lanes.getReference (i).clips)
            if (! erased.contains (clip.id) && getClipArea (i, clip.startBeat, clip.lengthBeats).intersects (box))
                selection.add (clip.id);

    if (! selection.contains (selectedClip))
        selectedClip = selection.isEmpty() ? juce::String() : selection[0];

    repaint();
}

void TimelineView::selectAll()
{
    selection.clearQuick();

    for (auto& lane : lanes)
        for (auto& clip : lane.clips)
            selection.add (clip.id);

    if (! selection.contains (selectedClip))
        selectedClip = selection.isEmpty() ? juce::String() : selection[0];

    repaint();
}

std::vector<std::pair<int, const TimelineView::Clip*>> TimelineView::getSelectionWithLanes() const
{
    std::vector<std::pair<int, const Clip*>> picked;

    for (int i = 0; i < lanes.size(); ++i)
        for (auto& clip : lanes.getReference (i).clips)
            if (isSelected (clip.id) && ! erased.contains (clip.id))
                picked.emplace_back (i, &clip);

    return picked;
}

juce::Range<double> TimelineView::getSelectionRange() const
{
    auto picked = getSelectionWithLanes();

    if (picked.empty())
        return {};

    auto start = picked.front().second->startBeat, end = start;

    for (auto [lane, clip] : picked)
    {
        start = std::min (start, clip->startBeat);
        end = std::max (end, clip->startBeat + clip->lengthBeats);
    }

    return { start, end };
}

bool TimelineView::canMoveSelectionByLanes (int lanesDown) const
{
    for (auto [lane, clip] : getSelectionWithLanes())
    {
        auto target = lane + lanesDown;

        if (! juce::isPositiveAndBelow (target, lanes.size()) || lanes[target].audio != clip->audio
             || (lanesDown != 0 && clip->pattern))
            return false;
    }

    return true;
}

namespace
{
    juce::var commandFor (const char* name, const juce::var& params)
    {
        return makeObject ({ { "command", name }, { "params", params } });
    }
}

juce::Array<juce::var> TimelineView::runCommands (const juce::String& undoLabel, const juce::Array<juce::var>& commands)
{
    if (commands.isEmpty())
        return {};

    // One command runs as itself, so its undo step keeps its name; several are one batch, and one undo step.
    if (commands.size() == 1)
    {
        auto result = dispatcher.execute (commands[0]["command"].toString(), commands[0]["params"]);
        return result.ok ? juce::Array<juce::var> { result.value } : juce::Array<juce::var>();
    }

    auto result = dispatcher.executeBatch (undoLabel, juce::var (commands));

    if (! result.ok || ! result.value.isArray())
        return {};

    return *result.value.getArray();
}

bool TimelineView::selectResults (const juce::Array<juce::var>& results, int primary,
                                  const std::function<juce::String (const juce::var&)>& clipOf)
{
    if (results.isEmpty())
        return false;

    selection.clearQuick();

    for (auto& r : results)
        if (auto id = clipOf (r); id.isNotEmpty())
            selection.addIfNotAlreadyThere (id);

    selectedClip = juce::isPositiveAndBelow (primary, results.size()) ? clipOf (results[primary]) : juce::String();

    if (! selection.contains (selectedClip))
        selectedClip = selection.isEmpty() ? juce::String() : selection[0];

    repaint();
    return true;
}

//==============================================================================
void TimelineView::deleteSelected()
{
    juce::Array<juce::var> commands;

    for (auto [lane, clip] : getSelectionWithLanes())
        commands.add (commandFor ("delete_clip", makeObject ({ { "clip", clip->id } })));

    if (! runCommands ("Delete clips", commands).isEmpty())
        selectOnly ({});
}

void TimelineView::duplicateSelected()
{
    // The copies go straight after the selection, keeping their places relative to each other.
    auto length = getSelectionRange().getLength();
    juce::Array<juce::var> commands;
    int primary = 0;

    for (auto [lane, clip] : getSelectionWithLanes())
    {
        if (clip->id == selectedClip)
            primary = commands.size();

        commands.add (commandFor ("duplicate_clip", makeObject ({ { "clip", clip->id }, { "start_beat", clip->startBeat + length } })));
    }

    selectResults (runCommands ("Duplicate clips", commands), primary, [] (const juce::var& r) { return r["clip"].toString(); });
}

void TimelineView::splitSelectedAtPlayhead()
{
    auto playhead = getPlayheadBeat();
    juce::Array<juce::var> commands;
    int primary = 0;

    for (auto [lane, clip] : getSelectionWithLanes())
    {
        if (clip->pattern || playhead <= clip->startBeat || playhead >= clip->startBeat + clip->lengthBeats)
            continue;

        if (clip->id == selectedClip)
            primary = commands.size();

        commands.add (commandFor ("split_clip", makeObject ({ { "clip", clip->id }, { "beat", playhead } })));
    }

    // Nothing to split: try the picked clip anyway, so its command says why not.
    if (commands.isEmpty() && selectedClip.isNotEmpty())
        commands.add (commandFor ("split_clip", makeObject ({ { "clip", selectedClip }, { "beat", playhead } })));

    // Keep the right-hand halves selected, so pressing S again keeps chopping forwards.
    selectResults (runCommands ("Split clips", commands), primary, [] (const juce::var& r) { return r[1]["clip"].toString(); });
}

void TimelineView::pasteAtPlayhead()
{
    // The earliest copied clip goes at the playhead and the rest keep their places after it,
    // each on its own channel.
    std::vector<const Clip*> copied;

    for (auto& lane : lanes)
        for (auto& clip : lane.clips)
            if (copiedClips.contains (clip.id))
                copied.push_back (&clip);

    if (copied.empty())
    {
        copiedClips.clear();   // they've been deleted since
        return;
    }

    auto first = copied.front()->startBeat;

    for (auto* clip : copied)
        first = std::min (first, clip->startBeat);

    auto playhead = getPlayheadBeat();
    juce::Array<juce::var> commands;

    for (auto* clip : copied)
        commands.add (commandFor ("duplicate_clip", makeObject ({ { "clip", clip->id }, { "start_beat", playhead + clip->startBeat - first } })));

    selectResults (runCommands ("Paste clips", commands), 0, [] (const juce::var& r) { return r["clip"].toString(); });
}

void TimelineView::moveSelection (double beats, int lanesDown)
{
    juce::Array<juce::var> commands;
    int primary = 0;

    for (auto [lane, clip] : getSelectionWithLanes())
    {
        if (! juce::isPositiveAndBelow (lane + lanesDown, lanes.size()))
            return;

        if (clip->id == selectedClip)
            primary = commands.size();

        commands.add (commandFor ("move_clip", makeObject ({ { "clip", clip->id },
                                                             { "start_beat", std::max (0.0, clip->startBeat + beats) },
                                                             { "channel", lanes[lane + lanesDown].channelId } })));
    }

    selectResults (runCommands ("Move clips", commands), primary, [] (const juce::var& r) { return r["clip"].toString(); });
}

void TimelineView::moveSelected (double beats, int lanesDown)
{
    int lane = -1;
    auto clip = findSelectedClip (&lane);

    if (clip == nullptr)
        return;

    // Up and down skip lanes of the other kind (audio or instrument). A group moves a lane at
    // a time, and only when all of it can.
    auto group = selection.size() > 1;
    auto target = lane;

    if (lanesDown != 0)
    {
        if (group)
            target += lanesDown > 0 ? 1 : -1;
        else
            do
                target += lanesDown > 0 ? 1 : -1;
            while (juce::isPositiveAndBelow (target, lanes.size()) && lanes[target].audio != clip->audio);
    }

    if (! juce::isPositiveAndBelow (target, lanes.size()) || (group && ! canMoveSelectionByLanes (target - lane)))
        return;

    // Step to the next beat (or bar) line that way, so a clip that's off the grid lands on it.
    auto start = clip->startBeat;

    if (beats > 0.0)
        start = std::floor (start / beats + 1.0 + 1.0e-6) * beats;
    else if (beats < 0.0)
        start = std::max (0.0, std::ceil (start / -beats - 1.0 - 1.0e-6) * -beats);

    // A group stops at the start of the song as a whole.
    auto offset = start - clip->startBeat;

    if (group)
        offset = std::max (offset, -getSelectionRange().getStart());

    if (std::abs (offset) < 1.0e-9 && target == lane)
        return;

    moveSelection (offset, target - lane);
}

void TimelineView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    glideTarget = -1.0;

    if (e.mods.isAltDown())
    {
        zoomVertically (std::pow (2.0, wheel.deltaY));
        return;
    }

    // Ctrl + wheel anywhere, or the plain wheel over the ruler (as in FL Studio), zooms.
    if (e.mods.isCommandDown() || (getRulerArea().contains (e.getPosition()) && std::abs (wheel.deltaX) <= 0.0f))
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
