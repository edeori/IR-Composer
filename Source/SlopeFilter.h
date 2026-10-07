#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <cmath>
#include "SVFilter.h"
#include "Parameters.h"

// A trapezoidal ("TPT") one-pole high/low pass -- the 1-pole sibling of SVFilter's
// 2-pole design, built the same way (state stays valid across a coefficient change,
// so it's just as safe to recoefficient at control-rate) so a 6dB/oct slope doesn't
// need a different safety story than the 12dB/oct case. Near-verbatim copy of
// parametric-dynamic-eq-VST/Source/SlopeFilter.h's OnePoleFilter.
class OnePoleFilter
{
public:
    void reset() noexcept { z = 0.0f; }

    void setCoefficients (double sampleRate, float frequencyHz, bool highPass) noexcept
    {
        const auto nyquist = (float) (sampleRate * 0.5);
        const auto freq = juce::jlimit (10.0f, nyquist * 0.98f, frequencyHz);
        const auto g = std::tan (juce::MathConstants<float>::pi * freq / (float) sampleRate);
        coeffA = g / (1.0f + g);
        isHighPass = highPass;
    }

    float processSample (float x) noexcept
    {
        const auto v = (x - z) * coeffA;
        const auto lowpass = v + z;
        z = lowpass + v;
        return isHighPass ? (x - lowpass) : lowpass;
    }

private:
    float coeffA = 0.0f;
    float z = 0.0f;
    bool isHighPass = false;
};

// Selectable-slope High Pass / Low Pass built by cascading SVFilter (2-pole, 12dB/oct)
// and OnePoleFilter (1-pole, 6dB/oct) stages -- 6=1x one-pole, 12=1x SVF, 18=one-pole+SVF,
// 24=2x SVF. For Bell/Shelf types (where "slope" has no meaning) this is just a single
// SVFilter passthrough, so MasterEQ can use SlopeFilter unconditionally for every type.
// Near-verbatim copy of parametric-dynamic-eq-VST/Source/SlopeFilter.h, per the user's
// request for the same parametric depth as that source EQ.
class SlopeFilter
{
public:
    struct Result { float output = 0.0f; float bandpass = 0.0f; };

    void reset() noexcept
    {
        for (auto& stage : svfStages) stage.reset();
        onePole.reset();
    }

    void setCoefficients (double sampleRate, float frequencyHz, float gainDb, float q,
                          FilterType type, FilterSlope slope) noexcept
    {
        isHighLowPass = (type == FilterType::HighPass || type == FilterType::LowPass);
        if (! isHighLowPass)
        {
            numSvfStages = 1;
            useOnePole = false;
            svfStages[0].setCoefficients (sampleRate, frequencyHz, gainDb, q, type);
            return;
        }

        switch (slope)
        {
            case FilterSlope::Slope6:  numSvfStages = 0; useOnePole = true;  break;
            case FilterSlope::Slope18: numSvfStages = 1; useOnePole = true;  break;
            case FilterSlope::Slope24: numSvfStages = 2; useOnePole = false; break;
            case FilterSlope::Slope12:
            default:                  numSvfStages = 1; useOnePole = false; break;
        }
        for (int i = 0; i < numSvfStages; ++i)
            svfStages[(size_t) i].setCoefficients (sampleRate, frequencyHz, 0.0f, q, type);
        if (useOnePole)
            onePole.setCoefficients (sampleRate, frequencyHz, type == FilterType::HighPass);
    }

    Result processSample (float x) noexcept
    {
        float y = x;
        float firstStageBandpass = 0.0f;

        if (numSvfStages >= 1)
        {
            const auto r = svfStages[0].processSample (y);
            y = r.output;
            firstStageBandpass = r.bandpass;
        }
        if (useOnePole)
            y = onePole.processSample (y);
        if (numSvfStages >= 2)
            y = svfStages[1].processSample (y).output;

        return { y, isHighLowPass ? y : firstStageBandpass };
    }

private:
    std::array<SVFilter, 2> svfStages;
    OnePoleFilter onePole;
    int numSvfStages = 1;
    bool useOnePole = false;
    bool isHighLowPass = false;
};
