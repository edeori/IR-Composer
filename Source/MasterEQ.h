#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include "Parameters.h"
#include "SlopeFilter.h"

// The single, post-blend EQ: the user clarified that EQ only ever applies to the
// summed/blended signal, never per source IR slot (see the plan's "EQ csak az
// összegzett jelre" decision). BlendEngine sums the 4 processed IRSlot buffers and
// then runs the result through one MasterEQ instance before it becomes the master IR
// fed to live convolution / export.
//
// Runs entirely offline (once per BlendEngine rebuild pass, on a static buffer), so
// unlike the sibling project's EQBand there is no need for juce::SmoothedValue
// control-rate coefficient smoothing -- every render starts from reset() with one
// fixed coefficient set. Uses SlopeFilter (selectable 6/12/18/24 dB/oct for High/Low
// Pass bands, same as parametric-dynamic-eq-VST) per the user's request for the same
// parametric depth as that source EQ -- still no dynamics/M-S/oversampling, which
// remain deliberately out of scope (see the plan's non-goals).
class MasterEQ
{
public:
    struct BandParams
    {
        bool active = false;
        float frequencyHz = 1000.0f;
        float gainDb = 0.0f;
        float q = 0.707f;
        FilterType type = FilterType::Bell;
        FilterSlope slope = FilterSlope::Slope12;
    };

    // Processes the buffer in place. Assumes 1 or 2 channels.
    void processBuffer (juce::AudioBuffer<float>& buffer, double sampleRate,
                         const std::array<BandParams, IRComposerConstants::numMasterBands>& bands) noexcept
    {
        const auto numChannels = buffer.getNumChannels();
        const auto numSamples = buffer.getNumSamples();

        for (int b = 0; b < IRComposerConstants::numMasterBands; ++b)
        {
            const auto& band = bands[(size_t) b];
            if (! band.active)
                continue;

            filtersL[(size_t) b].reset();
            filtersR[(size_t) b].reset();
            filtersL[(size_t) b].setCoefficients (sampleRate, band.frequencyHz, band.gainDb, band.q, band.type, band.slope);
            filtersR[(size_t) b].setCoefficients (sampleRate, band.frequencyHz, band.gainDb, band.q, band.type, band.slope);

            auto* left = buffer.getWritePointer (0);
            auto* right = numChannels > 1 ? buffer.getWritePointer (1) : nullptr;

            for (int i = 0; i < numSamples; ++i)
            {
                left[i] = filtersL[(size_t) b].processSample (left[i]).output;
                if (right != nullptr)
                    right[i] = filtersR[(size_t) b].processSample (right[i]).output;
            }
        }
    }

private:
    std::array<SlopeFilter, IRComposerConstants::numMasterBands> filtersL, filtersR;
};
