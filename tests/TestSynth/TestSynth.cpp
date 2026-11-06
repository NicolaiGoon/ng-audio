#include <juce_audio_processors/juce_audio_processors.h>

/**
    The smallest useful instrument plugin: one sine voice with a Volume parameter, which it
    saves with its state. The tests build it as a VST3 so plugin scanning, loading and
    saving can be checked against a real plugin.
*/
class TestSynth  : public juce::AudioProcessor
{
public:
    TestSynth()
        : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
    {
        addParameter (volume = new juce::AudioParameterFloat (juce::ParameterID { "volume", 1 }, "Volume", 0.0f, 1.0f, 1.0f));
    }

    const juce::String getName() const override     { return "Ng Test Synth"; }

    void prepareToPlay (double newSampleRate, int) override
    {
        sampleRate = newSampleRate;
    }

    void releaseResources() override {}

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        buffer.clear();

        for (const auto metadata : midi)
        {
            auto message = metadata.getMessage();

            if (message.isNoteOn())
            {
                frequency = juce::MidiMessage::getMidiNoteInHertz (message.getNoteNumber());
                level = message.getFloatVelocity() * 0.25f;
            }
            else if (message.isNoteOff() || message.isAllNotesOff())
            {
                level = 0.0f;
            }
        }

        if (level <= 0.0f)
            return;

        auto delta = juce::MathConstants<double>::twoPi * frequency / sampleRate;

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            auto sample = level * volume->get() * (float) std::sin (phase);
            phase = std::fmod (phase + delta, juce::MathConstants<double>::twoPi);

            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                buffer.setSample (channel, i, sample);
        }
    }

    bool acceptsMidi() const override               { return true; }
    bool producesMidi() const override              { return false; }
    double getTailLengthSeconds() const override    { return 0.0; }

    juce::AudioProcessorEditor* createEditor() override     { return new juce::GenericAudioProcessorEditor (*this); }
    bool hasEditor() const override                         { return true; }

    int getNumPrograms() override                           { return 1; }
    int getCurrentProgram() override                        { return 0; }
    void setCurrentProgram (int) override                   {}
    const juce::String getProgramName (int) override        { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& data) override
    {
        juce::MemoryOutputStream (data, true).writeFloat (volume->get());
    }

    void setStateInformation (const void* data, int size) override
    {
        if (size >= (int) sizeof (float))
            *volume = juce::MemoryInputStream (data, (size_t) size, false).readFloat();
    }

private:
    double sampleRate = 44100.0, frequency = 440.0, phase = 0.0;
    float level = 0.0f;
    juce::AudioParameterFloat* volume = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TestSynth)
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TestSynth();
}
