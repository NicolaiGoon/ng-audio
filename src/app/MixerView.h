#pragma once

#include "core/CommandDispatcher.h"

namespace ng
{

/**
    The mixer: one column per strip (name, pan, insert effects, mute, solo, fader, level
    meter) and the master strip on the right. Moving a control runs set_mixer_strip; a fader drag is one
    undo step.

    update() takes the project state after every command. When only fader, pan, mute or
    solo values changed (not the effects) it updates the existing columns in place, so a fader being
    dragged isn't rebuilt under the mouse.
*/
class MixerView  : public juce::Component
{
public:
    MixerView (Session&, CommandDispatcher&);
    ~MixerView() override;

    void update (const juce::var& state);

    /** Drops every column (and its meter), e.g. before the project is replaced. */
    void clear();

    void resized() override;

private:
    class Meter;
    class EffectSlots;
    class StripColumn;

    Session& session;
    CommandDispatcher& dispatcher;

    juce::Viewport viewport;
    juce::Component columnHolder;
    juce::OwnedArray<StripColumn> columns;
    std::unique_ptr<StripColumn> masterColumn;
    juce::String structureKey;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerView)
};

} // namespace ng
