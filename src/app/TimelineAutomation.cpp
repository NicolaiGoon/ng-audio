#include "TimelineView.h"
#include "Widgets.h"

// The timeline's automation lanes: one strip parameter per channel, drawn and edited under its lane.

namespace ng
{

juce::Rectangle<int> TimelineView::getAutomationButton (int lane) const
{
    return { headerWidth - 26, getLaneTop (lane) + (std::min (getLaneHeight (lane), defaultLaneHeight) - 18) / 2, 20, 18 };
}

juce::Rectangle<float> TimelineView::getAutomationArea (int lane) const
{
    auto lanesArea = getLanesArea();
    return { (float) lanesArea.getX(), (float) (getLaneTop (lane) + getLaneHeight (lane)), (float) lanesArea.getWidth(), (float) automationHeight };
}

int TimelineView::automationLaneAt (juce::Point<float> pos) const
{
    auto lane = laneAt (pos.y);

    if (! juce::isPositiveAndBelow (lane, lanes.size()) || ! isShowingAutomation (lanes[lane].channelId)
         || pos.x < (float) headerWidth || pos.y < (float) (getLaneTop (lane) + getLaneHeight (lane)) || ! getLanesArea().toFloat().contains (pos))
        return -1;

    return lane;
}

TimelineView::AutomationLane* TimelineView::getAutomation (int lane)
{
    if (! juce::isPositiveAndBelow (lane, lanes.size()))
        return nullptr;

    auto it = automationLanes.find (lanes[lane].channelId);
    return it != automationLanes.end() ? &it->second : nullptr;
}

void TimelineView::showAutomation (const juce::String& channelId, bool shouldShow)
{
    if (! shouldShow)
    {
        automationLanes.erase (channelId);
    }
    else if (! isShowingAutomation (channelId))
    {
        for (auto& lane : lanes)
            if (lane.channelId == channelId)
                refreshAutomation (lane, automationLanes[channelId]);
    }

    updateScrollBars();
    repaint();
}

void TimelineView::refreshAutomation (const Lane& lane, AutomationLane& automation)
{
    // The strip the channel plays into, or the master if it plays straight there.
    if (automation.strip != lane.output)
    {
        automation.strip = lane.output.isNotEmpty() ? lane.output : juce::String ("master");
        automation.effect = {};
        automation.parameter = "volume";
    }

    auto params = makeObject ({ { "strip", automation.strip }, { "parameter", automation.parameter } });

    if (automation.effect.isNotEmpty())
        params.getDynamicObject()->setProperty ("effect", automation.effect);

    auto target = findAutomationTarget (session, "automation", params);

    // An effect that's gone falls back to the strip's volume.
    if (target.param == nullptr && automation.effect.isNotEmpty())
    {
        automation.effect = {};
        automation.parameter = "volume";
        target = findAutomationTarget (session, "automation", makeObject ({ { "strip", automation.strip }, { "parameter", "volume" } }));
    }

    automation.valid = target.param != nullptr;
    automation.points.clearQuick();

    if (! automation.valid)
        return;

    auto described = describeAutomation (session, target);
    automation.name = target.name;
    automation.min = target.min;
    automation.max = target.max;
    automation.isVolume = target.isVolume;
    automation.value = described["value"];

    for (auto& point : *described["points"].getArray())
        automation.points.add ({ static_cast<double> (point["beat"]), static_cast<double> (point["value"]) });
}

float TimelineView::automationValueToY (const AutomationLane& automation, double value, juce::Rectangle<float> area) const
{
    // Volume is drawn as the fader shows it, so 0 dB sits most of the way up.
    auto proportion = automation.isVolume ? (double) te::decibelsToVolumeFaderPosition ((float) value)
                                          : (value - automation.min) / std::max (1.0e-9, automation.max - automation.min);
    auto inner = area.reduced (0.0f, 6.0f);
    return inner.getBottom() - (float) juce::jlimit (0.0, 1.0, proportion) * inner.getHeight();
}

double TimelineView::yToAutomationValue (const AutomationLane& automation, float y, juce::Rectangle<float> area) const
{
    auto inner = area.reduced (0.0f, 6.0f);
    auto proportion = juce::jlimit (0.0, 1.0, (double) ((inner.getBottom() - y) / inner.getHeight()));

    if (automation.isVolume)
        return juce::jlimit (automation.min, automation.max, (double) te::volumeFaderPositionToDB ((float) proportion));

    return automation.min + proportion * (automation.max - automation.min);
}

int TimelineView::automationPointAt (int lane, juce::Point<float> pos) const
{
    auto it = automationLanes.find (lanes[lane].channelId);

    if (it == automationLanes.end())
        return -1;

    auto area = getAutomationArea (lane);

    for (int i = it->second.points.size(); --i >= 0;)
    {
        auto& point = it->second.points.getReference (i);
        auto centre = juce::Point<float> ((float) beatToX (point.x), automationValueToY (it->second, point.y, area));

        if (centre.getDistanceFrom (pos) <= 7.0f)
            return i;
    }

    return -1;
}

void TimelineView::setAutomationPoints (int lane, const juce::Array<juce::Point<double>>& points)
{
    auto automation = getAutomation (lane);

    if (automation == nullptr || ! automation->valid)
        return;

    juce::Array<juce::var> list;

    for (auto& point : points)
        list.add (makeObject ({ { "beat", std::max (0.0, point.x) },
                                { "value", juce::jlimit (automation->min, automation->max, point.y) } }));

    auto params = makeObject ({ { "strip", automation->strip }, { "parameter", automation->parameter }, { "points", list } });

    if (automation->effect.isNotEmpty())
        params.getDynamicObject()->setProperty ("effect", automation->effect);

    dispatcher.execute ("set_automation", params);

    // Shown straight away, rather than when the UI next refreshes.
    refreshAutomation (lanes[lane], *automation);
    repaint();
}

void TimelineView::showAutomationMenu (int lane)
{
    auto automation = getAutomation (lane);

    if (automation == nullptr)
        return;

    auto channelId = lanes[lane].channelId;
    auto choose = [this, channelId] (juce::String effect, juce::String parameter)
    {
        auto it = automationLanes.find (channelId);

        if (it == automationLanes.end())
            return;

        it->second.effect = effect;
        it->second.parameter = parameter;

        for (auto& l : lanes)
            if (l.channelId == channelId)
                refreshAutomation (l, it->second);

        repaint();
    };

    juce::PopupMenu menu;
    menu.addSectionHeader (automation->strip == "master" ? "Master" : "Its mixer strip");
    menu.addItem ("Volume", true, automation->effect.isEmpty() && automation->parameter == "volume", [choose] { choose ({}, "volume"); });
    menu.addItem ("Pan", true, automation->effect.isEmpty() && automation->parameter == "pan", [choose] { choose ({}, "pan"); });

    if (auto list = session.findEffectList (automation->strip))
    {
        for (auto effect : Session::getEffects (*list))
        {
            juce::PopupMenu sub;
            auto effectId = effect->itemID.toString();

            for (auto param : effect->getAutomatableParameters())
            {
                auto paramId = param->paramID;
                auto ticked = automation->effect == effectId && automation->parameter == paramId;
                sub.addItem (param->getParameterName() + (param->hasAutomationPoints() ? " *" : ""), true, ticked,
                             [choose, effectId, paramId] { choose (effectId, paramId); });
            }

            if (sub.getNumItems() > 0)
                menu.addSubMenu (effect->getName(), sub);
        }
    }

    menu.addSeparator();
    menu.addItem ("Remove these points", automation->valid && ! automation->points.isEmpty(),
                  false, [this, lane] { setAutomationPoints (lane, {}); });
    menu.addItem ("Hide automation", [this, channelId] { showAutomation (channelId, false); });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

bool TimelineView::automationMouseDown (const juce::MouseEvent& e)
{
    // In the lane names: the A buttons, and the automation lanes' parameter names.
    if (e.position.x < (float) headerWidth && e.position.y >= (float) getLanesArea().getY())
    {
        auto lane = laneAt (e.position.y);

        if (! juce::isPositiveAndBelow (lane, lanes.size()))
            return false;

        if (getAutomationButton (lane).expanded (2).contains (e.getPosition()))
        {
            showAutomation (lanes[lane].channelId, ! isShowingAutomation (lanes[lane].channelId));
            return true;
        }

        if (isShowingAutomation (lanes[lane].channelId) && e.position.y >= (float) (getLaneTop (lane) + getLaneHeight (lane)))
        {
            showAutomationMenu (lane);
            return true;
        }

        // The name: a click picks the channel (the keys play it), right-click opens its menu,
        // double-click renames it.
        if (e.mods.isPopupMenu())
            showChannelMenu (lane);
        else if (e.getNumberOfClicks() >= 2)
            renameChannel (lanes[lane].channelId, lanes[lane].name);
        else
            dispatcher.execute ("arm_channel", makeObject ({ { "channel", lanes[lane].channelId } }));

        repaint();
        return true;
    }

    auto lane = automationLaneAt (e.position);

    if (lane < 0)
        return false;

    selectOnly ({});
    auto automation = getAutomation (lane);

    if (automation == nullptr || ! automation->valid)
        return true;

    auto point = automationPointAt (lane, e.position);

    if (e.mods.isPopupMenu())
    {
        if (point >= 0)
        {
            auto points = automation->points;
            points.remove (point);
            setAutomationPoints (lane, points);
        }
        else
        {
            showAutomationMenu (lane);
        }

        return true;
    }

    // A click adds a point there; dragging (one undo step) moves it, or one already there.
    dispatcher.beginGesture ("Automation");

    if (point < 0)
    {
        auto points = automation->points;
        juce::Point<double> added { std::max (0.0, snap (xToBeat (e.position.x), e.mods)),
                                    yToAutomationValue (*automation, e.position.y, getAutomationArea (lane)) };
        point = 0;

        while (point < points.size() && points[point].x <= added.x)
            ++point;

        points.insert (point, added);
        setAutomationPoints (lane, points);
    }

    automationDragLane = lane;
    automationDragPoint = point;
    return true;
}

void TimelineView::paintAutomation (juce::Graphics& g, int lane)
{
    auto it = automationLanes.find (lanes[lane].channelId);

    if (it == automationLanes.end())
        return;

    auto& automation = it->second;
    auto area = getAutomationArea (lane);
    auto colour = lanes[lane].colour;

    g.setColour (juce::Colours::black.withAlpha (0.25f));
    g.fillRect (area);
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.drawHorizontalLine ((int) area.getY(), area.getX(), area.getRight());

    if (! automation.valid)
        return;

    if (automation.points.isEmpty())
    {
        // Without points, the parameter stays where its control is.
        auto y = automationValueToY (automation, automation.value, area);
        g.setColour (colour.withAlpha (0.5f));
        const float dashes[] = { 4.0f, 4.0f };
        g.drawDashedLine ({ area.getX(), y, area.getRight(), y }, dashes, 2, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.45f));
        g.setFont (juce::FontOptions (11.0f));
        g.drawText ("Click to add automation points", area.reduced (8.0f, 4.0f), juce::Justification::topLeft);
        return;
    }

    juce::Path line;
    auto first = automation.points.getFirst();
    line.startNewSubPath (area.getX(), automationValueToY (automation, first.y, area));

    for (auto& point : automation.points)
        line.lineTo ((float) beatToX (point.x), automationValueToY (automation, point.y, area));

    line.lineTo (area.getRight(), automationValueToY (automation, automation.points.getLast().y, area));

    juce::Path fill (line);
    fill.lineTo (area.getRight(), area.getBottom());
    fill.lineTo (area.getX(), area.getBottom());
    fill.closeSubPath();
    g.setColour (colour.withAlpha (0.15f));
    g.fillPath (fill);
    g.setColour (colour);
    g.strokePath (line, juce::PathStrokeType (1.5f));

    for (int i = 0; i < automation.points.size(); ++i)
    {
        auto& point = automation.points.getReference (i);
        auto centre = juce::Point<float> ((float) beatToX (point.x), automationValueToY (automation, point.y, area));
        auto dragged = automationDragLane == lane && automationDragPoint == i;
        g.setColour (dragged ? juce::Colours::white : colour.brighter (0.4f));
        g.fillEllipse (juce::Rectangle<float> (dragged ? 9.0f : 7.0f, dragged ? 9.0f : 7.0f).withCentre (centre));

        if (dragged)
        {
            auto text = automation.isVolume ? juce::String (point.y, 1) + " dB" : juce::String (point.y, 2);
            g.setFont (juce::FontOptions (11.0f));
            g.drawText (text, juce::Rectangle<float> (60.0f, 14.0f).withPosition (centre.x + 6.0f, centre.y - 16.0f),
                        juce::Justification::centredLeft);
        }
    }

    // While playing, a dot rides the line at the playhead, showing the value being played.
    if (session.getEdit().getTransport().isPlaying())
    {
        auto beat = getPlayheadBeat();
        auto& points = automation.points;
        auto y = automationValueToY (automation, points.getFirst().y, area);

        if (beat >= points.getLast().x)
            y = automationValueToY (automation, points.getLast().y, area);
        else if (beat > points.getFirst().x)
            for (int i = 1; i < points.size(); ++i)
                if (beat < points[i].x)
                {
                    auto y1 = automationValueToY (automation, points[i - 1].y, area);
                    auto y2 = automationValueToY (automation, points[i].y, area);
                    auto t = (float) ((beat - points[i - 1].x) / std::max (1.0e-9, points[i].x - points[i - 1].x));
                    y = y1 + (y2 - y1) * t;
                    break;
                }

        auto centre = juce::Point<float> ((float) beatToX (beat), y);
        g.setColour (juce::Colours::white);
        g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre (centre));
        g.setColour (colour);
        g.drawEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre (centre), 1.5f);
    }
}

} // namespace ng
