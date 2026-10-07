#pragma once

#include <tracktion_engine/tracktion_engine.h>

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

    /** The whole project as plain data, for the UI, tests and AI tools. */
    juce::var toVar() const;

private:
    te::Engine& engine;
    std::unique_ptr<te::Edit> edit;

    JUCE_DECLARE_NON_COPYABLE (Session)
};

juce::String toString (Session::Role);

} // namespace ng
