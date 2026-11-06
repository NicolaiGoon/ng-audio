#pragma once

#include "Session.h"

namespace ng
{

/** How loud an audio file is: what analyze_audio and analyze_mix report. */
struct AudioLevels
{
    bool valid = false;
    float peak = 0.0f;              // sample peak, as gain
    double rmsDb = -100.0;          // RMS over the whole file (channels averaged), in dBFS
    double loudnessLufs = -100.0;   // integrated loudness (ITU-R BS.1770, gated), in LUFS
    juce::int64 clippedSamples = 0; // samples at or above full scale

    /** {peak_db, rms_db, loudness_lufs, clipped_samples}, rounded, added to an object. */
    void addTo (juce::DynamicObject&) const;
};

/** Reads a whole file and measures it. valid is false if it can't be read. */
AudioLevels measureAudioFile (te::Engine&, const juce::File&);

} // namespace ng
