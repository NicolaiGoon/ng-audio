#include "AudioAnalysis.h"

namespace ng
{
namespace
{
    constexpr double silenceDb = -100.0;

    double toDb (double power)
    {
        return power > 0.0 ? std::max (silenceDb, 10.0 * std::log10 (power)) : silenceDb;
    }

    double round2 (double v)
    {
        return std::round (v * 100.0) / 100.0;
    }

    /** A second-order filter section (direct form I). */
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;

        double process (double x)
        {
            auto y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = x;
            y2 = y1; y1 = y;
            return y;
        }
    };

    /** BS.1770's K-weighting (a high shelf, then a high-pass) for a sample rate, with the
        coefficients derived from the standard's analogue prototypes as libebur128 does. */
    std::pair<Biquad, Biquad> kWeighting (double sampleRate)
    {
        Biquad shelf, highPass;

        {
            const double f0 = 1681.974450955533, gainDb = 3.999843853973347, q = 0.7071752369554196;
            auto k = std::tan (juce::MathConstants<double>::pi * f0 / sampleRate);
            auto vh = std::pow (10.0, gainDb / 20.0);
            auto vb = std::pow (vh, 0.4996667741545416);
            auto a0 = 1.0 + k / q + k * k;

            shelf.b0 = (vh + vb * k / q + k * k) / a0;
            shelf.b1 = 2.0 * (k * k - vh) / a0;
            shelf.b2 = (vh - vb * k / q + k * k) / a0;
            shelf.a1 = 2.0 * (k * k - 1.0) / a0;
            shelf.a2 = (1.0 - k / q + k * k) / a0;
        }

        {
            const double f0 = 38.13547087602444, q = 0.5003270373238773;
            auto k = std::tan (juce::MathConstants<double>::pi * f0 / sampleRate);
            auto a0 = 1.0 + k / q + k * k;

            highPass.b0 = 1.0;
            highPass.b1 = -2.0;
            highPass.b2 = 1.0;
            highPass.a1 = 2.0 * (k * k - 1.0) / a0;
            highPass.a2 = (1.0 - k / q + k * k) / a0;
        }

        return { shelf, highPass };
    }
}

void AudioLevels::addTo (juce::DynamicObject& o) const
{
    o.setProperty ("peak_db", peak > 0.0f ? round2 (juce::Decibels::gainToDecibels (peak)) : silenceDb);
    o.setProperty ("rms_db", round2 (rmsDb));
    o.setProperty ("loudness_lufs", round2 (loudnessLufs));
    o.setProperty ("clipped_samples", clippedSamples);
}

AudioLevels measureAudioFile (te::Engine& engine, const juce::File& file)
{
    AudioLevels levels;
    std::unique_ptr<juce::AudioFormatReader> reader (engine.getAudioFileFormatManager().readFormatManager.createReaderFor (file));

    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0)
        return levels;

    // BS.1770 weights the surround channels differently; for mono and stereo every channel counts once.
    auto channels = static_cast<int> (std::min (2u, reader->numChannels));
    auto rate = reader->sampleRate;
    auto total = reader->lengthInSamples;

    std::vector<std::pair<Biquad, Biquad>> filters (static_cast<size_t> (channels), kWeighting (rate));

    // Loudness is measured in 400 ms blocks that overlap by 75%, so it's summed in 100 ms steps.
    auto stepLength = std::max<juce::int64> (1, juce::roundToInt (rate * 0.1));
    std::vector<double> stepPower;           // K-weighted power of each 100 ms step, summed over channels
    double stepSum = 0.0, squareSum = 0.0;
    juce::int64 inStep = 0;

    constexpr int blockSize = 8192;
    juce::AudioBuffer<float> buffer (channels, blockSize);

    for (juce::int64 pos = 0; pos < total; pos += blockSize)
    {
        auto n = static_cast<int> (std::min<juce::int64> (blockSize, total - pos));
        reader->read (&buffer, 0, n, pos, true, channels > 1);

        for (int i = 0; i < n; ++i)
        {
            for (int ch = 0; ch < channels; ++ch)
            {
                auto x = buffer.getSample (ch, i);
                auto magnitude = std::abs (x);
                levels.peak = std::max (levels.peak, magnitude);

                if (magnitude >= 0.99999f)
                    ++levels.clippedSamples;

                squareSum += static_cast<double> (x) * x;

                auto& [shelf, highPass] = filters[static_cast<size_t> (ch)];
                auto weighted = highPass.process (shelf.process (x));
                stepSum += weighted * weighted;
            }

            if (++inStep == stepLength)
            {
                stepPower.push_back (stepSum / static_cast<double> (stepLength));
                stepSum = 0.0;
                inStep = 0;
            }
        }
    }

    levels.valid = true;
    levels.rmsDb = toDb (squareSum / (static_cast<double> (total) * channels));

    // Blocks of four steps (400 ms); a file shorter than one block is measured as one.
    std::vector<double> blockPower;

    for (size_t i = 0; i + 4 <= stepPower.size(); ++i)
        blockPower.push_back ((stepPower[i] + stepPower[i + 1] + stepPower[i + 2] + stepPower[i + 3]) / 4.0);

    if (blockPower.empty() && ! stepPower.empty())
        blockPower.push_back (std::accumulate (stepPower.begin(), stepPower.end(), 0.0) / static_cast<double> (stepPower.size()));

    auto loudnessOf = [] (double power) { return power > 0.0 ? -0.691 + 10.0 * std::log10 (power) : silenceDb; };

    // Gating: drop blocks below -70 LUFS, then those more than 10 LU below the mean of the rest.
    auto gatedMean = [&] (double threshold)
    {
        double sum = 0.0;
        int count = 0;

        for (auto p : blockPower)
            if (loudnessOf (p) > threshold)
            {
                sum += p;
                ++count;
            }

        return count > 0 ? sum / count : 0.0;
    };

    auto absoluteGated = gatedMean (-70.0);

    if (absoluteGated > 0.0)
        levels.loudnessLufs = std::max (silenceDb, loudnessOf (gatedMean (loudnessOf (absoluteGated) - 10.0)));

    return levels;
}

} // namespace ng
