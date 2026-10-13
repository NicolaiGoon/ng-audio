#include <juce_audio_processors/juce_audio_processors.h>

/**
    A tiny multi-output instrument: two stereo outputs, with C1 (MIDI note 36) playing a sine
    on the first and D1 (38) on the second. The tests build it as a VST3 to check that each
    output of a multi-output instrument reaches its own mixer strip.
*/
class TestDrums  : public juce::AudioProcessor
{
public:
    TestDrums()
        : AudioProcessor (BusesProperties().withOutput ("Kick", juce::AudioChannelSet::stereo(), true)
                                           .withOutput ("Snare", juce::AudioChannelSet::stereo(), true))
    {
    }

    const juce::String getName() const override     { return "Ng Test Drums"; }

    bool isBusesLayoutSupported (const BusesLayout& layout) const override
    {
        for (auto& bus : layout.outputBuses)
            if (! bus.isDisabled() && bus != juce::AudioChannelSet::stereo())
                return false;

        return layout.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
    }

    void prepareToPlay (double newSampleRate, int) override     { sampleRate = newSampleRate; }
    void releaseResources() override {}

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        buffer.clear();

        for (const auto metadata : midi)
        {
            auto message = metadata.getMessage();
            auto voice = message.getNoteNumber() == 36 ? 0 : message.getNoteNumber() == 38 ? 1 : -1;

            if (voice < 0)
                continue;

            if (message.isNoteOn())
                levels[voice] = 0.25f;
            else if (message.isNoteOff())
                levels[voice] = 0.0f;
        }

        for (int voice = 0; voice < 2; ++voice)
        {
            if (levels[voice] <= 0.0f || getBusCount (false) <= voice || ! getBus (false, voice)->isEnabled())
                continue;

            auto out = getBusBuffer (buffer, false, voice);
            auto delta = juce::MathConstants<double>::twoPi * (voice == 0 ? 110.0 : 220.0) / sampleRate;

            for (int i = 0; i < out.getNumSamples(); ++i)
            {
                auto sample = levels[voice] * (float) std::sin (phases[voice]);
                phases[voice] = std::fmod (phases[voice] + delta, juce::MathConstants<double>::twoPi);

                for (int channel = 0; channel < out.getNumChannels(); ++channel)
                    out.setSample (channel, i, sample);
            }
        }
    }

    bool acceptsMidi() const override               { return true; }
    bool producesMidi() const override              { return false; }
    double getTailLengthSeconds() const override    { return 0.0; }

    juce::AudioProcessorEditor* createEditor() override     { return nullptr; }
    bool hasEditor() const override                         { return false; }

    int getNumPrograms() override                           { return 1; }
    int getCurrentProgram() override                        { return 0; }
    void setCurrentProgram (int) override                   {}
    const juce::String getProgramName (int) override        { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

private:
    double sampleRate = 44100.0, phases[2] = {};
    float levels[2] = {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TestDrums)
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TestDrums();
}
