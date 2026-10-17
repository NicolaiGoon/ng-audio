#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <atomic>
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

/** Ng's engine behaviour: Tracktion's own, except that while stems are exported, muted
    strips still feed their sends, so a bus's stem can hold what is sent to it without the
    senders' own sound (see export_audio). Pass one when creating the te::Engine. */
class EngineBehaviour  : public te::EngineBehaviour
{
public:
    bool shouldProcessMutedTracks() override    { return processMutedTracks; }

    static inline std::atomic<bool> processMutedTracks { false };
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

    /** Writes the project to a file without making it the project's file or counting as a save,
        e.g. for autosave. Returns an error, or an empty string on success. */
    juce::String writeCopy (const juce::File&);

    /** Replaces the open project with a copy written by writeCopy (an autosave), as the project in
        originalFile (or a new, unsaved one if that's empty). Audio files it refers to by relative
        path are found from the original's folder. It opens with unsaved changes. */
    juce::String recoverProject (const juce::File& copy, const juce::File& originalFile);

    /** The project's file, empty until it is first saved or when it was opened from nowhere. */
    juce::File getFile() const                      { return file; }
    juce::String getProjectName() const;

    /** True when the project differs from what was last saved, opened or created. */
    bool hasUnsavedChanges() const;

    /** The project as text, leaving out what isn't a change the user made (like the playhead),
        to tell whether it has changed. */
    juce::String getComparableState() const;

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

    /** An audio channel plays audio clips (imported or, later, recorded files) instead of an
        instrument. It has no instrument, steps or MIDI clips, and its clips stay on audio
        channels. Set when the channel is created (create_track with instrument "audio"). */
    static bool isAudioChannel (const te::AudioTrack& channel);
    void setAudioChannel (te::AudioTrack& channel);

    /** An audio clip as get_state lists it, added to the clip's object: its file, missing: true
        if the file isn't there, max_end_beat (the furthest its end can go before the file runs
        out), gain_db, fade_in_beats, fade_out_beats, and follow_tempo with the file's source_bpm. */
    void describeAudioClip (te::WaveAudioClip&, juce::DynamicObject&) const;

    /** How an audio clip changes its speed and pitch, as set_audio_clip sets it:
        "stretch" (Rubber Band's R3 engine: speed and pitch change separately, for most sounds),
        "beats" (Rubber Band's percussive mode: keeps drum hits crisp) or "resample" (speed and
        pitch change together, like a record played faster or slower). */
    static juce::String getStretchMode (const te::WaveAudioClip&);

    /** Makes an audio clip stretch and pitch-shift its file as it plays (on a read-ahead thread)
        instead of Tracktion's default of rendering a stretched copy first. A rendered copy
        leaves the clip silent from each change until the render is done, then restarts
        playback; stretching as it plays changes the sound straight away, with no gap. Not
        part of the undo history: it's how the clip plays, not a setting. */
    static void useRealTimeStretching (te::WaveAudioClip&);

    /** An audio clip's pitch change in semitones (fractions are cents). In resample mode it
        comes from how fast the clip plays. */
    static double getPitchChange (const te::WaveAudioClip&);

    /** How fast an audio clip plays its file: 1 as recorded, 2 twice as fast. */
    double getPlaybackSpeed (te::WaveAudioClip&) const;

    /** Where an audio clip's file starts and ends in the song, in beats, wherever the clip has
        been trimmed to. A clip that follows the tempo stretches its file over a fixed number of
        beats; otherwise the file lasts as long as it does at the project tempo. */
    double getSourceStartBeat (te::WaveAudioClip&) const;
    double getSourceEndBeat (te::WaveAudioClip&) const;

    /** A channel's instrument (the built-in 4OSC synth or a plugin), or nullptr. With multiple
        outputs on, the plugin lives in a rack (see getMultiOutputRack), and this finds it there. */
    static te::Plugin* getInstrument (const te::AudioTrack& channel);

    //==============================================================================
    // Multi-output instruments. The plugin moves into a Tracktion rack whose outputs are the
    // plugin's. The channel keeps a rack instance that feeds it MIDI and plays nothing itself;
    // each stereo output gets a strip of its own with a rack instance playing that output, and
    // those strips all go to the channel's own strip, which sums them.

    /** The rack instance on a channel playing its instrument with multiple outputs, or nullptr. */
    static te::RackInstance* getMultiOutputRack (const te::AudioTrack& channel);

    /** The strips a multi-output channel's outputs play on, first output first. */
    juce::Array<te::AudioTrack*> getOutputStrips (const te::AudioTrack& channel) const;

    /** For an output strip, the channel whose output it plays and which stereo pair (0 is
        outputs 1-2); nullptr and -1 for other strips. */
    te::AudioTrack* getOutputOf (const te::AudioTrack& strip) const;
    static int getOutputPair (const te::AudioTrack& strip);
    static void setOutputOf (te::AudioTrack& strip, const te::AudioTrack& channel, int pair);

    /** How many audio outputs an instrument has (2 for a stereo one). */
    static int getNumOutputs (te::Plugin& instrument);

    //==============================================================================
    // The sampler: Tracktion's built-in sampler as a channel's instrument. Each sample
    // plays on a range of keys, at its own pitch on its root key; a drum kit puts one
    // sample on each key and lets each play to its end (one-shot).

    /** A channel's sampler, or nullptr if its instrument is something else. */
    static te::SamplerPlugin* getSampler (const te::AudioTrack& channel);

    /** The sampler's samples as get_state lists them: {index, name, file, note, min_note,
        max_note, gain_db, pan, one_shot}, plus missing: true for a file that isn't there. */
    static juce::var describeSamples (te::SamplerPlugin&);

    /** The audio file types the sampler reads, as a wildcard like "*.wav;*.aif". */
    juce::String getAudioFileWildcard() const;

    /** How get_state and set_instrument name an instrument: "4osc", "sampler", "none", or a plugin's
        id as list_plugins gives it. get_state names an audio channel's "audio". */
    static juce::String getInstrumentId (const te::Plugin*);

    /** The effects Ng DAW has built in, as add_effect names them (e.g. "eq"), with their
        display names and Tracktion plugin types. */
    struct BuiltInEffect { juce::String id, name, pluginType; };
    static const juce::Array<BuiltInEffect>& getBuiltInEffects();

    /** How get_state and add_effect name a plugin: a built-in effect's name ("reverb"),
        "4osc", or a scanned plugin's id as list_plugins gives it. */
    static juce::String getPluginId (const te::Plugin&);

    /** The insert effects on a mixer strip (or "master"), in signal order, and the list
        they live in. nullptr if there's no such strip. */
    te::PluginList* findEffectList (const juce::String& stripIdOrName) const;
    static juce::Array<te::Plugin*> getEffects (const te::PluginList&);

    /** Where an effect at a position among the effects goes in the plugin list: effects
        come before the fader, and on the master before its meter. */
    static int getEffectInsertIndex (const te::PluginList&, int effectIndex);

    /** An effect as get_state lists it: {id, type, name, enabled}, plus missing: true for a
        plugin that isn't installed (or failed to load) on this computer. */
    static juce::var describeEffect (te::Plugin&);

    /** Writes all of a built-in effect's settings into the project, defaults included, so
        undoing a change always has a value to go back to. Called when an effect is added and
        when a project opens. */
    static void storeEffectSettings (te::Plugin&);

    /** The automated parameters on a strip's (or the master's) plugin list, as get_state lists
        them: {parameter, effect (for an effect's), name, points (how many)}. The master's fader
        isn't in its plugin list, so it's passed separately. */
    static juce::var describeAutomatedParameters (const te::PluginList&, te::Plugin* masterFader = nullptr);

    /** Calls fn for every effect on every strip and the master. */
    void forEachEffect (const std::function<void (te::Plugin&)>&) const;

    //==============================================================================
    /** A bus is a mixer strip that sends can feed: it holds an aux return for one of
        Tracktion's numbered aux buses. Returns the bus number, or -1 for other strips. */
    static int getBusNumber (const te::AudioTrack& strip);

    /** The strip that returns an aux bus, or nullptr. */
    te::AudioTrack* findBus (int busNumber) const;

    /** A strip's sends, in the order they appear in its plugin list. */
    static juce::Array<te::AuxSendPlugin*> getSends (const te::AudioTrack& strip);

    /** True if a send is before the strip's fader, so the fader doesn't change it. */
    static bool isPreFader (const te::AuxSendPlugin&);

    /** True if audio from 'from' can reach 'to' through strip outputs and sends,
        e.g. to refuse a send that would feed a bus back into itself. */
    bool feeds (const te::AudioTrack& from, const te::AudioTrack& to) const;

    //==============================================================================
    // Tags group channels by what they are ("drums", "bass", "vox"). Each tag has a bus of its
    // own (a strip named like "Drums Bus") that its channels' strips play into; tag_channel
    // creates and routes it.

    /** A channel's tag, lower case, or "" if it has none. */
    static juce::String getTag (const te::AudioTrack& channel);
    void setTag (te::AudioTrack& channel, const juce::String& tag);

    /** For a tag's bus, its tag; "" for other strips. */
    static juce::String getTagOfBus (const te::AudioTrack& strip);
    void setTagOfBus (te::AudioTrack& strip, const juce::String& tag);

    /** The bus a tag's channels go to, or nullptr if there isn't one yet. */
    te::AudioTrack* findTagBus (const juce::String& tag) const;

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

    //==============================================================================
    // Recording audio. Each audio channel can take an audio input (saved with the project)
    // and a monitoring mode: "auto" plays the input through the channel while it's armed,
    // "on" always, "off" never. Any number of audio channels can be armed at once; like MIDI
    // arming, that's setup, not part of the song.

    /** The names of the audio inputs the audio device offers (e.g. "Input 1 + 2"). */
    juce::StringArray getAudioInputNames() const;

    /** An audio channel's input ("" for none) and monitoring mode ("auto", "on" or "off"). */
    static juce::String getAudioInput (const te::AudioTrack& channel);
    static juce::String getMonitorMode (const te::AudioTrack& channel);
    void setAudioInput (te::AudioTrack& channel, const juce::String& input);
    void setMonitorMode (te::AudioTrack& channel, const juce::String& mode);

    bool isAudioArmed (const te::AudioTrack& channel) const;
    void setAudioArmed (te::AudioTrack& channel, bool armed);
    juce::Array<te::AudioTrack*> getArmedAudioChannels() const;

    /** Points each audio input at the audio channels that use it, records on the armed ones and
        sets how they're monitored. Called when arming, when an input or monitoring mode
        changes, after undo and redo, and before recording. */
    void routeAudioInputs();

    /** Where recordings are written: a Recordings folder beside the project file, or, before
        the project is saved, in the default folder's Recordings. */
    juce::File getRecordingFolder() const;

    /** Where projects go by default (a preference): Music/Ng DAW unless set. New projects are
        saved there, and recordings and exports go there until the project is saved. */
    juce::File getDefaultFolder() const;
    void setDefaultFolder (const juce::File&);

    /** Where the playhead is, as bar.beat.sixteenth counting from 1 (e.g. "3.2.1"). */
    juce::String getPositionText() const;

    /** The level meter on the master output, for the mixer's master strip. */
    te::LevelMeterPlugin* getMasterMeter() const;

    /** The colours new channels cycle through, also offered in the UI's colour menu. */
    static juce::Array<juce::Colour> getPalette();

    /** The whole project as plain data, for the UI, tests and AI tools. */
    juce::var toVar() const;

    /** True while export_audio renders. It keeps handling messages meanwhile, and the
        dispatcher refuses other commands until it's done. */
    bool isExporting() const noexcept               { return exporting; }
    void setExporting (bool isNowExporting)         { exporting = isNowExporting; }

private:
    std::unique_ptr<te::Edit> createNewEdit();
    void adoptEdit (std::unique_ptr<te::Edit>, const juce::File&);

    te::Engine& engine;
    std::unique_ptr<te::Edit> edit;
    te::EditItemID armedChannel;
    juce::Array<te::EditItemID> armedAudio;
    juce::File file, defaultFolder;
    juce::String savedState;
    bool exporting = false;

    JUCE_DECLARE_NON_COPYABLE (Session)
};

juce::String toString (Session::Role);

/** Reads "#3c8ce0" or "3c8ce0" (alpha optional); nullopt if it isn't a hex colour. */
std::optional<juce::Colour> parseColour (const juce::String&);

} // namespace ng
