#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>

// One source IR slot's offline processing chain: crop -> polarity -> gain -> fine
// delay -> phase-rotate. Deliberately does NOT include EQ -- the user clarified EQ
// only ever applies to the blended sum, never per slot (see MasterEQ.h and the plan's
// "EQ csak az összegzett jelre" decision).
//
// Runs entirely offline: renderProcessed() is called from BlendEngine's background
// thread on a static buffer, never touches the audio thread directly. Meanwhile the
// message thread can call setRawBuffer() (file finished loading), prepare() (host
// sample rate changed) or setCropStartSample()/setCropEndSample() (user dragged a crop
// marker) at any time -- genuinely concurrently with a renderProcessed() in flight,
// since neither thread waits for the other. `lock` guards exactly the shared state
// those calls touch; renderProcessed() only holds it for the brief crop-copy step, not
// for the delay/phase-rotate work that follows on its own private output buffer.
class IRSlot
{
public:
    struct RenderParams
    {
        bool polarityInverted = false;
        float delayMs = 0.0f;
        float rotateDegrees = 0.0f;
        float gainDb = 0.0f;
    };

    // Must be called whenever the host sample rate changes; re-resamples the cached
    // original buffer to the new rate without re-reading the source file.
    void prepare (double newHostSampleRate);

    // Takes ownership of newly loaded/synthetic source audio at its own sample rate.
    // Mono is duplicated to stereo; anything beyond 2 channels is trimmed to stereo.
    // Immediately resamples to the current host sample rate and resets crop-start to 0
    // and crop-end to the *auto-detected* tail end (see autoTrimTail()) -- not the raw
    // file's full length, so a long real-world capture (e.g. a deconvolved sweep
    // recording with several seconds of trailing near-silence) doesn't default to
    // exporting/auditioning all of that dead air.
    void setRawBuffer (juce::AudioBuffer<float> sourceBuffer, double sourceSampleRate);

    // Both ends of the kept region: [cropStartSample, cropEndSample). cropEndSample is
    // clamped to the raw buffer length, so callers can pass "a very large number" to
    // mean "the end of the file" without needing to know its exact length.
    void setCropStartSample (juce::int64 newCropStart) noexcept;
    void setCropEndSample (juce::int64 newCropEnd) noexcept;
    juce::int64 getCropStartSample() const noexcept;
    juce::int64 getCropEndSample() const noexcept;

    // Re-runs the same auto-detection setRawBuffer() applies by default, moving
    // crop-end to the initial response's decay, confirmed by 100 ms below thresholdDb
    // relative to the file peak, plus a 40 ms safety margin. Disconnected later events
    // are excluded. Lets the user snap back to a sensible length after having
    // dragged the end marker elsewhere, without needing to reload the file.
    void autoTrimTail (float thresholdDb = -60.0f) noexcept;

    // Fresh-file preprocessing, matching JUCE Convolution::Trim's -80 dBFS onset
    // threshold. Both channels share a crop so stereo timing remains intact.
    // Keep the raw file intact for manual edits and explicit saved crop points.
    void autoTrimLeadingSilence() noexcept;

    bool hasAudio() const noexcept;

    // Bumped whenever the audio a render would start from changes (new file, sample-rate
    // change, crop points) -- lets callers cache work derived from it (BlendEngine's
    // level-match gain, the combined waveform preview) instead of redoing it blindly.
    juce::uint32 getContentVersion() const noexcept { return contentVersion.load(); }
    juce::int64 getRawLengthSamples() const noexcept;

    // Returns a copy (not a reference) of the raw, pre-crop buffer at the current host
    // sample rate, for UI display -- a reference would be unsafe to read once `lock` is
    // released, since setRawBuffer()/prepare() can reassign it from the message thread
    // at any time.
    juce::AudioBuffer<float> getRawBufferAtHostRateCopy() const;

    // Crop (both ends) -> polarity -> gain -> fine delay -> phase-rotate, truncated/
    // zero-padded to exactly targetLengthSamples. Safe to call from a background thread.
    juce::AudioBuffer<float> renderProcessed (juce::int64 targetLengthSamples, const RenderParams& params) const;

private:
    void resampleFromOriginal();

    mutable juce::CriticalSection lock;

    juce::AudioBuffer<float> originalBuffer;
    double originalSampleRate = 44100.0;

    juce::AudioBuffer<float> rawBufferAtHostRate;
    double hostSampleRate = 44100.0;

    juce::int64 cropStartSample = 0;
    juce::int64 cropEndSample = 0; // 0 is a sentinel meaning "full length" until a real buffer loads

    std::atomic<juce::uint32> contentVersion { 0 };
};
