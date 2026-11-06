#include <juce_audio_processors/juce_audio_processors.h>

/**
    The smallest useful effect plugin: a Gain parameter that scales its input, saved with
    its state. The tests build it as a VST3 so effects on mixer strips can be checked
    against a real plugin.
*/
class TestGain  : public juce::AudioProcessor
{
public:
    TestGain()
        : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                           .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
    {
        addParameter (gain = new juce::AudioParameterFloat (juce::ParameterID { "gain", 1 }, "Gain", 0.0f, 1.0f, 1.0f));
    }

    const juce::String getName() const override     { return "Ng Test Gain"; }

    void prepareToPlay (double, int) override {}
    void releaseResources() override {}

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        buffer.applyGain (gain->get());
    }

    bool acceptsMidi() const override               { return false; }
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
        juce::MemoryOutputStream (data, true).writeFloat (gain->get());
    }

    void setStateInformation (const void* data, int size) override
    {
        if (size >= (int) sizeof (float))
            *gain = juce::MemoryInputStream (data, (size_t) size, false).readFloat();
    }

private:
    juce::AudioParameterFloat* gain = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TestGain)
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TestGain();
}
