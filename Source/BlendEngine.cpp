#include "BlendEngine.h"
#include "IRLevel.h"
#include "PluginProcessor.h"

BlendEngine::BlendEngine (IRComposerAudioProcessor& ownerProcessor)
    : juce::Thread ("IR Composer Blend Engine"), processor (ownerProcessor)
{
    startThread (juce::Thread::Priority::normal);
}

BlendEngine::~BlendEngine()
{
    shutdown();
}

void BlendEngine::shutdown()
{
    signalThreadShouldExit();
    wakeEvent.signal();
    stopThread (-1);
}

void BlendEngine::prepare (double sampleRateIn) noexcept
{
    sampleRate.store (sampleRateIn);
    forgetSentKernels.store (true);
    markDirty();
}

void BlendEngine::markDirty() noexcept
{
    dirty.store (true, std::memory_order_relaxed);
    wakeEvent.signal();
}

BlendEngine::KernelSet BlendEngine::tryTakeReadyKernels()
{
    KernelSet taken;
    const juce::SpinLock::ScopedTryLockType tryLock (handoffLock);
    if (tryLock.isLocked())
        for (size_t s = 0; s < taken.size(); ++s)
            taken[s] = std::move (pendingKernels[s]);
    return taken;
}

void BlendEngine::run()
{
    while (! threadShouldExit())
    {
        wakeEvent.wait (-1);
        if (threadShouldExit())
            return;

        // Self-throttling rebuild loop: as long as something marked us dirty again
        // while we were rendering, go again immediately rather than waiting for the
        // next wakeEvent signal -- see the plan's "Azonnaliság" section.
        while (dirty.exchange (false, std::memory_order_relaxed))
        {
            if (threadShouldExit())
                return;
            rebuildChangedKernels();
        }
    }
}

void BlendEngine::rebuildChangedKernels()
{
    if (forgetSentKernels.exchange (false))
        for (auto& key : sentKernelKeys)
            key.clear();

    const auto targetLength = processor.getCombineLengthSamples();
    const auto sr = sampleRate.load();

    for (int s = 0; s < IRComposerConstants::numSlots; ++s)
    {
        const auto& slot = processor.getIRSlot (s);
        const auto hasAudio = slot.hasAudio() && targetLength > 0;
        const auto params = processor.getSlotRenderParams (s);

        const std::vector<double> key = hasAudio
            ? std::vector<double> { 1.0, (double) slot.getContentVersion(), (double) targetLength, sr,
                                    (double) params.delayMs, (double) params.rotateDegrees }
            : std::vector<double> { 0.0 };
        if (key == sentKernelKeys[(size_t) s])
            continue;

        auto kernel = std::make_unique<juce::AudioBuffer<float>> (2, 0);
        if (hasAudio)
        {
            // Gain and polarity are deliberately left neutral here -- they're applied
            // live by the processor, see this class's header comment.
            IRSlot::RenderParams kernelParams;
            kernelParams.delayMs = params.delayMs;
            kernelParams.rotateDegrees = params.rotateDegrees;
            *kernel = slot.renderProcessed (targetLength, kernelParams);
            kernel->applyGain (getLevelMatchGain (s, slot, targetLength));

            // Trailing zero-padding (a slot shorter than the combine length) costs
            // convolution CPU for nothing -- drop it. Leading samples are never touched:
            // they carry the slot's alignment.
            auto lastNonZero = 0;
            for (int ch = 0; ch < kernel->getNumChannels(); ++ch)
            {
                const auto* d = kernel->getReadPointer (ch);
                for (int i = kernel->getNumSamples(); --i > lastNonZero;)
                    if (std::abs (d[i]) > 1.0e-9f) { lastNonZero = i; break; }
            }
            kernel->setSize (2, lastNonZero + 1, true, false, true);
        }

        sentKernelKeys[(size_t) s] = key;

        const juce::SpinLock::ScopedLockType sl (handoffLock);
        pendingKernels[(size_t) s] = std::move (kernel);
    }
}

float BlendEngine::getLevelMatchGain (int s, const IRSlot& slot, juce::int64 targetLength)
{
    // Level-match each slot's kernel on its own (see IRLevel.h): its band-average
    // magnitude response is brought to 0dB, so the plugin roughly preserves the input's
    // perceived level whatever IR is loaded -- the old unit-PEAK normalisation made a
    // typical cab IR add +20dB or more, which blew up hot (amp-sim) input signals.
    //
    // Per slot rather than on the blended sum: the blend pad's weights always sum to 1,
    // so with every slot at 0dB the blend stays at 0dB wherever the puck goes -- moving
    // it changes the ratio, not the loudness. Gain/polarity/delay/rotate don't enter
    // into it (delay and phase-rotate don't change the magnitude response anyway), so
    // the Gain faders stay real volume controls. Export keeps its own, independent
    // peak-"normalize" toggle.
    const auto sr = sampleRate.load();
    const std::vector<double> key { (double) slot.getContentVersion(), (double) targetLength, sr };
    if (key != levelMatchKeys[(size_t) s])
    {
        levelMatchGains[(size_t) s] = IRLevel::computeNormalisationGain (slot.renderProcessed (targetLength, {}), sr);
        levelMatchKeys[(size_t) s] = key;
    }
    return levelMatchGains[(size_t) s];
}
