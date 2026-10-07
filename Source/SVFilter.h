#pragma once

#include <juce_core/juce_core.h>
#include <cmath>
#include "Parameters.h"

// Andy Simper's ("Cytomic") Topology-Preserving-Transform state-variable filter.
// Near-verbatim copy of parametric-dynamic-eq-VST/Source/SVFilter.h -- see that file's
// header comment for the full rationale (TPT state stays valid across a coefficient
// change, unlike a Direct-Form biquad's raw delay-line state). IR Composer's MasterEQ
// runs this offline, once per rebuild, from a fresh reset() each time (see the plan's
// "no control-rate smoothing needed" note), so the modulation-safety property this
// topology buys isn't strictly required here -- it's still the right filter core to
// reuse rather than inventing a different one for a single fixed-coefficient pass.
class SVFilter
{
public:
    struct Result
    {
        float output = 0.0f;
        float bandpass = 0.0f;
    };

    void reset() noexcept
    {
        ic1eq = 0.0f;
        ic2eq = 0.0f;
    }

    void setCoefficients (double sampleRate, float frequencyHz, float gainDb, float qValue, FilterType type) noexcept
    {
        const auto nyquist = (float) (sampleRate * 0.5);
        const auto freq = juce::jlimit (10.0f, nyquist * 0.98f, frequencyHz);
        const auto qClamped = juce::jlimit (0.05f, 40.0f, qValue);
        const auto a = std::pow (10.0f, gainDb / 40.0f);
        const auto wc = juce::MathConstants<float>::pi * freq / (float) sampleRate;

        float g = std::tan (wc);
        float k = 1.0f / qClamped;

        switch (type)
        {
            case FilterType::Bell:
                k = 1.0f / (qClamped * a);
                m0 = 1.0f; m1 = k * (a * a - 1.0f); m2 = 0.0f;
                break;
            case FilterType::LowShelf:
                g = std::tan (wc) / std::sqrt (a);
                m0 = 1.0f; m1 = k * (a - 1.0f); m2 = a * a - 1.0f;
                break;
            case FilterType::HighShelf:
                g = std::tan (wc) * std::sqrt (a);
                m0 = a * a; m1 = k * (1.0f - a) * a; m2 = 1.0f - a * a;
                break;
            case FilterType::HighPass:
                m0 = 1.0f; m1 = -k; m2 = -1.0f;
                break;
            case FilterType::LowPass:
            default:
                m0 = 0.0f; m1 = 0.0f; m2 = 1.0f;
                break;
        }

        coeffA1 = 1.0f / (1.0f + g * (g + k));
        coeffA2 = g * coeffA1;
        coeffA3 = g * coeffA2;
    }

    Result processSample (float x) noexcept
    {
        const auto v3 = x - ic2eq;
        const auto v1 = coeffA1 * ic1eq + coeffA2 * v3;
        const auto v2 = ic2eq + coeffA2 * ic1eq + coeffA3 * v3;
        ic1eq = 2.0f * v1 - ic1eq;
        ic2eq = 2.0f * v2 - ic2eq;
        return { m0 * x + m1 * v1 + m2 * v2, v1 };
    }

private:
    float coeffA1 = 0.0f, coeffA2 = 0.0f, coeffA3 = 0.0f;
    float m0 = 0.0f, m1 = 0.0f, m2 = 0.0f;
    float ic1eq = 0.0f, ic2eq = 0.0f;
};
