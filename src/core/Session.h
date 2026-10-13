#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <optional>

namespace ng
{
namespace te = tracktion;

/**
    The open project: one Tracktion Edit plus the Ng-specific view of it.

    Ng's model on top of Tracktion's tracks:
      - a *channel* is an instrument (a track holding the instrument plugin and its clips)
      - a *strip* is a mixer strip (a track used as a bus, with the faders and FX)

    Creating a channel also creates a strip with the same name and colour and links
    the two ("linked by default"). Routing a channel to a different strip detaches it
    ("detachable by choice"). Many channels can feed one strip.

    The roles and links are stored as properties on Tracktion's track state, so they
    are saved with the Edit and take part in its undo history.
*/
/** The extension Ng DAW gives project files. They hold a Tracktion Edit as XML. */
constexpr const char* projectFileExtension = ".ngdaw";

/** A note in a MIDI clip, timed in beats from the clip's start (as the piano roll shows it).
    A note before 0 or past the clip's end is in a part of the clip that has been trimmed off. */
struct ClipNote
{
    int pitch = 60;
    double start = 0.0, length = 1.0;
    int velocity = 100;
};

class Session
{
public:
    explicit Session (te::Engine&);
    ~Session();

    te::Engine& getEngine() noexcept                { return engine; }
    te::Edit& getEdit() noexcept                    { return *edit; }
    juce::UndoManager& getUndoManager() noexcept    { return edit->getUndoManager(); }

    //==============================================================================
    /** Replaces the open project with an empty one. */
    void newProject();

    /** Replaces the open project with the one in the file. Returns an error, or an empty
        string on success. The open project is left alone if the file can't be read. */
    juce::String openProject (const juce::File&);

    /** Saves to the file, or to the project's own file when none is given (an error if it
        has never been saved). Returns an error, or an empty string on success. */
    juce::String saveProject (const juce::File& = {});

    /** The project's file, empty until it is first saved or when it was opened from nowhere. */
    juce::File getFile() const                      { return file; }
    juce::String getProjectName() const;

    /** True when the project differs from what was last saved, opened or created. */
    bool hasUnsavedChanges() const;

    /** Treats the project as it is now as saved, e.g. a starter project nobody has touched yet. */
    void markAsSaved();

    /** Called before the open Edit is replaced (new or open), while it still exists, so
        anything holding on to its tracks or plugins can let go. */
    std::function<void()> onEditAboutToChange;

    //==============================================================================
    enum class Role { none, channel, strip };

    static Role getRole (const te::Track&);
    void setRole (te::Track&, Role);

    juce::Array<te::AudioTrack*> getChannels() const;
    juce::Array<te::AudioTrack*> getStrips() const;

    /** Finds a channel or strip by its id or, failing that, its name. */
    te::AudioTrack* findTrack (const juce::String& idOrName, Role) const;

    /** The strip a channel was created with, whether or not it still feeds it. */
    te::AudioTrack* getOwnStrip (const te::AudioTrack& channel) const;
    void setOwnStrip (te::AudioTrack& channel, te::AudioTrack* strip);

    /** True while the channel feeds its own strip, so name and colour stay in sync. */
    bool isLinked (const te::AudioTrack& channel) const;

    /** Where a channel's audio currently goes (nullptr means straight to master). */
    te::AudioTrack* getDestinationStrip (const te::AudioTrack& channel) const;

    /** For a linked channel, its strip; for a strip, the channel linked to it. Renaming or
        recolouring either one changes both. Returns nullptr when there is no live link. */
    te::AudioTrack* getLinkedPartner (const te::AudioTrack&) const;

    /** Tracktion stores a track's output as "track <number>", so deleting a track shifts what
        those numbers mean. writeTrackOutputs() rewrites them for the current track order
        (part of the undo step, called after deleting tracks); refreshTrackOutputs() re-reads
        them (called after undo, redo and rollback, when the stored numbers match the order again). */
    void writeTrackOutputs();
    void refreshTrackOutputs();

    /** Brings Tracktion's live state back in line with the saved state after undo, redo or
        a rollback: track outputs (above) and fader and pan parameters. */
    void refreshAfterHistoryChange();

    /** Puts tracks, and each track's clips, plugins and outputs, in the order Tracktion keeps them in.
        Tracktion sorts them itself a moment after a change, as a separate undo step, so a
        following Undo would only undo the sort. Doing it inside the command's own undo step
        leaves Tracktion nothing to move. Called by the dispatcher after every command. */
    void sortTrackContents();

    /** Finds a channel or a mixer strip by id or name (channels are searched first). */
    te::AudioTrack* findAnyTrack (const juce::String& idOrName) const;

    /** A channel's step pattern as 'x' (on) and '.' (off), one per 16th note (16 off by
        default), and the MIDI note the steps play (default 60). Changed by set_steps. */
    static juce::String getSteps (const te::AudioTrack& channel);
    static int getStepPitch (const te::AudioTrack& channel);

    /** True for the clip set_steps keeps in sync with a channel's step pattern. */
    static bool isPatternClip (const te::Clip&);

    /** A clip on any channel, by its id. */
    te::Clip* findClip (const juce::String& id) const;

    /** A MIDI clip's notes, sorted by start and then pitch. */
    static juce::Array<ClipNote> getNotes (const te::MidiClip&);

    //==============================================================================
    /** The loop region in beats. Unless a custom region is set, it covers the whole
        project, rounded up to whole bars (at least one bar). */
    juce::Range<double> getLoopBeats() const;
    bool hasCustomLoop() const;

    /** Sets a custom loop region in beats, or clears it (back to the whole project). */
    void setCustomLoop (std::optional<juce::Range<double>> beats);

    /** Moves Tracktion's loop points (kept in seconds) to match the loop region in beats.
        Called after every command, so the loop follows tempo changes and new clips. */
    void updateLoopRange();

    //==============================================================================
    /** The channel that MIDI input plays and records into, or nullptr. Only one channel is
        armed at a time; arming one disarms the others. This is part of the setup, not the
        song, so it isn't saved or undone. */
    te::AudioTrack* getArmedChannel() const;
    void setArmedChannel (te::AudioTrack*);

    /** The names of the MIDI inputs (keyboards and controllers) the armed channel listens to. */
    juce::StringArray getMidiInputNames() const;

    /** Plays a MIDI message into the armed channel as if it came from a MIDI keyboard,
        e.g. from the on-screen keyboard. Records it too while recording. */
    void sendLiveMidi (const juce::MidiMessage&);

    /** Points every MIDI input at the armed channel, or at nothing. Called when arming and
        when the project or the MIDI devices change. */
    void routeMidiInputs();

    /** Where the playhead is, as bar.beat.sixteenth counting from 1 (e.g. "3.2.1"). */
    juce::String getPositionText() const;

    /** The level meter on the master output, for the mixer's master strip. */
    te::LevelMeterPlugin* getMasterMeter() const;

    /** The colours new channels cycle through, also offered in the UI's colour menu. */
    static juce::Array<juce::Colour> getPalette();

    /** The whole project as plain data, for the UI, tests and AI tools. */
    juce::var toVar() const;

private:
    std::unique_ptr<te::Edit> createNewEdit();
    void adoptEdit (std::unique_ptr<te::Edit>, const juce::File&);
    juce::String getComparableState() const;

    te::Engine& engine;
    std::unique_ptr<te::Edit> edit;
    te::EditItemID armedChannel;
    juce::File file;
    juce::String savedState;

    JUCE_DECLARE_NON_COPYABLE (Session)
};

juce::String toString (Session::Role);

/** Reads "#3c8ce0" or "3c8ce0" (alpha optional); nullopt if it isn't a hex colour. */
std::optional<juce::Colour> parseColour (const juce::String&);

} // namespace ng
