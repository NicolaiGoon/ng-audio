#pragma once

#include "core/CommandDispatcher.h"

namespace ng
{

/**
    Edits one audio clip, like FL Studio's sample settings with Cubase's sample editor tools:
    the clip's whole file as a waveform, with the part the clip plays lit up and the song's
    beats laid over it, and a row of controls above.

    - Stretch mode: Stretch (Rubber Band's R3 engine, for most sounds), Beats (keeps drum hits
      crisp) or Resample (speed and pitch change together, like a record sped up).
    - Pitch in semitones and Fine in cents; Gain, or Normalize to bring the loudest peak to 0 dB.
    - Follow tempo, with the file's own tempo (type it, or Detect it), so the clip keeps its
      length in beats when the song's tempo changes; Song tempo sets the song to the file's tempo.
    - Length in beats: type a length, or halve or double it, to stretch the clip to fit.
    - Reverse, and Sampler, which makes a sampler channel playing the file across the keys.

    Drag the edges of the lit part to trim the clip. Every change runs a command, so it's one
    undo step, and the AI can do all of it too (set_audio_clip, analyze_audio).
*/
class SampleEditor  : public juce::Component
{
public:
    SampleEditor (Session&, CommandDispatcher&);
    ~SampleEditor() override;

    void setClip (const juce::String& clipId);
    juce::String getClip() const                { return clipId; }

    /** Re-reads the clip after a command. Returns false if the clip is gone (or isn't audio). */
    bool update();

    /** Lets go of the clip, e.g. before the project is replaced. */
    void clear();

    /** Moves the playhead marker; call while playing. */
    void updatePlayhead();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    static constexpr int controlsHeight = 64;

private:
    te::WaveAudioClip* getWaveClip() const;
    void apply (const juce::String& property, const juce::var& value);
    void detectTempo();
    void makeSamplerChannel();

    /** Where a point in the file (in seconds, as the clip plays it) falls in the song, in beats. */
    double fileSecondsToBeat (double seconds) const;
    double beatToFileSeconds (double beat) const;
    juce::Rectangle<int> getWaveArea() const;
    float secondsToX (double seconds) const;
    double xToSeconds (float x) const;

    enum class Edge { none, start, end };
    Edge edgeAt (juce::Point<float>) const;

    Session& session;
    CommandDispatcher& dispatcher;
    juce::String clipId;

    // What's shown, read from the clip by update().
    juce::String fileName, filePath;
    double fileSeconds = 0.0, sampleRate = 0.0;
    int numChannels = 0;
    double startBeat = 0.0, lengthBeats = 0.0, sourceStartBeat = 0.0, sourceBpm = 0.0, speed = 1.0;
    bool followTempo = false, reversed = false, missing = false;
    juce::Colour colour { 0xff4a90e2 };

    juce::ComboBox modeBox;
    juce::Slider pitchKnob, fineKnob, gainKnob;
    juce::Label pitchLabel { {}, "Pitch" }, fineLabel { {}, "Fine" }, gainLabel { {}, "Gain" },
                modeLabel { {}, "Stretch mode" }, bpmLabel { {}, "File tempo" }, lengthLabel { {}, "Length (beats)" },
                bpmValue, lengthValue, info;
    juce::ToggleButton followButton { "Follow tempo" };
    juce::TextButton detectButton { "Detect" }, songTempoButton { "Song tempo" }, halveButton { juce::String::fromUTF8 ("\xc3\xb7" "2") }, doubleButton { juce::String::fromUTF8 ("\xc3\x97" "2") },
                     reverseButton { "Reverse" }, normalizeButton { "Normalize" }, samplerButton { "Sampler" };

    std::unique_ptr<te::SmartThumbnail> thumbnail;

    Edge dragEdge = Edge::none;
    double dragBeat = 0.0;
    float playheadX = -1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SampleEditor)
};

} // namespace ng
