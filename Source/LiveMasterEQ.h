#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include "MasterEQ.h"
#include "Parameters.h"
#include "SlopeFilter.h"

// The real-time counterpart to MasterEQ: same filter core (SlopeFilter), same
// BandParams, but processes continuously across processBlock() calls instead of doing
// one fixed-coefficient pass on a static buffer. Filter state is NEVER reset between
// calls (only MasterEQ::processBuffer's offline, one-shot use does that) -- the TPT/SVF
// topology inside SlopeFilter is specifically designed to tolerate a coefficient change
// while its state is non-zero, which is what makes it safe to recompute coefficients
// every block (audio-rate parameter changes) without zipper noise or a reset click.
//
// Lives at the very end of the processing chain, after the live A/B convolution (see
// the plan's "a teljes lánc végén" requirement) -- so unlike MasterEQ (still used
// offline by ExportEngine to bake the same curve into the exported IR), this one
// shapes the actual host audio passing through the plugin, not the IR/kernel itself.
//
// Frequency/gain/Q are ramped (~30ms) and the coefficients re-derived every few samples
// while a ramp is running: a host block can be 1024+ samples, and jumping straight to
// the new coefficients once per block is audible as stepping/zipper clicks on a fader
// drag. A band that gets switched back on starts from cleared filter state rather than
// whatever was frozen in it when it was switched off.
class LiveMasterEQ
{
public:
    void prepare (double sampleRateIn) noexcept
    {
        sampleRate = sampleRateIn;
        for (auto& f : filtersL) f.reset();
        for (auto& f : filtersR) f.reset();
        for (auto& st : bandStates)
        {
            st.frequency.reset (sampleRate, rampSeconds);
            st.gainDb.reset (sampleRate, rampSeconds);
            st.q.reset (sampleRate, rampSeconds);
            st.wasActive = false;
        }
    }

    void processBlock (juce::dsp::AudioBlock<float>& block,
                        const std::array<MasterEQ::BandParams, IRComposerConstants::numMasterBands>& bands) noexcept
    {
        const auto numSamples = (int) block.getNumSamples();
        const auto numChannels = (int) block.getNumChannels();
        if (numSamples <= 0 || numChannels <= 0)
            return;

        auto* left = block.getChannelPointer (0);
        auto* right = numChannels > 1 ? block.getChannelPointer (1) : nullptr;

        for (int b = 0; b < IRComposerConstants::numMasterBands; ++b)
        {
            const auto& band = bands[(size_t) b];
            auto& st = bandStates[(size_t) b];
            auto& filterL = filtersL[(size_t) b];
            auto& filterR = filtersR[(size_t) b];

            if (! band.active)
            {
                st.wasActive = false;
                continue;
            }

            if (! st.wasActive)
            {
                filterL.reset();
                filterR.reset();
                st.frequency.setCurrentAndTargetValue (band.frequencyHz);
                st.gainDb.setCurrentAndTargetValue (band.gainDb);
                st.q.setCurrentAndTargetValue (band.q);
                st.wasActive = true;
            }
            else
            {
                st.frequency.setTargetValue (band.frequencyHz);
                st.gainDb.setTargetValue (band.gainDb);
                st.q.setTargetValue (band.q);
            }

            for (int start = 0; start < numSamples;)
            {
                const auto smoothing = st.frequency.isSmoothing() || st.gainDb.isSmoothing() || st.q.isSmoothing();
                const auto count = smoothing ? juce::jmin (coefficientUpdateInterval, numSamples - start)
                                             : numSamples - start;

                const auto f = st.frequency.skip (count);
                const auto g = st.gainDb.skip (count);
                const auto q = st.q.skip (count);
                filterL.setCoefficients (sampleRate, f, g, q, band.type, band.slope);
                filterR.setCoefficients (sampleRate, f, g, q, band.type, band.slope);

                for (int i = start; i < start + count; ++i)
                {
                    left[i] = filterL.processSample (left[i]).output;
                    if (right != nullptr)
                        right[i] = filterR.processSample (right[i]).output;
                }
                start += count;
            }
        }
    }

private:
    static constexpr double rampSeconds = 0.03;
    static constexpr int coefficientUpdateInterval = 16;

    struct BandState
    {
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> frequency { 1000.0f };
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> gainDb { 0.0f };
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> q { 0.707f };
        bool wasActive = false;
    };

    double sampleRate = 44100.0;
    std::array<SlopeFilter, IRComposerConstants::numMasterBands> filtersL, filtersR;
    std::array<BandState, IRComposerConstants::numMasterBands> bandStates;
};
