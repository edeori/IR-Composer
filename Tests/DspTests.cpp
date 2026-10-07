#include "../Source/SVFilter.h"
#include "../Source/SlopeFilter.h"
#include "../Source/MasterEQ.h"
#include "../Source/LiveMasterEQ.h"
#include "../Source/PhaseRotateFilter.h"
#include "../Source/IRSlot.h"
#include "../Source/IRLevel.h"
#include "../Source/BlendWeights.h"
#include "../Source/IRAlign.h"
#include <juce_core/juce_core.h>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace
{
    int failures = 0;

    bool closeEnough (float a, float b, float tolerance = 1.0e-4f)
    {
        return std::abs (a - b) <= tolerance;
    }

    void check (bool condition, const std::string& name)
    {
        if (! condition)
        {
            std::cerr << "FAILED: " << name << '\n';
            ++failures;
        }
        else
        {
            std::cout << "  ok: " << name << '\n';
        }
    }

    constexpr double sampleRate = 48000.0;

    // Feeds a sine of the given frequency/amplitude through the filter for enough
    // samples to reach a settled steady state, returning the RMS amplitude of the
    // final `measureSamples` (skipping the settling window entirely).
    float settledRms (SVFilter& filter, float sineFreqHz, float amplitude, int totalSamples, int measureSamples)
    {
        std::vector<float> tail;
        tail.reserve ((size_t) measureSamples);
        double phase = 0.0;
        const auto phaseInc = juce::MathConstants<double>::twoPi * sineFreqHz / sampleRate;
        for (int i = 0; i < totalSamples; ++i)
        {
            const auto x = amplitude * (float) std::sin (phase);
            phase += phaseInc;
            const auto result = filter.processSample (x);
            if (i >= totalSamples - measureSamples)
                tail.push_back (result.output);
        }
        double sumSquares = 0.0;
        for (auto v : tail)
            sumSquares += (double) v * (double) v;
        return (float) std::sqrt (sumSquares / (double) tail.size());
    }

    // Same idea as settledRms() above, but for a whole SlopeFilter (HP/LP with a
    // selectable slope) instead of a bare SVFilter.
    float settledRms (SlopeFilter& filter, float sineFreqHz, float amplitude, int totalSamples, int measureSamples)
    {
        std::vector<float> tail;
        tail.reserve ((size_t) measureSamples);
        double phase = 0.0;
        const auto phaseInc = juce::MathConstants<double>::twoPi * sineFreqHz / sampleRate;
        for (int i = 0; i < totalSamples; ++i)
        {
            const auto x = amplitude * (float) std::sin (phase);
            phase += phaseInc;
            const auto result = filter.processSample (x);
            if (i >= totalSamples - measureSamples)
                tail.push_back (result.output);
        }
        double sumSquares = 0.0;
        for (auto v : tail)
            sumSquares += (double) v * (double) v;
        return (float) std::sqrt (sumSquares / (double) tail.size());
    }

    float rmsOf (const std::vector<float>& v)
    {
        double sumSquares = 0.0;
        for (auto x : v)
            sumSquares += (double) x * (double) x;
        return (float) std::sqrt (sumSquares / (double) v.size());
    }

    void testBellUnityGainAtZeroDb()
    {
        SVFilter filter;
        filter.setCoefficients (sampleRate, 1000.0f, 0.0f, 0.707f, FilterType::Bell);
        const auto rms = settledRms (filter, 1000.0f, 1.0f, 4000, 1000);
        check (closeEnough (rms, 1.0f / std::sqrt (2.0f), 0.01f), "SVFilter: Bell at 0dB gain leaves amplitude unchanged");
    }

    void testBellCutReducesGainAtCenterFrequency()
    {
        SVFilter filter;
        filter.setCoefficients (sampleRate, 2000.0f, -20.0f, 5.0f, FilterType::Bell);
        const auto rms = settledRms (filter, 2000.0f, 1.0f, 8000, 1000);
        const auto expected = (1.0f / std::sqrt (2.0f)) * juce::Decibels::decibelsToGain (-20.0f);
        check (rms < 1.0f / std::sqrt (2.0f), "SVFilter: Bell at -20dB gain reduces amplitude at the center frequency (not increases it)");
        check (closeEnough (rms, expected, 0.02f), "SVFilter: Bell at -20dB gain matches the expected ~-20dB reduction");
    }

    void testBellBoostIncreasesGainAtCenterFrequency()
    {
        SVFilter filter;
        filter.setCoefficients (sampleRate, 2000.0f, 12.0f, 0.707f, FilterType::Bell);
        const auto rms = settledRms (filter, 2000.0f, 0.5f, 8000, 1000);
        const auto expected = (0.5f / std::sqrt (2.0f)) * juce::Decibels::decibelsToGain (12.0f);
        check (closeEnough (rms, expected, 0.02f), "SVFilter: Bell at +12dB gain matches the expected ~+12dB boost");
    }

    void testMasterEQBellAtZeroGainIsUnity()
    {
        MasterEQ eq;
        std::array<MasterEQ::BandParams, IRComposerConstants::numMasterBands> bands {};
        bands[0] = { true, 1000.0f, 0.0f, 0.707f, FilterType::Bell };

        constexpr int numSamples = 4000;
        juce::AudioBuffer<float> buffer (2, numSamples);
        double phase = 0.0;
        const auto phaseInc = juce::MathConstants<double>::twoPi * 1000.0 / sampleRate;
        for (int i = 0; i < numSamples; ++i)
        {
            const auto s = (float) std::sin (phase);
            phase += phaseInc;
            buffer.setSample (0, i, s);
            buffer.setSample (1, i, s);
        }

        eq.processBuffer (buffer, sampleRate, bands);

        std::vector<float> tail (buffer.getReadPointer (0) + numSamples - 1000, buffer.getReadPointer (0) + numSamples);
        check (closeEnough (rmsOf (tail), 1.0f / std::sqrt (2.0f), 0.01f), "MasterEQ: active Bell band at 0dB leaves amplitude unchanged");
    }

    void testMasterEQInactiveBandIsBypassed()
    {
        MasterEQ eq;
        std::array<MasterEQ::BandParams, IRComposerConstants::numMasterBands> bands {};
        bands[0] = { false, 1000.0f, 24.0f, 0.707f, FilterType::Bell };

        juce::AudioBuffer<float> buffer (2, 8);
        for (int i = 0; i < 8; ++i)
        {
            buffer.setSample (0, i, 0.5f);
            buffer.setSample (1, i, -0.5f);
        }

        eq.processBuffer (buffer, sampleRate, bands);

        bool unchanged = true;
        for (int i = 0; i < 8; ++i)
            unchanged = unchanged && closeEnough (buffer.getSample (0, i), 0.5f) && closeEnough (buffer.getSample (1, i), -0.5f);
        check (unchanged, "MasterEQ: an inactive band leaves the buffer untouched");
    }

    void testPhaseRotatePreservesMagnitude()
    {
        for (float degrees : { -180.0f, -90.0f, 90.0f, 180.0f })
        {
            juce::AudioBuffer<float> buffer (2, 4000);
            double phase = 0.0;
            const auto phaseInc = juce::MathConstants<double>::twoPi * 1000.0 / sampleRate;
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                const auto s = (float) std::sin (phase);
                phase += phaseInc;
                buffer.setSample (0, i, s);
                buffer.setSample (1, i, s);
            }

            PhaseRotateFilter rotator;
            rotator.processBuffer (buffer, degrees);

            std::vector<float> tail (buffer.getReadPointer (0) + 3000, buffer.getReadPointer (0) + 4000);
            const auto rms = rmsOf (tail);
            check (closeEnough (rms, 1.0f / std::sqrt (2.0f), 0.02f),
                   "PhaseRotateFilter: all-pass cascade preserves amplitude at " + std::to_string ((int) degrees) + " degrees");

            bool allFinite = true;
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                allFinite = allFinite && std::isfinite (buffer.getSample (0, i)) && std::isfinite (buffer.getSample (1, i));
            check (allFinite, "PhaseRotateFilter: output stays finite/stable at " + std::to_string ((int) degrees) + " degrees");
        }
    }

    void testPhaseRotateAtZeroDegreesIsPassthrough()
    {
        juce::AudioBuffer<float> buffer (2, 8);
        for (int i = 0; i < 8; ++i)
        {
            buffer.setSample (0, i, 0.3f);
            buffer.setSample (1, i, -0.7f);
        }

        PhaseRotateFilter rotator;
        rotator.processBuffer (buffer, 0.0f);

        bool unchanged = true;
        for (int i = 0; i < 8; ++i)
            unchanged = unchanged && closeEnough (buffer.getSample (0, i), 0.3f) && closeEnough (buffer.getSample (1, i), -0.7f);
        check (unchanged, "PhaseRotateFilter: 0 degrees is a no-op passthrough");
    }

    juce::AudioBuffer<float> makeImpulseAt (int length, int impulseIndex, float amplitude = 1.0f)
    {
        juce::AudioBuffer<float> buffer (2, length);
        buffer.clear();
        buffer.setSample (0, impulseIndex, amplitude);
        buffer.setSample (1, impulseIndex, amplitude);
        return buffer;
    }

    int findPeakIndex (const juce::AudioBuffer<float>& buffer, int channel)
    {
        int peakIndex = -1;
        float peakValue = 0.0f;
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const auto v = std::abs (buffer.getSample (channel, i));
            if (v > peakValue)
            {
                peakValue = v;
                peakIndex = i;
            }
        }
        return peakIndex;
    }

    void testIRSlotCropShiftsImpulseToBufferStart()
    {
        IRSlot slot;
        slot.prepare (sampleRate);
        slot.setRawBuffer (makeImpulseAt (2000, 500), sampleRate);
        slot.setCropStartSample (500);

        const auto rendered = slot.renderProcessed (1000, {});
        check (findPeakIndex (rendered, 0) == 0, "IRSlot: crop moves the impulse to sample 0 of the rendered buffer");
    }

    void testIRSlotLeadingSilenceTrimPreservesStereoTiming()
    {
        IRSlot slot;
        slot.prepare (sampleRate);
        auto source = makeImpulseAt (2000, 500);
        source.clear (1, 0, source.getNumSamples());
        source.setSample (1, 520, 1.0f);
        slot.setRawBuffer (std::move (source), sampleRate);
        slot.autoTrimLeadingSilence();
        const auto rendered = slot.renderProcessed (1000, {});
        check (slot.getCropStartSample() == 500 && findPeakIndex (rendered, 0) == 0
               && findPeakIndex (rendered, 1) == 20,
               "IRSlot: onset trim removes file delay and preserves stereo offsets");
        IRSlot::RenderParams delayed;
        delayed.delayMs = 2.0f;
        check (findPeakIndex (slot.renderProcessed (1000, delayed), 0) == 96,
               "IRSlot: onset trim preserves the explicitly requested fine delay");
        slot.setCropStartSample (0);
        check (findPeakIndex (slot.renderProcessed (1000, {}), 0) == 500,
               "IRSlot: onset trim keeps the original samples available for manual crop");

        juce::AudioBuffer<float> silent (2, 1000);
        silent.clear();
        slot.setRawBuffer (std::move (silent), sampleRate);
        slot.autoTrimLeadingSilence();
        check (slot.getCropStartSample() == 0 && slot.getCropEndSample() == 1000,
               "IRSlot: onset trim leaves an entirely silent file valid");
    }

    void testIRSlotPolarityInvertsSign()
    {
        IRSlot slot;
        slot.prepare (sampleRate);
        slot.setRawBuffer (makeImpulseAt (1000, 0, 0.8f), sampleRate);

        IRSlot::RenderParams normal;
        IRSlot::RenderParams inverted;
        inverted.polarityInverted = true;

        const auto a = slot.renderProcessed (500, normal);
        const auto b = slot.renderProcessed (500, inverted);
        check (closeEnough (a.getSample (0, 0), -b.getSample (0, 0), 1.0e-3f),
               "IRSlot: polarity invert flips the sign of the rendered output");
    }

    void testIRSlotGainAppliesLinearScale()
    {
        IRSlot slot;
        slot.prepare (sampleRate);
        slot.setRawBuffer (makeImpulseAt (1000, 0, 1.0f), sampleRate);

        IRSlot::RenderParams params;
        params.gainDb = -6.0f;
        const auto rendered = slot.renderProcessed (500, params);
        check (closeEnough (rendered.getSample (0, 0), juce::Decibels::decibelsToGain (-6.0f), 1.0e-3f),
               "IRSlot: gainDb is applied as a linear scale to the rendered output");
    }

    void testIRSlotCropEndTrimsTail()
    {
        IRSlot slot;
        slot.prepare (sampleRate);
        slot.setRawBuffer (makeImpulseAt (2000, 800), sampleRate);
        slot.setCropEndSample (500); // the impulse at 800 is now outside the kept region

        const auto rendered = slot.renderProcessed (1000, {});
        float peak = 0.0f;
        for (int i = 0; i < rendered.getNumSamples(); ++i)
            peak = juce::jmax (peak, std::abs (rendered.getSample (0, i)));
        check (peak < 1.0e-6f, "IRSlot: crop-end excludes content past the end marker");
    }

    void testIRSlotCropEndKeepsContentBeforeIt()
    {
        IRSlot slot;
        slot.prepare (sampleRate);
        slot.setRawBuffer (makeImpulseAt (2000, 300), sampleRate);
        slot.setCropEndSample (500); // the impulse at 300 is still inside the kept region

        const auto rendered = slot.renderProcessed (1000, {});
        check (findPeakIndex (rendered, 0) == 300, "IRSlot: crop-end keeps content that falls before the end marker");
    }

    void testIRSlotCropEndCannotGoBeforeStart()
    {
        IRSlot slot;
        slot.prepare (sampleRate);
        slot.setRawBuffer (makeImpulseAt (2000, 0), sampleRate);
        slot.setCropStartSample (600);
        slot.setCropEndSample (400); // attempt to place the end before the start

        check (slot.getCropEndSample() >= slot.getCropStartSample(),
               "IRSlot: crop-end is clamped so it can never sit before crop-start");
    }

    void testIRSlotAutoTrimsLongSilentTailOnLoad()
    {
        // A short burst of real signal followed by several seconds of true silence --
        // standing in for a long real-world capture (e.g. a deconvolved sweep
        // recording) whose meaningful content ends well before the file itself does.
        constexpr int totalLength = 5 * (int) sampleRate; // 5 seconds
        constexpr int signalLength = 2000;
        juce::AudioBuffer<float> buffer (2, totalLength);
        buffer.clear();
        for (int i = 0; i < signalLength; ++i)
        {
            const auto v = (i % 7 == 0) ? 1.0f : 0.3f; // some full-scale peaks, some lower content
            buffer.setSample (0, i, v);
            buffer.setSample (1, i, v);
        }

        IRSlot slot;
        slot.prepare (sampleRate);
        slot.setRawBuffer (buffer, sampleRate);

        check (slot.getCropEndSample() < totalLength / 2,
               "IRSlot: a fresh load auto-trims a long silent tail instead of defaulting to the full file length");
        check (slot.getCropEndSample() >= signalLength,
               "IRSlot: auto-trim doesn't cut into the real signal, only the silence after it");
    }

    void testIRSlotAutoTrimKeepsFullLengthWhenContentFillsTheBuffer()
    {
        IRSlot slot;
        slot.prepare (sampleRate);
        juce::AudioBuffer<float> buffer (2, 4000);
        double phase = 0.0;
        const auto phaseInc = juce::MathConstants<double>::twoPi * 1000.0 / sampleRate;
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const auto s = (float) std::sin (phase);
            phase += phaseInc;
            buffer.setSample (0, i, s);
            buffer.setSample (1, i, s);
        }

        slot.setRawBuffer (buffer, sampleRate);
        check (slot.getCropEndSample() == 4000,
               "IRSlot: auto-trim keeps the full length when there's no long silent tail to remove");
    }

    void testIRSlotAutoTrimRejectsDisconnectedCaptureEvents()
    {
        for (double rate : { 44100.0, 48000.0, 96000.0 })
        {
            const auto onset = (int) (rate * 0.3);
            const auto echo = (int) (rate * 2.8);
            auto source = makeImpulseAt ((int) (rate * 3), onset);
            source.setSample (0, echo, 0.2f);
            source.setSample (1, echo, 0.2f);
            IRSlot slot;
            slot.prepare (rate);
            slot.setRawBuffer (source, rate);
            slot.autoTrimLeadingSilence();
            check (slot.getCropStartSample() == onset && slot.getCropEndSample() > onset
                   && slot.getCropEndSample() < (juce::int64) (rate * 0.5),
                   "IRSlot: leading silence is retained until onset, disconnected late capture is excluded");
            slot.setCropEndSample (source.getNumSamples());
            const auto manual = slot.renderProcessed (source.getNumSamples(), {});
            check (closeEnough (manual.getSample (0, echo - onset), 0.2f, 1.0e-6f),
                   "IRSlot: manual crop can recover an intentional late reflection");

            // A brief gap is not a settled tail; content in either stereo channel
            // keeps a continuous long response alive.
            source.clear();
            source.setSample (0, 0, 1.0f);
            for (int i = (int) (rate * 0.05); i < (int) (rate * 2); ++i)
                source.setSample (1, i, 0.01f);
            slot.setRawBuffer (source, rate);
            check (slot.getCropEndSample() >= (juce::int64) (rate * 2),
                   "IRSlot: a short quiet gap and a long stereo tail are preserved");
        }
    }

    void testIRSlotAutoTrimTailCanBeReTriggeredManually()
    {
        constexpr int totalLength = 3 * (int) sampleRate;
        constexpr int signalLength = 1000;
        juce::AudioBuffer<float> buffer (2, totalLength);
        buffer.clear();
        for (int i = 0; i < signalLength; ++i)
        {
            buffer.setSample (0, i, 0.8f);
            buffer.setSample (1, i, 0.8f);
        }

        IRSlot slot;
        slot.prepare (sampleRate);
        slot.setRawBuffer (buffer, sampleRate);
        slot.setCropEndSample (totalLength); // user manually drags the end marker back out to the full file
        check (slot.getCropEndSample() == totalLength, "IRSlot: the end marker can be manually dragged past the auto-trim point");

        slot.autoTrimTail();
        check (slot.getCropEndSample() < totalLength / 2,
               "IRSlot: autoTrimTail() re-applies the silence trim on demand after a manual override");
    }

    void testSlopeFilterSteeperSlopeAttenuatesMoreBelowCutoff()
    {
        // A tone well below a low-pass's cutoff should be attenuated more by a steeper
        // (24 dB/oct) slope than by a shallower (6 dB/oct) one -- same property the
        // sibling project's testFilterSlopeIncreasesRolloffSteepness checks.
        auto measure = [] (FilterSlope slope)
        {
            SlopeFilter filter;
            filter.setCoefficients (sampleRate, 1000.0f, 0.0f, 0.707f, FilterType::LowPass, slope);
            return settledRms (filter, 4000.0f, 1.0f, 8000, 1000);
        };

        const auto rms6 = measure (FilterSlope::Slope6);
        const auto rms24 = measure (FilterSlope::Slope24);
        check (rms24 < rms6, "SlopeFilter: 24dB/oct low-pass attenuates a tone above cutoff more than 6dB/oct");
    }

    void testSlopeFilterStableAtEverySlope()
    {
        for (auto slope : { FilterSlope::Slope6, FilterSlope::Slope12, FilterSlope::Slope18, FilterSlope::Slope24 })
        {
            SlopeFilter filter;
            filter.setCoefficients (sampleRate, 1000.0f, 0.0f, 0.707f, FilterType::HighPass, slope);
            const auto rms = settledRms (filter, 1000.0f, 1.0f, 4000, 500);
            check (std::isfinite (rms), "SlopeFilter: high-pass stays finite/stable at every selectable slope");
        }
    }

    void testIRSlotMonoSourceIsDuplicatedToStereo()
    {
        IRSlot slot;
        slot.prepare (sampleRate);
        juce::AudioBuffer<float> mono (1, 1000);
        mono.clear();
        mono.setSample (0, 0, 0.6f);
        slot.setRawBuffer (mono, sampleRate);

        const auto rendered = slot.renderProcessed (500, {});
        check (closeEnough (rendered.getSample (0, 0), rendered.getSample (1, 0)),
               "IRSlot: a mono source is duplicated identically to both channels");
    }

    void testIRLevelUnitImpulseNeedsNoGain()
    {
        juce::AudioBuffer<float> ir (2, 4096);
        ir.clear();
        ir.setSample (0, 0, 1.0f);
        ir.setSample (1, 0, 1.0f);
        const auto gain = IRLevel::computeNormalisationGain (ir, sampleRate);
        check (closeEnough (gain, 1.0f, 1.0e-3f), "IRLevel: a unit impulse (flat 0dB response) gets unity gain");
    }

    // A peak-normalised, strongly resonant "cab-like" IR used to add 20dB+ of gain in
    // the guitar range -- the bug that made hot amp-sim signals blow up the host.
    void testIRLevelTamesPeakNormalisedResonantIR()
    {
        const int irLength = 4800;
        juce::AudioBuffer<float> ir (2, irLength);
        for (int i = 0; i < irLength; ++i)
        {
            const auto t = (double) i / sampleRate;
            const auto v = (float) (std::exp (-t * 40.0) * std::sin (juce::MathConstants<double>::twoPi * 250.0 * t)
                                    + 0.3 * std::exp (-t * 60.0) * std::sin (juce::MathConstants<double>::twoPi * 2500.0 * t));
            ir.setSample (0, i, v);
            ir.setSample (1, i, v);
        }
        ir.applyGain (1.0f / ir.getMagnitude (0, 0, irLength));

        // Guitar-ish, heavily clipped two-note input at 0dBFS.
        const int inLength = 24000;
        std::vector<float> input ((size_t) inLength);
        for (int i = 0; i < inLength; ++i)
        {
            const auto t = (double) i / sampleRate;
            input[(size_t) i] = (float) std::tanh (20.0 * std::sin (juce::MathConstants<double>::twoPi * 110.0 * t)
                                                  + 10.0 * std::sin (juce::MathConstants<double>::twoPi * 165.0 * t));
        }

        const auto outputRms = [&] (float irGain)
        {
            const auto* h = ir.getReadPointer (0);
            double sumSquares = 0.0;
            int counted = 0;
            for (int n = irLength; n < inLength; ++n)
            {
                double y = 0.0;
                for (int k = 0; k < irLength; ++k)
                    y += (double) h[k] * (double) input[(size_t) (n - k)];
                y *= irGain;
                sumSquares += y * y;
                ++counted;
            }
            return std::sqrt (sumSquares / counted);
        };

        double inSumSquares = 0.0;
        for (auto v : input)
            inSumSquares += (double) v * (double) v;
        const auto inputRms = std::sqrt (inSumSquares / inLength);

        const auto oldGainDb = juce::Decibels::gainToDecibels (outputRms (1.0f) / inputRms);
        const auto newGainDb = juce::Decibels::gainToDecibels (outputRms (IRLevel::computeNormalisationGain (ir, sampleRate)) / inputRms);
        std::cout << "    peak-normalised: " << oldGainDb << " dB, level-matched: " << newGainDb << " dB\n";

        check (oldGainDb > 12.0, "IRLevel: the old peak normalisation really did add a large gain (test sanity)");
        check (std::abs (newGainDb) < 6.0, "IRLevel: a level-matched resonant IR stays within +-6dB of the input level");
    }

    // A decaying, non-trivial "cab-like" IR starting at onsetSample, so cross-
    // correlation has a single clear peak rather than a flat plateau.
    juce::AudioBuffer<float> makeDecayingIR (int numSamples, double onsetSample, float sign = 1.0f)
    {
        juce::AudioBuffer<float> ir (2, numSamples);
        ir.clear();
        juce::Random rng (1234);
        std::vector<float> shape (2048);
        for (size_t i = 0; i < shape.size(); ++i)
            shape[i] = (rng.nextFloat() * 2.0f - 1.0f) * std::exp (-(float) i / 300.0f);

        // Fractional onset via linear interpolation of the shape.
        for (int i = 0; i < numSamples; ++i)
        {
            const auto t = (double) i - onsetSample;
            if (t < 0.0 || t >= (double) shape.size() - 1.0)
                continue;
            const auto i0 = (size_t) t;
            const auto frac = (float) (t - (double) i0);
            const auto v = sign * (shape[i0] * (1.0f - frac) + shape[i0 + 1] * frac);
            ir.setSample (0, i, v);
            ir.setSample (1, i, v);
        }
        return ir;
    }

    void testLiveEQMatchesOfflineAcrossBlocks()
    {
        std::array<MasterEQ::BandParams, IRComposerConstants::numMasterBands> bands {};
        bands[0].active = true;
        bands[0].frequencyHz = 1700.0f;
        bands[0].gainDb = 9.0f;
        bands[0].q = 1.8f;
        bands[0].type = FilterType::Bell;
        juce::AudioBuffer<float> offline (2, 4096);
        offline.clear();
        offline.setSample (0, 0, 1.0f);
        offline.setSample (1, 17, -0.5f);
        auto live = offline;
        MasterEQ().processBuffer (offline, sampleRate, bands);
        LiveMasterEQ eq;
        eq.prepare (sampleRate);
        for (int start = 0; start < live.getNumSamples(); start += 113)
        {
            auto block = juce::dsp::AudioBlock<float> (live).getSubBlock (
                (size_t) start, (size_t) juce::jmin (113, live.getNumSamples() - start));
            eq.processBlock (block, bands);
        }
        float maxError = 0.0f;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < live.getNumSamples(); ++i)
                maxError = juce::jmax (maxError, std::abs (live.getSample (ch, i) - offline.getSample (ch, i)));
        check (maxError < 1.0e-5f, "LiveMasterEQ: live block processing matches exported EQ impulse response");
    }

    void testBlendWeightsSumToOneAndFavourNearestCorner()
    {
        BlendWeights::ActiveMask all { true, true, true, true };
        const auto centre = BlendWeights::compute (0.5f, 0.5f, all);
        float total = 0.0f;
        for (auto w : centre) total += w;
        check (closeEnough (total, 1.0f, 1.0e-5f), "BlendWeights: weights sum to 1");
        check (closeEnough (centre[0], 0.25f, 1.0e-4f), "BlendWeights: centre blends 4 active slots equally");

        const auto corner = BlendWeights::compute (0.0f, 0.0f, all);
        check (corner[0] > 0.999f, "BlendWeights: puck on a slot's corner gives that slot ~100%");
    }

    void testBlendWeightsIgnoreInactiveSlots()
    {
        BlendWeights::ActiveMask two { true, true, false, false };
        const auto w = BlendWeights::compute (0.5f, 0.0f, two);
        check (closeEnough (w[0], 0.5f, 1.0e-4f) && closeEnough (w[1], 0.5f, 1.0e-4f) && w[2] == 0.0f && w[3] == 0.0f,
               "BlendWeights: inactive slots get 0 and the active ones share the whole mix");

        BlendWeights::ActiveMask one { false, false, true, false };
        check (closeEnough (BlendWeights::compute (0.0f, 0.0f, one)[2], 1.0f, 1.0e-5f),
               "BlendWeights: a lone active slot always gets 100%");
    }

    void testIRAlignFindsFractionalLag()
    {
        const auto reference = makeDecayingIR (4800, 100.0);
        const auto candidate = makeDecayingIR (4800, 63.4); // early by 36.6 samples
        const auto r = IRAlign::estimate (reference, candidate, 4800, 2400);
        check (r.valid && std::abs (r.lagSamples - 36.6) < 0.25,
               "IRAlign: finds a sub-sample lag (got " + std::to_string (r.lagSamples) + ")");
        check (! r.invertPolarity, "IRAlign: same-polarity IRs are not flagged for inversion");
    }

    void testIRAlignFindsNegativeLagAndInvertedPolarity()
    {
        const auto reference = makeDecayingIR (4800, 50.0);
        const auto candidate = makeDecayingIR (4800, 170.0, -1.0f); // late by 120, inverted
        const auto r = IRAlign::estimate (reference, candidate, 4800, 2400);
        check (r.valid && std::abs (r.lagSamples + 120.0) < 0.25,
               "IRAlign: a late candidate gives a negative lag (got " + std::to_string (r.lagSamples) + ")");
        check (r.invertPolarity, "IRAlign: an inverted candidate is flagged for polarity inversion");
        check (r.confidence > 0.99f, "IRAlign: identical shapes (whole-sample shift) give ~full confidence (got "
                                       + std::to_string (r.confidence) + ")");
    }

    void testIRAlignRejectsSilence()
    {
        juce::AudioBuffer<float> silent (2, 1000);
        silent.clear();
        check (! IRAlign::estimate (makeDecayingIR (1000, 0.0), silent, 1000, 500).valid,
               "IRAlign: a silent IR gives no alignment instead of a bogus one");
    }
}

int main()
{
    testLiveEQMatchesOfflineAcrossBlocks();
    testBlendWeightsSumToOneAndFavourNearestCorner();
    testBlendWeightsIgnoreInactiveSlots();
    testIRAlignFindsFractionalLag();
    testIRAlignFindsNegativeLagAndInvertedPolarity();
    testIRAlignRejectsSilence();
    testBellUnityGainAtZeroDb();
    testBellCutReducesGainAtCenterFrequency();
    testBellBoostIncreasesGainAtCenterFrequency();
    testMasterEQBellAtZeroGainIsUnity();
    testMasterEQInactiveBandIsBypassed();
    testPhaseRotatePreservesMagnitude();
    testPhaseRotateAtZeroDegreesIsPassthrough();
    testIRSlotCropShiftsImpulseToBufferStart();
    testIRSlotCropEndTrimsTail();
    testIRSlotCropEndKeepsContentBeforeIt();
    testIRSlotCropEndCannotGoBeforeStart();
    testIRSlotAutoTrimsLongSilentTailOnLoad();
    testIRSlotAutoTrimRejectsDisconnectedCaptureEvents();
    testIRSlotAutoTrimKeepsFullLengthWhenContentFillsTheBuffer();
    testIRSlotAutoTrimTailCanBeReTriggeredManually();
    testSlopeFilterSteeperSlopeAttenuatesMoreBelowCutoff();
    testSlopeFilterStableAtEverySlope();
    testIRSlotLeadingSilenceTrimPreservesStereoTiming();
    testIRSlotPolarityInvertsSign();
    testIRSlotGainAppliesLinearScale();
    testIRSlotMonoSourceIsDuplicatedToStereo();
    testIRLevelUnitImpulseNeedsNoGain();
    testIRLevelTamesPeakNormalisedResonantIR();

    if (failures == 0)
    {
        std::cout << "All IR Composer DSP tests passed.\n";
        return 0;
    }
    std::cerr << failures << " IR Composer DSP test(s) failed.\n";
    return 1;
}
