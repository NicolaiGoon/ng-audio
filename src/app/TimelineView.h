#pragma once

#include "core/CommandDispatcher.h"

namespace ng
{

/**
    The arrangement timeline: a bar ruler, then one lane per channel showing its clips,
    with the playhead on top. Click or drag in the ruler to move the playhead.

    The loop region shows in the ruler (dimmed while looping is off). Drag its edges to
    change it, Shift+drag in the ruler to draw a new one, or right-click the ruler to loop
    a bar or the whole project.

    Clips: drag to move (to another lane too), drag an edge to trim, double-click an empty
    spot for a new one-bar clip, and double-click a clip (or press Enter) to edit its notes. Delete removes the selected clip, Ctrl+D duplicates it and
    S splits it at the playhead; right-click for the same actions. Ctrl+C copies it and
    Ctrl+V pastes a copy at the playhead. The arrow keys move it by a beat (Shift: a bar)
    or to the lane above or below, and Escape deselects it. Drags snap to the beat
    (to 16ths when zoomed in); hold Alt to place freely. Each edit is one command, so one
    undo step.

    Audio channels show their clips' waveforms. Drop audio files (from the browser or the
    desktop) on an audio channel's lane to add them there, one after another, or anywhere else
    for a new audio channel each; a folder brings in the audio files in it. Double-click an
    empty spot on an audio lane to pick files for it. An audio clip can't be trimmed past the
    end of its file, and audio and MIDI clips each stay on their own kind of channel.

    Automation: the A button on a lane's name shows an automation lane under it, for the
    volume, pan or an effect parameter of the strip the channel plays into (click its name to
    pick which). Click to add a point, drag one to move it, and double-click or right-click it
    to remove it. Each click or drag is one undo step.

    Ctrl + mouse wheel zooms around the mouse, Shift + wheel (or a sideways scroll)
    scrolls through time, and the plain wheel scrolls the lanes. While playing, the view
    follows the playhead.

    update() takes the project state after every command; the playhead is polled.
*/
class TimelineView  : public juce::Component,
                      public juce::DragAndDropTarget,
                      public juce::FileDragAndDropTarget,
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

    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragMove (const SourceDetails&) override;
    void itemDragExit (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragMove (const juce::StringArray&, int x, int y) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int x, int y) override;

    /** Asks for audio files and puts them on the audio channel (or, with no channel, on a new
        audio channel each) from the beat. */
    void chooseAudioFiles (const juce::String& channelId, double beat);

    /** The playhead, on the beat grid, for importing there. */
    double getPlayheadBeatForImport() const     { return std::round (getPlayheadBeat() * 4.0) / 4.0; }

    /** Called to open a clip's notes in the piano roll. Step patterns are edited in the rack instead. */
    std::function<void (const juce::String& clipId)> onOpenClip;

    /** The selected clip's id, or empty. */
    juce::String getSelectedClip() const        { return selectedClip; }

    /** Horizontal zoom, in pixels per beat. */
    double getPixelsPerBeat() const noexcept    { return pixelsPerBeat; }
    void setPixelsPerBeat (double newPixelsPerBeat, float anchorX);

    static constexpr int headerWidth = 130, rulerHeight = 24, laneHeight = 44, automationHeight = 64, scrollBarSize = 12;

    /** Shows or hides a channel's automation lane. */
    void showAutomation (const juce::String& channelId, bool shouldShow);
    bool isShowingAutomation (const juce::String& channelId) const     { return automationLanes.count (channelId) > 0; }

private:
    struct Clip
    {
        juce::String id, name;
        double startBeat = 0.0, lengthBeats = 0.0;
        bool pattern = false, audio = false, missing = false;
        double maxEndBeat = -1.0;   // for an audio clip, where its file runs out
        double gainDb = 0.0, fadeInBeats = 0.0, fadeOutBeats = 0.0, sourceBpm = 0.0;   // audio only
        bool followTempo = false;
    };

    struct Lane
    {
        juce::String channelId, name, output;
        juce::Colour colour;
        juce::Array<Clip> clips;
        bool audio = false;
    };

    /** An automation lane: one parameter of the strip a channel plays into, and its points. */
    struct AutomationLane
    {
        juce::String strip, effect, parameter { "volume" }, name;
        double min = 0.0, max = 1.0, value = 0.0;
        bool isVolume = true, valid = false;
        juce::Array<juce::Point<double>> points;   // beat, value
    };

    enum class DragMode { none, move, trimStart, trimEnd, fadeIn, fadeOut };
    enum class RulerDrag { none, playhead, loopStart, loopEnd, newLoop };

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
        double fadeIn = 0.0, fadeOut = 0.0;   // an audio clip's fades as they would be, in beats
        bool moved = false;
    };

    Hit hitTestLanes (juce::Point<float>) const;

    // Lanes are laneHeight tall, plus automationHeight when their automation shows.
    int getLaneTop (int lane) const;
    int getLaneSpan (int lane) const;
    int getContentHeight() const;
    juce::Rectangle<int> getAutomationButton (int lane) const;
    juce::Rectangle<float> getAutomationArea (int lane) const;
    int automationLaneAt (juce::Point<float>) const;
    AutomationLane* getAutomation (int lane);
    void refreshAutomation (const Lane&, AutomationLane&);
    float automationValueToY (const AutomationLane&, double value, juce::Rectangle<float> area) const;
    double yToAutomationValue (const AutomationLane&, float y, juce::Rectangle<float> area) const;
    int automationPointAt (int lane, juce::Point<float>) const;
    void setAutomationPoints (int lane, const juce::Array<juce::Point<double>>&);
    void showAutomationMenu (int lane);
    void paintAutomation (juce::Graphics&, int lane);
    bool automationMouseDown (const juce::MouseEvent&);
    juce::Rectangle<float> getClipArea (int lane, double startBeat, double lengthBeats) const;
    static juce::Rectangle<float> getFadeHandle (juce::Rectangle<float> clipArea, double beatsFromEdge, bool fadeIn, double pixelsPerBeat);
    void setSelectedAudio (const juce::String& property, const juce::var& value);
    int laneAt (float y) const;
    double snap (double beat, const juce::ModifierKeys&) const;
    const Clip* findSelectedClip (int* laneIndex = nullptr) const;
    void showClipMenu();
    void openSelected();
    void deleteSelected();
    void duplicateSelected();
    void splitSelectedAtPlayhead();
    void pasteAtPlayhead();
    void moveSelected (double beats, int lanesDown);

    juce::Array<juce::var> itemsForFiles (const juce::StringArray&) const;
    void showDropTarget (juce::Point<float>);
    void dropItems (const juce::Array<juce::var>&, juce::Point<float>);
    te::SmartThumbnail* getThumbnail (const juce::String& file);
    void paintWaveform (juce::Graphics&, const Lane&, const Clip&, juce::Rectangle<float>, double extraOffsetBeats);
    void paintFades (juce::Graphics&, const Clip&, juce::Rectangle<float>, bool showHandles);

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
    RulerDrag rulerHitTest (float x) const;
    void showRulerMenu (double beat);
    juce::Range<double> getShownLoop() const;

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

    juce::String selectedClip, copiedClip;
    Drag drag;

    RulerDrag rulerDrag = RulerDrag::none;
    juce::Range<double> loopPreview;        // the loop being dragged, while rulerDrag is a loop drag
    double loopAnchor = 0.0;

    juce::ScrollBar horizontal { false }, vertical { true };

    // While audio is dragged over: the audio lane it would go on (-1 for new channels) and the beat.
    bool dropActive = false;
    int dropLane = -1;
    double dropBeat = 0.0;

    std::map<juce::String, std::unique_ptr<te::SmartThumbnail>> thumbnails;   // by file path

    std::map<juce::String, AutomationLane> automationLanes;   // by channel id, for the lanes showing one
    int automationDragLane = -1, automationDragPoint = -1;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TimelineView)
};

} // namespace ng
