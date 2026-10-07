#include "IRSlot.h"
#include "Parameters.h"
#include "PhaseRotateFilter.h"
#include <juce_dsp/juce_dsp.h>
#include <cmath>

namespace
{
    // Follow the initial response until it has stayed below the relative threshold
    // for 100 ms. Scanning for the LAST loud window in the entire file also included
    // disconnected sweep/capture artefacts seconds later, turning them into echoes
    // during convolution. Continuous long decays are still kept, and leading silence
    // does not start the quiet-period counter. Raw audio remains available for manual
    // crop overrides and explicitly saved crops.
    juce::int64 findTailEndSample (const juce::AudioBuffer<float>& buffer, double sampleRate,
                                   float thresholdDb = -60.0f) noexcept
    {
        const auto numSamples = buffer.getNumSamples();
        if (numSamples <= 0)
            return 0;

        float peak = 0.0f;
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            peak = juce::jmax (peak, buffer.getMagnitude (ch, 0, numSamples));
        if (peak < 1.0e-8f)
            return numSamples; // effectively silent throughout -- nothing meaningful to trim

        const auto threshold = peak * juce::Decibels::decibelsToGain (thresholdDb);
        const auto windowSamples = juce::jmax (1, (int) std::lround (sampleRate * 0.01));
        const auto quietSamples = (juce::int64) std::ceil (sampleRate * 0.1);

        juce::int64 lastAboveThreshold = 0;
        for (int start = 0; start < numSamples; start += windowSamples)
        {
            const auto count = juce::jmin (windowSamples, numSamples - start);
            float windowPeak = 0.0f;
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                windowPeak = juce::jmax (windowPeak, buffer.getMagnitude (ch, start, count));
            if (windowPeak >= threshold)
                lastAboveThreshold = start + count;
            else if (lastAboveThreshold > 0 && start + count - lastAboveThreshold >= quietSamples)
                break;
        }

        const auto marginSamples = (juce::int64) std::ceil (sampleRate * 0.04);
        return juce::jmin ((juce::int64) numSamples, lastAboveThreshold + marginSamples);
    }
}

void IRSlot::prepare (double newHostSampleRate)
{
    const juce::ScopedLock sl (lock);
    if (! juce::approximatelyEqual (hostSampleRate, newHostSampleRate))
    {
        const auto oldLength = (juce::int64) rawBufferAtHostRate.getNumSamples();
        hostSampleRate = newHostSampleRate;
        resampleFromOriginal();
        const auto newLength = (juce::int64) rawBufferAtHostRate.getNumSamples();

        // Crop points are stored in samples at the *current* host rate -- rescale them
        // proportionally so a sample-rate change (which changes rawBufferAtHostRate's
        // length) doesn't silently move where the user's crop markers point to.
        if (oldLength > 0 && newLength != oldLength)
        {
            const auto scale = (double) newLength / (double) oldLength;
            cropStartSample = juce::jlimit ((juce::int64) 0, newLength, (juce::int64) std::llround ((double) cropStartSample * scale));
            cropEndSample = juce::jlimit (cropStartSample, newLength, (juce::int64) std::llround ((double) cropEndSample * scale));
        }
        ++contentVersion;
    }
}

void IRSlot::setRawBuffer (juce::AudioBuffer<float> sourceBuffer, double sourceSampleRate)
{
    juce::AudioBuffer<float> stereoBuffer;
    if (sourceBuffer.getNumChannels() == 1)
    {
        stereoBuffer.setSize (2, sourceBuffer.getNumSamples());
        stereoBuffer.copyFrom (0, 0, sourceBuffer, 0, 0, sourceBuffer.getNumSamples());
        stereoBuffer.copyFrom (1, 0, sourceBuffer, 0, 0, sourceBuffer.getNumSamples());
    }
    else
    {
        stereoBuffer = std::move (sourceBuffer);
        if (stereoBuffer.getNumChannels() > 2)
            stereoBuffer.setSize (2, stereoBuffer.getNumSamples(), true, false, true);
    }

    const juce::ScopedLock sl (lock);
    originalBuffer = std::move (stereoBuffer);
    originalSampleRate = sourceSampleRate;
    cropStartSample = 0;
    resampleFromOriginal();
    // Default to the auto-detected tail end, not the full raw length -- see
    // findTailEndSample()'s comment. The user can still drag the end marker back out
    // to the full length (or call autoTrimTail() again later) if they want more.
    cropEndSample = findTailEndSample (rawBufferAtHostRate, hostSampleRate);
    ++contentVersion;
}

void IRSlot::autoTrimLeadingSilence() noexcept
{
    const juce::ScopedLock sl (lock);
    const auto threshold = juce::Decibels::decibelsToGain (-80.0f);
    for (int sample = 0; sample < rawBufferAtHostRate.getNumSamples(); ++sample)
        for (int channel = 0; channel < rawBufferAtHostRate.getNumChannels(); ++channel)
            if (std::abs (rawBufferAtHostRate.getSample (channel, sample)) >= threshold)
            {
                cropStartSample = juce::jmin ((juce::int64) sample, cropEndSample);
                ++contentVersion;
                return;
            }
    // No detectable onset: preserve silent/very quiet files instead of cutting
    // away their entire contents.
}

void IRSlot::autoTrimTail (float thresholdDb) noexcept
{
    const juce::ScopedLock sl (lock);
    cropEndSample = juce::jmax (cropStartSample, findTailEndSample (rawBufferAtHostRate, hostSampleRate, thresholdDb));
    ++contentVersion;
}

void IRSlot::setCropStartSample (juce::int64 newCropStart) noexcept
{
    const juce::ScopedLock sl (lock);
    cropStartSample = juce::jlimit ((juce::int64) 0, cropEndSample, newCropStart);
    ++contentVersion;
}

void IRSlot::setCropEndSample (juce::int64 newCropEnd) noexcept
{
    const juce::ScopedLock sl (lock);
    const auto rawLength = (juce::int64) rawBufferAtHostRate.getNumSamples();
    cropEndSample = juce::jlimit (cropStartSample, rawLength, newCropEnd);
    ++contentVersion;
}

juce::int64 IRSlot::getCropStartSample() const noexcept
{
    const juce::ScopedLock sl (lock);
    return cropStartSample;
}

juce::int64 IRSlot::getCropEndSample() const noexcept
{
    const juce::ScopedLock sl (lock);
    return cropEndSample;
}

bool IRSlot::hasAudio() const noexcept
{
    const juce::ScopedLock sl (lock);
    return rawBufferAtHostRate.getNumSamples() > 0;
}

juce::int64 IRSlot::getRawLengthSamples() const noexcept
{
    const juce::ScopedLock sl (lock);
    return rawBufferAtHostRate.getNumSamples();
}

juce::AudioBuffer<float> IRSlot::getRawBufferAtHostRateCopy() const
{
    const juce::ScopedLock sl (lock);
    return rawBufferAtHostRate;
}

// Precondition: called with `lock` already held.
void IRSlot::resampleFromOriginal()
{
    if (originalBuffer.getNumSamples() == 0)
    {
        rawBufferAtHostRate.setSize (2, 0);
        return;
    }

    if (juce::approximatelyEqual (originalSampleRate, hostSampleRate))
    {
        rawBufferAtHostRate = originalBuffer;
        return;
    }

    const auto ratio = originalSampleRate / hostSampleRate;
    const auto numOutSamples = (int) std::ceil ((double) originalBuffer.getNumSamples() / ratio);
    rawBufferAtHostRate.setSize (originalBuffer.getNumChannels(), juce::jmax (0, numOutSamples));

    for (int ch = 0; ch < originalBuffer.getNumChannels(); ++ch)
    {
        juce::LagrangeInterpolator interpolator;
        interpolator.reset();
        interpolator.process (ratio, originalBuffer.getReadPointer (ch),
                               rawBufferAtHostRate.getWritePointer (ch), numOutSamples, originalBuffer.getNumSamples(), 0);
    }
}

juce::AudioBuffer<float> IRSlot::renderProcessed (juce::int64 targetLengthSamples, const RenderParams& params) const
{
    juce::AudioBuffer<float> output (2, (int) juce::jmax ((juce::int64) 0, targetLengthSamples));
    output.clear();

    if (targetLengthSamples <= 0)
        return output;

    double sampleRateForDelay;
    {
        const juce::ScopedLock sl (lock);

        if (rawBufferAtHostRate.getNumSamples() == 0)
            return output;

        sampleRateForDelay = hostSampleRate;

        const auto croppedRegionEnd = juce::jmin (cropEndSample, (juce::int64) rawBufferAtHostRate.getNumSamples());
        const auto availableFromCrop = juce::jmax ((juce::int64) 0, croppedRegionEnd - cropStartSample);
        const auto samplesToCopy = (int) juce::jmin (availableFromCrop, targetLengthSamples);
        const auto gainLinear = juce::Decibels::decibelsToGain (params.gainDb)
                               * (params.polarityInverted ? -1.0f : 1.0f);
        const auto numSourceChannels = rawBufferAtHostRate.getNumChannels();

        for (int ch = 0; ch < 2; ++ch)
        {
            const auto sourceChannel = juce::jmin (ch, numSourceChannels - 1);
            const auto* src = rawBufferAtHostRate.getReadPointer (sourceChannel, (int) cropStartSample);
            auto* dst = output.getWritePointer (ch);
            for (int i = 0; i < samplesToCopy; ++i)
                dst[i] = src[i] * gainLinear;
        }
    }
    // `lock` released here -- everything below only touches `output`, which is private
    // to this call, so it's safe to keep working on it unlocked.

    if (params.delayMs > 0.0f)
    {
        const auto maxDelaySamples = (int) std::ceil (IRComposerConstants::maxFineDelayMs * 0.001 * sampleRateForDelay) + 4;

        juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd> delayL, delayR;
        delayL.setMaximumDelayInSamples (maxDelaySamples);
        delayR.setMaximumDelayInSamples (maxDelaySamples);
        const juce::dsp::ProcessSpec spec { sampleRateForDelay, (juce::uint32) targetLengthSamples, 1 };
        delayL.prepare (spec);
        delayR.prepare (spec);

        const auto delaySamples = juce::jlimit (0.0f, (float) maxDelaySamples,
            params.delayMs * 0.001f * (float) sampleRateForDelay);
        delayL.setDelay (delaySamples);
        delayR.setDelay (delaySamples);

        auto* left = output.getWritePointer (0);
        auto* right = output.getWritePointer (1);
        for (int i = 0; i < (int) targetLengthSamples; ++i)
        {
            delayL.pushSample (0, left[i]);
            left[i] = delayL.popSample (0);
            delayR.pushSample (0, right[i]);
            right[i] = delayR.popSample (0);
        }
    }

    if (params.rotateDegrees != 0.0f)
    {
        PhaseRotateFilter rotator;
        rotator.processBuffer (output, params.rotateDegrees);
    }

    return output;
}
