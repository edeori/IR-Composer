#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <memory>
#include <vector>
#include "IRSlot.h"
#include "Parameters.h"

class IRComposerAudioProcessor;

// Renders one convolution kernel per slot on its own background thread and hands them
// to the audio thread through a non-blocking, SpinLock-guarded pointer swap.
//
// Each slot has its own live convolution engine (see PluginProcessor), so a kernel only
// carries what genuinely needs a re-render: crop, fine delay and phase-rotate, plus a
// per-slot level-match gain. Gain, polarity, mute/solo and the blend pad weights are
// all applied live, sample-smoothed, on the audio thread -- moving them never rebuilds
// anything, which is what keeps the blend pad and the Gain faders glitch-free.
//
// markDirty() does NOT wait out a fixed debounce window -- a rebuild starts the instant
// nothing is already running, and self-throttles (starts again immediately on
// completion) if another change arrived meanwhile. Only slots whose kernel inputs
// actually changed are re-rendered and re-sent.
class BlendEngine final : private juce::Thread
{
public:
    explicit BlendEngine (IRComposerAudioProcessor& ownerProcessor);
    ~BlendEngine() override;

    // Forgets every previously-sent kernel, so the next rebuild re-sends all of them --
    // the processor's engines are re-prepared (and emptied) alongside this.
    void prepare (double sampleRateIn) noexcept;
    void shutdown();

    // Call from any thread whenever a crop point, delay/rotate parameter, the combine
    // length, or a newly-loaded IR changes. Real-time safe: just an atomic store and an
    // event signal, no allocation.
    void markDirty() noexcept;

    // Non-blocking. Called once per processBlock. Entries are nullptr for slots with no
    // new kernel since the last call; a 0-sample buffer means "this slot is now empty".
    using KernelSet = std::array<std::unique_ptr<juce::AudioBuffer<float>>, IRComposerConstants::numSlots>;
    KernelSet tryTakeReadyKernels();

private:
    void run() override;
    void rebuildChangedKernels();
    float getLevelMatchGain (int slot, const IRSlot& irSlot, juce::int64 targetLength);

    IRComposerAudioProcessor& processor;
    std::atomic<double> sampleRate { 44100.0 };

    std::atomic<bool> dirty { false };
    std::atomic<bool> forgetSentKernels { false };
    juce::WaitableEvent wakeEvent;

    // Blend-thread only: what each slot's last-sent kernel was built from, and each
    // slot's cached level-match gain (which depends only on its audio/crop and the
    // combine length, so a delay/rotate move doesn't redo the FFT).
    std::array<std::vector<double>, IRComposerConstants::numSlots> sentKernelKeys;
    std::array<std::vector<double>, IRComposerConstants::numSlots> levelMatchKeys;
    std::array<float, IRComposerConstants::numSlots> levelMatchGains {};

    juce::SpinLock handoffLock;
    KernelSet pendingKernels;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BlendEngine)
};
