#pragma once

#include "core/CommandDispatcher.h"

namespace ng
{

/**
    The arrangement timeline: a bar ruler, then one lane per channel showing its clips,
    with the playhead on top. Click in the ruler to move the playhead.

    The loop region shows in the ruler (dimmed while looping is off). Drag in the ruler to
    draw a new one, drag its edges to change it, or right-click
    the ruler to loop a bar or the whole project.

    Clips work as in FL Studio's playlist, with its paint, slice, delete and mute tools (see
    setTool); this describes the paint tool, the default. Click a clip to pick it, then click empty spots on
    its lane to paint copies of it there (drag to paint several in a row); on a lane with no
    clips, a click makes a new empty one-bar clip (Shift+click always does). Right-click a
    clip to delete it, or right-drag across several. Drag a clip to move it (to another lane
    too), drag an edge to trim it, and double-click it (or press Enter) to edit its notes.
    The small arrow on a clip's name (or Shift+right-click) opens its menu. Delete removes
    the selected clip, Ctrl+D duplicates it and S splits it at the playhead. Ctrl+C copies it
    and Ctrl+V pastes a copy at the playhead. The arrow keys move it by a beat (Shift: a bar)
    or to the lane above or below, and Escape deselects it. Drags snap to the beat (to 16ths
    when zoomed in); hold Alt to place freely. Each edit (and each paint or delete drag) is
    one undo step.

    Several clips can be selected: Ctrl+click adds or removes one, Ctrl+drag on the lanes
    selects those in a box and Ctrl+A selects all (a Ctrl+click that doesn't drag fits the song
    in view). Dragging a selected clip moves them all, and the keys above act on all of them,
    still one undo step.

    Audio channels show their clips' waveforms. Drop audio files (from the browser or the
    desktop) on an audio channel's lane to add them there, one after another, or anywhere else
    for a new audio channel each; a folder brings in the audio files in it. Double-click an
    empty spot on an audio lane to pick files for it. An audio clip can't be trimmed past the
    end of its file, and audio and MIDI clips each stay on their own kind of channel.

    Automation: the A button on a lane's name shows an automation lane under it, for the
    volume, pan or an effect parameter of the strip the channel plays into (click its name to
    pick which). Click to add a point, drag one to move it, and double-click or right-click it
    to remove it. Each click or drag is one undo step.

    Ctrl + mouse wheel (or the wheel over the ruler) zooms in time around the mouse, Alt +
    wheel makes the lanes taller or shorter, Shift + wheel (or a sideways scroll) scrolls
    through time, and the plain wheel scrolls the lanes. Drag the bottom edge of a lane's
    name to make just that lane taller (e.g. to see an audio clip's waveform). While
    playing, the view glides along with the playhead.

    update() takes the project state after every command; the playhead is polled.
*/
class TimelineView  : public juce::Component,
                      public juce::DragAndDropTarget,
                      public juce::FileDragAndDropTarget,
                      private juce::ScrollBar::Listener
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
    void finishMouseDrag();
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

    /** Called to open a clip: a MIDI clip in the piano roll, an audio clip in the sample editor.
        Step patterns are edited in the rack instead. */
    std::function<void (const juce::String& clipId)> onOpenClip;

    /** The clip picked last (what Enter, F2 and its menu act on), or empty. */
    juce::String getSelectedClip() const        { return selectedClip; }

    /** Every selected clip, including the one picked last. */
    const juce::StringArray& getSelectedClips() const noexcept   { return selection; }

    /** Horizontal zoom, in pixels per beat. */
    double getPixelsPerBeat() const noexcept    { return pixelsPerBeat; }
    void setPixelsPerBeat (double newPixelsPerBeat, float anchorX);

    static constexpr int headerWidth = 130, rulerHeight = 24, defaultLaneHeight = 44, automationHeight = 64, scrollBarSize = 12;

    /** Zooms in time (factor > 1 zooms in) around the middle of the view. */
    void zoomHorizontally (double factor);

    /** Makes every lane taller (factor > 1) or shorter. */
    void zoomVertically (double factor);

    /** Fits the whole song into the view. */
    void zoomToFit();

    /** What a left click on the lanes does, as FL Studio's playlist tools: paint (pick, move, trim
        and paint copies, the default), slice (cut a clip in two where it's clicked), delete
        (click or drag across clips to delete them) or mute (click or drag across clips to mute
        or unmute them). Right-click deletes whatever the tool. */
    enum class Tool { paint, slice, erase, mute };
    void setTool (Tool);
    Tool getTool() const noexcept               { return tool; }

    /** Called when the tool changes, so the header's buttons can follow. */
    std::function<void()> onToolChanged;

    /** What clip drags, painting, slicing and drops snap to: automatic (beats, or 16ths when
        zoomed in far enough), bars, beats, 8ths, 16ths, or off. Alt places freely whatever it is. */
    enum class Snap { automatic, bar, beat, eighth, sixteenth, off };
    void setSnap (Snap newSnap)                 { snapTo = newSnap; }
    Snap getSnap() const noexcept               { return snapTo; }

    /** Shows or hides a channel's automation lane. */
    void showAutomation (const juce::String& channelId, bool shouldShow);
    bool isShowingAutomation (const juce::String& channelId) const     { return automationLanes.count (channelId) > 0; }

private:
    /** A note of a MIDI clip, for the small picture of its notes: its start in beats from where
        the clip's content starts (the clip's start, unless its front is trimmed), and length. */
    struct NoteBar
    {
        double start = 0.0, length = 0.0;
        int pitch = 60;
    };

    struct Clip
    {
        juce::String id, name;
        double startBeat = 0.0, lengthBeats = 0.0;
        bool pattern = false, audio = false, missing = false;
        double maxEndBeat = -1.0;   // for an audio clip, where its file runs out
        double gainDb = 0.0, fadeInBeats = 0.0, fadeOutBeats = 0.0, sourceBpm = 0.0;   // audio only
        bool followTempo = false, muted = false;
        int patternId = 0;     // for a step pattern's clip, the pattern it plays

        te::EditItemID itemID;
        juce::Colour colour;   // the clip's own colour, or transparent for its channel's

        // A MIDI clip's notes, read once per update() rather than on every repaint.
        juce::Array<NoteBar> notes;
        int lowestNote = 127, highestNote = 0;
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

    enum class DragMode { none, move, trimStart, trimEnd, fadeIn, fadeOut, paint, erase, mute, laneHeight, select };
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
        bool stretch = false;   // Shift-dragging an audio clip's end stretches it instead of trimming
        bool group = false;     // moving the whole selection, by as much as this clip moves
        juce::Rectangle<float> box;   // for DragMode::select
    };

    Hit hitTestLanes (juce::Point<float>) const;

    /** Lanes are a common height (the vertical zoom) unless one has been dragged taller. */
    int getLaneHeight (int lane) const;
    int laneEdgeAt (juce::Point<float>) const;
    juce::Rectangle<float> getClipMenuButton (juce::Rectangle<float> clipArea) const;

    // Painting copies of a clip (FL Studio style) and deleting clips by right-dragging.
    const Clip* getBrush (int lane) const;
    void paintAt (int lane, double beat, bool forceNew);
    void eraseAt (juce::Point<float>);
    void muteAt (juce::Point<float>);
    void sliceAt (juce::Point<float>, const juce::ModifierKeys&);

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
    void showChannelMenu (int lane);
    void renameChannel (const juce::String& channelId, const juce::String& name);
    void renameSelected();
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

    // The selection: one clip or several, each command on it one undo step.
    void selectOnly (const juce::String& clipId);
    void toggleSelected (const juce::String& clipId);
    void selectInBox (juce::Rectangle<float>);
    void selectAll();
    bool isSelected (const juce::String& clipId) const      { return selection.contains (clipId); }
    /** The selected clips with their lane index. For commands, a step pattern's block counts
        once: a command on any of its clips acts on all of it. */
    std::vector<std::pair<int, const Clip*>> getSelectionWithLanes (bool onePerBlock = false) const;
    juce::Range<double> getSelectionRange() const;
    bool canMoveSelectionByLanes (int lanesDown) const;
    void moveSelection (double beats, int lanesDown);
    juce::Array<juce::var> runCommands (const juce::String& undoLabel, const juce::Array<juce::var>& commands);
    bool selectResults (const juce::Array<juce::var>& results, int primary, const std::function<juce::String (const juce::var&)>& clipOf);

    juce::Array<juce::var> itemsForFiles (const juce::StringArray&) const;
    void showDropTarget (juce::Point<float>);
    void dropItems (const juce::Array<juce::var>&, juce::Point<float>);
    te::SmartThumbnail* getThumbnail (const juce::String& file);
    void paintWaveform (juce::Graphics&, const Lane&, const Clip&, juce::Rectangle<float>, double extraOffsetBeats);
    void paintFades (juce::Graphics&, const Clip&, juce::Rectangle<float>, bool showHandles);

    void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;
    void onVBlank();
    void repaintPlayhead (double beat);

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

    /** Where following the playhead is gliding the view to (-1 when it isn't). */
    double glideTarget = -1.0;

    int baseLaneHeight = defaultLaneHeight;
    std::map<juce::String, int> laneHeights;   // by channel id, for lanes dragged to their own height
    int laneHeightDragStart = 0;

    juce::String selectedClip;
    juce::StringArray selection, copiedClips;
    Drag drag;

    /** The last clip picked on each lane (by channel id): what clicking that lane paints. */
    std::map<juce::String, juce::String> brushes;
    double paintNextBeat = 0.0;
    juce::StringArray erased;          // clips a right-drag has deleted, until the lanes catch up
    juce::StringArray toggled;         // clips a mute-tool drag has muted or unmuted
    Tool tool = Tool::paint;
    Snap snapTo = Snap::automatic;
    juce::String lastPainted;          // so a double-click on a clip just painted doesn't open it

    RulerDrag rulerDrag = RulerDrag::none;
    juce::Range<double> loopPreview;        // the loop being dragged, while rulerDrag is a loop drag
    double loopAnchor = 0.0;

    juce::ScrollBar horizontal { false }, vertical { true };

    // While audio is dragged over: the audio lane it would go on (-1 for new channels) and the beat.
    bool dropActive = false;

    /** Set by a Ctrl + left click, which fits the song in view when the mouse comes up. */
    bool fitOnClick = false;
    int dropLane = -1;
    double dropBeat = 0.0;

    std::map<juce::String, std::unique_ptr<te::SmartThumbnail>> thumbnails;   // by file path

    std::map<juce::String, AutomationLane> automationLanes;   // by channel id, for the lanes showing one
    int automationDragLane = -1, automationDragPoint = -1;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::VBlankAttachment vblank { this, [this] { onVBlank(); } };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TimelineView)
};

} // namespace ng
