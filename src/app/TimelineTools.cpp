#include "TimelineView.h"
#include "Widgets.h"

// The timeline's playlist tools, as in FL Studio: painting copies of a clip, deleting, muting and slicing.

namespace ng
{

const TimelineView::Clip* TimelineView::getBrush (int lane) const
{
    // The selected clip if it's on this lane, else the last one picked there, else its last clip.
    auto& l = lanes.getReference (lane);
    auto find = [&l, this] (const juce::String& id) -> const Clip*
    {
        for (auto& c : l.clips)
            if (c.id == id && ! erased.contains (c.id))
                return &c;

        return nullptr;
    };

    if (auto c = find (selectedClip))
        return c;

    if (auto it = brushes.find (l.channelId); it != brushes.end())
        if (auto c = find (it->second))
            return c;

    const Clip* last = nullptr;

    for (auto& c : l.clips)
        if (! erased.contains (c.id) && (last == nullptr || c.startBeat >= last->startBeat))
            last = &c;

    return last;
}

void TimelineView::paintAt (int lane, double beat, bool forceNew)
{
    auto& l = lanes.getReference (lane);
    auto channelId = l.channelId;
    auto brush = forceNew ? nullptr : getBrush (lane);
    paintNextBeat = std::numeric_limits<double>::max();

    if (brush == nullptr)
    {
        // Nothing to copy: a new empty clip, a bar long, on an instrument lane. (Audio lanes
        // take files instead: double-click or drop them.)
        if (l.audio)
            return;

        auto& tempo = session.getEdit().tempoSequence;
        auto bar = tempo.toBarsAndBeats (tempo.toTime (te::BeatPosition::fromBeats (beat))).bars;
        auto barLength = tempo.toBeats (te::tempo::BarsAndBeats { bar + 1, {} }).inBeats()
                           - tempo.toBeats (te::tempo::BarsAndBeats { bar, {} }).inBeats();
        auto result = dispatcher.execute ("add_midi_clip", makeObject ({ { "channel", channelId },
                                                                         { "start_beat", beat },
                                                                         { "length_beats", barLength } }));
        if (result.ok)
        {
            selectedClip = lastPainted = brushes[channelId] = result.value["clip"].toString();
            paintNextBeat = beat + barLength;
        }

        return;
    }

    auto brushId = brush->id;
    auto length = brush->lengthBeats;
    paintNextBeat = beat + length;

    // Not on top of a copy that already starts there.
    for (auto& c : l.clips)
        if (! erased.contains (c.id) && std::abs (c.startBeat - beat) < 1.0e-6)
            return;

    auto result = dispatcher.execute ("duplicate_clip", makeObject ({ { "clip", brushId }, { "start_beat", beat } }));

    if (result.ok)
    {
        brushes[channelId] = brushId;
        selectedClip = lastPainted = result.value["clip"].toString();
    }
}

void TimelineView::eraseAt (juce::Point<float> pos)
{
    auto hit = hitTestLanes (pos);

    if (hit.clip < 0)
        return;

    auto id = lanes[hit.lane].clips[hit.clip].id;

    if (erased.contains (id))
        return;

    erased.add (id);

    if (dispatcher.execute ("delete_clip", makeObject ({ { "clip", id } })).ok && selectedClip == id)
        selectedClip = {};

    repaint();
}

void TimelineView::muteAt (juce::Point<float> pos)
{
    auto hit = hitTestLanes (pos);

    if (hit.clip < 0)
        return;

    auto id = lanes[hit.lane].clips[hit.clip].id;

    if (toggled.contains (id))
        return;

    toggled.add (id);
    dispatcher.execute ("mute_clip", makeObject ({ { "clip", id } }));
    repaint();
}

void TimelineView::sliceAt (juce::Point<float> pos, const juce::ModifierKeys& mods)
{
    auto hit = hitTestLanes (pos);

    if (hit.clip < 0)
        return;

    auto& clip = lanes[hit.lane].clips.getReference (hit.clip);

    if (clip.pattern)
        return;

    auto result = dispatcher.execute ("split_clip", makeObject ({ { "clip", clip.id }, { "beat", snap (xToBeat (pos.x), mods) } }));

    if (result.ok)
        selectedClip = result.value[1]["clip"].toString();
}

void TimelineView::setTool (Tool newTool)
{
    if (tool == newTool)
        return;

    tool = newTool;

    if (onToolChanged != nullptr)
        onToolChanged();

    repaint();
}

} // namespace ng
