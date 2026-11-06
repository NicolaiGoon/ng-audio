#pragma once

#include "Session.h"

namespace ng
{

/**
    Starter sounds for the built-in 4osc synth, so a channel can sound like a bass, a lead or a
    pad straight away instead of the synth's plain default. A sound is a set of the synth's
    settings; create_track and set_instrument take one by name with "sound".
*/
namespace synthsounds
{
    /** The sounds' names, e.g. "Round Bass", in the order list_sounds gives them. */
    juce::StringArray getNames();

    /** {name, description} for each sound. */
    juce::var describe();

    /** The name as the table spells it (matching ignores case), or "" if there's no such sound. */
    juce::String find (const juce::String& name);

    /** A 4osc plugin's state with the sound's settings, ready for PluginCache::createNewPlugin. */
    juce::ValueTree createState (const juce::String& name);
}

} // namespace ng
