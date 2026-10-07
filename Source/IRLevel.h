#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <vector>

// Level-matching for the live-audition master IR.
//
// Peak-normalising an IR (largest sample = 1.0) says almost nothing about how loud
// the convolution output will be: a typical guitar-cab IR normalised that way boosts
// the 100Hz-1kHz range by +20..+26dB, so a hot amp-sim output (~0dBFS) arriving in
// front of the plugin came out at +20dBFS and above. Instead, this scales the IR so
// its magnitude response, averaged (in power, on a log-frequency grid) across the
// musically relevant 80Hz-8kHz band, sits at 0dB -- i.e. the plugin roughly keeps the
// input's perceived level instead of adding a huge, IR-dependent gain.
namespace IRLevel
{
    inline constexpr float referenceLowHz = 80.0f;
    inline constexpr float referenceHighHz = 8000.0f;

    // Average magnitude (linear) of one channel's frequency response over the
    // reference band. Returns 0 for an empty/silent channel.
    inline float bandAverageMagnitude (const float* samples, int numSamples, double sampleRate)
    {
        if (samples == nullptr || numSamples <= 0 || sampleRate <= 0.0)
            return 0.0f;

        // Zero-padded to at least 2^14 for decent low-frequency resolution; anything
        // past 2^20 samples (~20s) is irrelevant tail as far as level goes.
        constexpr int minOrder = 14, maxOrder = 20;
        int order = minOrder;
        while (order < maxOrder && (1 << order) < numSamples)
            ++order;
        const auto fftSize = 1 << order;
        const auto samplesToUse = juce::jmin (numSamples, fftSize);

        std::vector<float> data ((size_t) fftSize * 2, 0.0f);
        for (int i = 0; i < samplesToUse; ++i)
            data[(size_t) i] = std::isfinite (samples[i]) ? samples[i] : 0.0f;

        juce::dsp::FFT fft (order);
        fft.performFrequencyOnlyForwardTransform (data.data(), true);

        const auto highHz = juce::jmin ((double) referenceHighHz, sampleRate * 0.45);
        const auto lowHz = juce::jmin ((double) referenceLowHz, highHz * 0.5);
        constexpr int numPoints = 256;
        double powerSum = 0.0;
        for (int p = 0; p < numPoints; ++p)
        {
            const auto freq = lowHz * std::pow (highHz / lowHz, (double) p / (double) (numPoints - 1));
            const auto bin = juce::jlimit (0, fftSize / 2, (int) std::lround (freq * fftSize / sampleRate));
            const auto mag = (double) data[(size_t) bin];
            powerSum += mag * mag;
        }
        return (float) std::sqrt (powerSum / numPoints);
    }

    // Linear gain that brings the louder channel's band-average response to 0dB.
    // Returns 1 for a silent buffer.
    inline float computeNormalisationGain (const juce::AudioBuffer<float>& ir, double sampleRate)
    {
        float reference = 0.0f;
        for (int ch = 0; ch < ir.getNumChannels(); ++ch)
            reference = juce::jmax (reference, bandAverageMagnitude (ir.getReadPointer (ch), ir.getNumSamples(), sampleRate));
        return reference > 1.0e-9f ? 1.0f / reference : 1.0f;
    }
}
