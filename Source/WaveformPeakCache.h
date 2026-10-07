#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <vector>

// Perceptual amplitude mapping for waveform *display* only -- never applied to the
// actual audio, purely a paint()-time curve. Real impulse responses routinely have a
// huge peak-to-average ratio (one sharp initial transient followed by a much quieter
// decaying tail, e.g. a peak near 0dBFS with a tail averaging -50dB or more) -- a pure
// linear-amplitude waveform display makes that tail's shape invisible (a few percent
// of a pixel tall), even though it's exactly the part a user needs to see to judge a
// decay/crop point. Square-root companding boosts quiet content disproportionately
// while leaving 0 at 0 and the peak (+-1) unchanged, which is the standard trick most
// waveform displays use for this -- simpler and better-behaved at zero than a true dB
// scale (which has no finite value for silence).
inline float waveformVisualScale (float linearValue) noexcept
{
    return linearValue < 0.0f ? -std::sqrt (-linearValue) : std::sqrt (linearValue);
}

// Synchronous, O(numSamples) min/max-per-column peak cache -- deliberately not
// juce::AudioThumbnail, which is built for async, incrementally-generated peaks from a
// long file/stream plus a disk-backed cache. IR buffers here are short, already fully
// resident in memory, and get rebuilt on every relevant edit (crop drag, phase/EQ
// change) -- a plain synchronous pass is simpler, has no cache-file lifecycle, and is
// always exactly in sync with whatever buffer was just rendered.
class WaveformPeakCache
{
public:
    void rebuild (const juce::AudioBuffer<float>& source, int numPeakColumns)
    {
        peaks.assign ((size_t) juce::jmax (0, numPeakColumns), {});
        const auto numSamples = source.getNumSamples();
        if (numSamples <= 0 || numPeakColumns <= 0 || source.getNumChannels() <= 0)
            return;

        for (int col = 0; col < numPeakColumns; ++col)
        {
            const auto start = (int) ((juce::int64) col * numSamples / numPeakColumns);
            const auto end = (int) ((juce::int64) (col + 1) * numSamples / numPeakColumns);
            const auto count = juce::jmax (1, end - start);

            float minV = 0.0f, maxV = 0.0f;
            for (int ch = 0; ch < source.getNumChannels(); ++ch)
            {
                const auto r = source.findMinMax (ch, start, count);
                minV = juce::jmin (minV, r.getStart());
                maxV = juce::jmax (maxV, r.getEnd());
            }
            peaks[(size_t) col] = { minV, maxV };
        }

        // Auto-scale to the buffer's own peak, like any waveform display -- a quiet
        // (e.g. un-normalised, -20dBFS-peak) source should still fill the view, not
        // just whatever fraction of +-1.0 it happens to hit.
        float overallPeak = 0.0f;
        for (const auto& p : peaks)
            overallPeak = juce::jmax (overallPeak, std::abs (p.getStart()), std::abs (p.getEnd()));

        if (overallPeak > 1.0e-6f)
        {
            const auto scale = 1.0f / overallPeak;
            for (auto& p : peaks)
                p = { p.getStart() * scale, p.getEnd() * scale };
        }
    }

    juce::Range<float> getPeakAt (int column) const noexcept
    {
        if (column < 0 || column >= (int) peaks.size())
            return {};
        return peaks[(size_t) column];
    }

    int getNumColumns() const noexcept { return (int) peaks.size(); }

private:
    std::vector<juce::Range<float>> peaks;
};
