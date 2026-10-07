#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cmath>

// Broadband "phase rotate" control -- original design, no sibling-project precedent.
// A cascade of first-order TPT all-pass sections sharing one coefficient `a`, driven
// by a single -180..+180 degree knob. Always unity-magnitude and stable for any
// |a| < 1 regardless of frequency, so this is deliberately simple and safe rather than
// a literal per-frequency phase guarantee.
//
// No real causal filter gives a frequency-independent fixed-degree phase shift (that's
// what a Hilbert transform approximates, a much heavier construct). This knob is a
// nominal, monotonic control: more knob = more cascaded phase smear in one direction.
// That is exactly the practical tool the brief asks for -- a way to nudge one slot's
// waveform phase relative to the others so they can be eyeballed into alignment on the
// combined overlay view, not a mathematically exact phase guarantee.
//
// Runs offline (once per IRSlot render pass on a static buffer), so -- like MasterEQ --
// no control-rate coefficient smoothing is needed; every render starts from reset().
class PhaseRotateFilter
{
public:
    void reset() noexcept
    {
        for (auto& z : stateL) z = 0.0f;
        for (auto& z : stateR) z = 0.0f;
    }

    void processBuffer (juce::AudioBuffer<float>& buffer, float rotateDegrees) noexcept
    {
        reset();

        const auto a = juce::jlimit (-0.95f, 0.95f, (rotateDegrees / 180.0f) * 0.95f);
        if (std::abs (a) < 1.0e-6f)
            return;

        const auto numChannels = buffer.getNumChannels();
        const auto numSamples = buffer.getNumSamples();
        auto* left = buffer.getWritePointer (0);
        auto* right = numChannels > 1 ? buffer.getWritePointer (1) : nullptr;

        for (int i = 0; i < numSamples; ++i)
        {
            left[i] = processCascade (left[i], a, stateL);
            if (right != nullptr)
                right[i] = processCascade (right[i], a, stateR);
        }
    }

private:
    static constexpr int numStages = 6;

    // One-multiplier first-order all-pass (Regalia/Mitra form): with w[n] = x[n] +
    // a*w[n-1] and y[n] = -a*w[n] + w[n-1], H(z) = (z^-1 - a) / (1 - a*z^-1) -- numerator
    // and denominator are coefficient-reverses of each other, the defining property of
    // an all-pass, with a pole at z=a (stable for any |a| < 1) and a mirror-image zero.
    static float processCascade (float x, float a, std::array<float, numStages>& state) noexcept
    {
        float y = x;
        for (int s = 0; s < numStages; ++s)
        {
            const auto wPrev = state[(size_t) s];
            const auto w = y + a * wPrev;
            y = -a * w + wPrev;
            state[(size_t) s] = w;
        }
        return y;
    }

    std::array<float, numStages> stateL {};
    std::array<float, numStages> stateR {};
};
