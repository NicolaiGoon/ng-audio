#pragma once

#include "core/CommandDispatcher.h"

namespace ng
{

/**
    The mixer: one column per strip (name, pan, insert effects, sends, mute, solo, fader,
    level meter) and the master strip on the right. Moving a control runs set_mixer_strip; a fader drag is one
    undo step.

    The faders and pan knobs follow automation as the song plays.

    Drag a strip by its name onto another to move it there. A bus or group strip has a fold
    button that hides the strips playing into it, and setBusesDocked() gathers the buses in
    their own section beside the master.

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
    void paint (juce::Graphics&) override;

    /** Puts the buses (and groups other strips play into) in their own section beside the
        master, rather than among the other strips. */
    void setBusesDocked (bool);
    bool areBusesDocked() const noexcept    { return busesDocked; }

private:
    void timerCallback() override;

    class Meter;
    class EffectSlots;
    class SendSlots;
    class StripColumn;

    Session& session;
    CommandDispatcher& dispatcher;

    void moveStrip (const juce::String& draggedId, const juce::String& targetId);

    juce::Viewport viewport, busViewport;
    juce::Component columnHolder, busHolder;
    juce::OwnedArray<StripColumn> columns;
    juce::Array<bool> dockedColumns;    // for each column, whether it's in the docked section
    std::unique_ptr<StripColumn> masterColumn;
    juce::String structureKey;
    juce::var lastState;
    bool busesDocked = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerView)
};

} // namespace ng
