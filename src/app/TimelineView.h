#pragma once

#include "core/CommandDispatcher.h"

namespace ng
{

/**
    The arrangement timeline: a bar ruler, then one lane per channel showing its clips,
    with the playhead on top. Click or drag in the ruler to move the playhead.

    Clips: drag to move (to another lane too), drag an edge to trim, double-click an empty
    spot for a new one-bar clip, and double-click a clip (or press Enter) to edit its notes. Delete removes the selected clip, Ctrl+D duplicates it and
    S splits it at the playhead; right-click for the same actions. Drags snap to the beat
    (to 16ths when zoomed in); hold Alt to place freely. Each edit is one command, so one
    undo step.

    Ctrl + mouse wheel zooms around the mouse, Shift + wheel (or a sideways scroll)
    scrolls through time, and the plain wheel scrolls the lanes. While playing, the view
    follows the playhead.

    update() takes the project state after every command; the playhead is polled.
*/
class TimelineView  : public juce::Component,
                      private juce::ScrollBar::Listener,
                      private juce::Timer
{
public:
    TimelineView (Session&, CommandDispatcher&);
    ~TimelineView() override;

    void update (const juce::var& state);

    /** Forgets the lanes, e.g. before the project is replaced. */
    void clear();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

    /** Called to open a clip's notes in the piano roll. Step patterns are edited in the rack instead. */
    std::function<void (const juce::String& clipId)> onOpenClip;

    /** The selected clip's id, or empty. */
    juce::String getSelectedClip() const        { return selectedClip; }

    /** Horizontal zoom, in pixels per beat. */
    double getPixelsPerBeat() const noexcept    { return pixelsPerBeat; }
    void setPixelsPerBeat (double newPixelsPerBeat, float anchorX);

    static constexpr int headerWidth = 130, rulerHeight = 24, laneHeight = 44, scrollBarSize = 12;

private:
    struct Clip
    {
        juce::String id, name;
        double startBeat = 0.0, lengthBeats = 0.0;
        bool pattern = false;
    };

    struct Lane
    {
        juce::String channelId, name;
        juce::Colour colour;
        juce::Array<Clip> clips;
    };

    enum class DragMode { none, move, trimStart, trimEnd };

    /** What's under the mouse in the lanes: a lane, maybe a clip, and which part of it. */
    struct Hit
    {
        int lane = -1, clip = -1;
        DragMode mode = DragMode::none;
    };

    struct Drag
    {
        DragMode mode = DragMode::none;
        int lane = -1, targetLane = -1;
        Clip clip;
        double start = 0.0, end = 0.0;   // where the clip would go, in beats
        bool moved = false;
    };

    Hit hitTestLanes (juce::Point<float>) const;
    int laneAt (float y) const;
    double snap (double beat, const juce::ModifierKeys&) const;
    const Clip* findSelectedClip (int* laneIndex = nullptr) const;
    void showClipMenu();
    void openSelected();
    void deleteSelected();
    void duplicateSelected();
    void splitSelectedAtPlayhead();

    void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;
    void timerCallback() override;

    juce::Rectangle<int> getRulerArea() const;
    juce::Rectangle<int> getLanesArea() const;
    double getTotalBeats() const;
    double beatToX (double beat) const;
    double xToBeat (double x) const;
    double getPlayheadBeat() const;
    void updateScrollBars();
    void movePlayheadTo (float x);

    void paintRuler (juce::Graphics&);
    void paintLanes (juce::Graphics&);
    void paintClip (juce::Graphics&, const Lane&, const Clip&, juce::Rectangle<float>, double extraOffsetBeats = 0.0);

    Session& session;
    CommandDispatcher& dispatcher;

    juce::Array<Lane> lanes;
    juce::Range<double> loopBeats;
    bool looping = false;

    double pixelsPerBeat = 20.0, scrollBeats = 0.0;
    int scrollY = 0;
    double lastPlayheadBeat = -1.0;

    juce::String selectedClip;
    Drag drag;

    juce::ScrollBar horizontal { false }, vertical { true };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TimelineView)
};

} // namespace ng
