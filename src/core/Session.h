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
class Session
{
public:
    explicit Session (te::Engine&);
    ~Session();

    te::Engine& getEngine() noexcept                { return engine; }
    te::Edit& getEdit() noexcept                    { return *edit; }
    juce::UndoManager& getUndoManager() noexcept    { return edit->getUndoManager(); }

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

    /** Finds a channel or a mixer strip by id or name (channels are searched first). */
    te::AudioTrack* findAnyTrack (const juce::String& idOrName) const;

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

    /** The level meter on the master output, for the mixer's master strip. */
    te::LevelMeterPlugin* getMasterMeter() const;

    /** The colours new channels cycle through, also offered in the UI's colour menu. */
    static juce::Array<juce::Colour> getPalette();

    /** The whole project as plain data, for the UI, tests and AI tools. */
    juce::var toVar() const;

private:
    te::Engine& engine;
    std::unique_ptr<te::Edit> edit;

    JUCE_DECLARE_NON_COPYABLE (Session)
};

juce::String toString (Session::Role);

/** Reads "#3c8ce0" or "3c8ce0" (alpha optional); nullopt if it isn't a hex colour. */
std::optional<juce::Colour> parseColour (const juce::String&);

} // namespace ng
