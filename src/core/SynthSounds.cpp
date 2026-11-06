#include "SynthSounds.h"

namespace ng::synthsounds
{
namespace
{
    // The 4osc's wave shapes (te::FourOscPlugin::WaveShape) and filter types, by number.
    constexpr int sine = 1, triangle = 2, sawUp = 3, sawDown = 4, square = 5;
    constexpr int lowPass = 1;

    struct Sound
    {
        const char* name;
        const char* description;
        std::initializer_list<std::pair<const char*, double>> settings;
    };

    // Settings are the synth's own property names: oscillators are numbered 1 to 4 (waveShape1,
    // tune2 in semitones, level2 in dB, voices1, detune1 0-0.5, spread1 %), the filter's
    // frequency is a MIDI note (69 = 440 Hz) and its amount scales its envelope (-1 to 1),
    // envelope times are in seconds and sustains in %, effects have an On switch and a Mix.
    // Every sound turns masterLevel down, so a chord doesn't clip at a 0 dB fader.
    const Sound sounds[] = {
        { "Soft Bell Lead", "A gentle, bell-like lead for melodies: sine with a quiet octave triangle and a quick decay (dry: add a reverb effect on its strip for space)",
          { { "waveShape1", sine }, { "waveShape2", triangle }, { "tune2", 12 }, { "level2", -14 },
            { "ampAttack", 0.005 }, { "ampDecay", 0.7 }, { "ampSustain", 35 }, { "ampRelease", 0.5 }, { "ampVelocity", 40 },
            { "masterLevel", -12 } } },

        { "Square Lead", "A bright, steady chiptune-style lead: two square waves slightly detuned, with a short echo",
          { { "waveShape1", square }, { "waveShape2", square }, { "fineTune2", 8 }, { "level2", -6 },
            { "ampAttack", 0.01 }, { "ampDecay", 0.2 }, { "ampSustain", 80 }, { "ampRelease", 0.15 },
            { "delayOn", 1 }, { "delayMix", 0.15 }, { "masterLevel", -4 } } },

        { "Round Bass", "A warm, rounded bass: a sine with a low-filtered saw on top, and a little bite on each note",
          { { "waveShape1", sine }, { "waveShape2", sawUp }, { "tune2", 0 }, { "level2", -8 },
            { "ampAttack", 0.003 }, { "ampDecay", 0.3 }, { "ampSustain", 75 }, { "ampRelease", 0.08 },
            { "filterType", lowPass }, { "filterFreq", 62 }, { "filterResonance", 15 },
            { "filterAmount", 0.2 }, { "filterAttack", 0.0 }, { "filterDecay", 0.2 }, { "filterSustain", 0 },
            { "filterKey", 50 }, { "masterLevel", -6 } } },

        { "Sub Bass", "A deep, clean sine bass for the low end",
          { { "waveShape1", sine },
            { "ampAttack", 0.005 }, { "ampDecay", 0.1 }, { "ampSustain", 100 }, { "ampRelease", 0.06 },
            { "masterLevel", -6 } } },

        { "Pluck", "A short, plucked sound for arpeggios and chords: a saw whose filter snaps shut",
          { { "waveShape1", sawUp }, { "voices1", 2 }, { "detune1", 0.08 }, { "spread1", 40 },
            { "ampAttack", 0.001 }, { "ampDecay", 0.35 }, { "ampSustain", 0 }, { "ampRelease", 0.2 }, { "ampVelocity", 50 },
            { "filterType", lowPass }, { "filterFreq", 58 }, { "filterResonance", 20 },
            { "filterAmount", 0.45 }, { "filterAttack", 0.0 }, { "filterDecay", 0.18 }, { "filterSustain", 0 },
            { "filterKey", 60 }, { "reverbOn", 1 }, { "reverbMix", 0.15 }, { "masterLevel", -12 } } },

        { "Warm Pad", "A slow, wide pad for chords: detuned saws, chorus and reverb",
          { { "waveShape1", sawUp }, { "voices1", 4 }, { "detune1", 0.15 }, { "spread1", 70 },
            { "waveShape2", sawDown }, { "tune2", 12 }, { "level2", -12 },
            { "ampAttack", 0.8 }, { "ampDecay", 1.0 }, { "ampSustain", 80 }, { "ampRelease", 1.5 },
            { "filterType", lowPass }, { "filterFreq", 78 }, { "filterResonance", 10 },
            { "chorusOn", 1 }, { "chorusMix", 0.4 }, { "reverbOn", 1 }, { "reverbMix", 0.35 }, { "reverbSize", 0.8 },
            { "masterLevel", -16 } } },

        { "Electric Keys", "Soft electric-piano-like keys: sine with a faint octave triangle, decaying, with chorus",
          { { "waveShape1", sine }, { "waveShape2", triangle }, { "tune2", 12 }, { "level2", -20 },
            { "ampAttack", 0.002 }, { "ampDecay", 1.4 }, { "ampSustain", 25 }, { "ampRelease", 0.4 }, { "ampVelocity", 60 },
            { "chorusOn", 1 }, { "chorusMix", 0.3 }, { "masterLevel", -10 } } },

        { "Organ", "A simple drawbar-style organ: three sines stacked on the octave and the twelfth",
          { { "waveShape1", sine }, { "waveShape2", sine }, { "tune2", 12 }, { "level2", -6 },
            { "waveShape3", sine }, { "tune3", 19 }, { "level3", -12 },
            { "ampAttack", 0.01 }, { "ampDecay", 0.1 }, { "ampSustain", 100 }, { "ampRelease", 0.08 },
            { "chorusOn", 1 }, { "chorusMix", 0.25 }, { "masterLevel", -14 } } },
    };

    const Sound* lookUp (const juce::String& name)
    {
        for (auto& s : sounds)
            if (name.trim().equalsIgnoreCase (s.name))
                return &s;

        return nullptr;
    }
}

juce::StringArray getNames()
{
    juce::StringArray names;

    for (auto& s : sounds)
        names.add (s.name);

    return names;
}

juce::var describe()
{
    juce::Array<juce::var> list;

    for (auto& s : sounds)
    {
        auto o = new juce::DynamicObject();
        o->setProperty ("name", s.name);
        o->setProperty ("description", s.description);
        list.add (juce::var (o));
    }

    return list;
}

juce::String find (const juce::String& name)
{
    auto s = lookUp (name);
    return s != nullptr ? juce::String (s->name) : juce::String();
}

juce::ValueTree createState (const juce::String& name)
{
    juce::ValueTree state (te::IDs::PLUGIN);
    state.setProperty (te::IDs::type, te::FourOscPlugin::xmlTypeName, nullptr);

    if (auto s = lookUp (name))
    {
        for (auto& [key, value] : s->settings)
        {
            // Switches and choices are stored as whole numbers or booleans, the rest as numbers.
            juce::String id (key);

            if (id.endsWith ("On"))
                state.setProperty (id, value != 0.0, nullptr);
            else if (id.startsWith ("waveShape") || id.startsWith ("voices") || id == "filterType")
                state.setProperty (id, juce::roundToInt (value), nullptr);
            else
                state.setProperty (id, value, nullptr);
        }
    }

    return state;
}

} // namespace ng::synthsounds
