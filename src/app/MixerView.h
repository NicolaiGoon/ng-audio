#pragma once

#include "core/CommandDispatcher.h"

namespace ng
{

/**
    The mixer: one column per strip (name, pan, insert effects, sends, mute, solo, fader,
    level meter) and the master strip on the right. Moving a control runs set_mixer_strip; a fader drag is one
    undo step.

    The faders and pan knobs follow automation as the song plays.

    update() takes the project state after every command. When only fader, pan, mute or
    solo values or send levels changed it updates the existing columns in place, so a fader being
    dragged isn't rebuilt under the mouse.
*/
class MixerView  : public juce::Component,
                   private juce::Timer
{
public:
    MixerView (Session&, CommandDispatcher&);
    ~MixerView() override;

    void update (const juce::var& state);

    /** Drops every column (and its meter), e.g. before the project is replaced. */
    void clear();

    void resized() override;

private:
    void timerCallback() override;

    class Meter;
    class EffectSlots;
    class SendSlots;
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
