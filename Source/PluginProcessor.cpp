#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "IRAlign.h"
#include <cmath>

namespace
{
    juce::NormalisableRange<float> frequencyRange()
    {
        return { IRComposerConstants::minFrequencyHz, IRComposerConstants::maxFrequencyHz, 1.0f, 0.3f };
    }

    // Spreads the numMasterBands default centre frequencies evenly on a log scale, so
    // an untouched MasterEQ already looks like a sensible starting EQ curve rather than
    // every band stacked on the same frequency.
    float defaultBandFrequency (int bandIndex)
    {
        const auto t = (bandIndex + 0.5f) / (float) IRComposerConstants::numMasterBands;
        return IRComposerConstants::minFrequencyHz
             * std::pow (IRComposerConstants::maxFrequencyHz / IRComposerConstants::minFrequencyHz, t);
    }
}

IRComposerAudioProcessor::IRComposerAudioProcessor()
    : AudioProcessor (BusesProperties()
                           .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                           .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    for (int s = 0; s < IRComposerConstants::numSlots; ++s)
    {
        slotGainParams[(size_t) s] = apvts.getRawParameterValue (ParamIDs::slotGain (s));
        slotPolarityParams[(size_t) s] = apvts.getRawParameterValue (ParamIDs::slotPolarity (s));
        slotDelayParams[(size_t) s] = apvts.getRawParameterValue (ParamIDs::slotDelay (s));
        slotRotateParams[(size_t) s] = apvts.getRawParameterValue (ParamIDs::slotRotate (s));
        slotMuteParams[(size_t) s] = apvts.getRawParameterValue (ParamIDs::slotMute (s));
        slotSoloParams[(size_t) s] = apvts.getRawParameterValue (ParamIDs::slotSolo (s));

        apvts.addParameterListener (ParamIDs::slotGain (s), this);
        apvts.addParameterListener (ParamIDs::slotPolarity (s), this);
        apvts.addParameterListener (ParamIDs::slotDelay (s), this);
        apvts.addParameterListener (ParamIDs::slotRotate (s), this);
        apvts.addParameterListener (ParamIDs::slotMute (s), this);
        apvts.addParameterListener (ParamIDs::slotSolo (s), this);
    }

    blendXParam = apvts.getRawParameterValue (ParamIDs::blendX);
    blendYParam = apvts.getRawParameterValue (ParamIDs::blendY);

    for (int b = 0; b < IRComposerConstants::numMasterBands; ++b)
    {
        masterBandActiveParams[(size_t) b] = apvts.getRawParameterValue (ParamIDs::masterBandActive (b));
        masterBandFreqParams[(size_t) b] = apvts.getRawParameterValue (ParamIDs::masterBandFreq (b));
        masterBandGainParams[(size_t) b] = apvts.getRawParameterValue (ParamIDs::masterBandGain (b));
        masterBandQParams[(size_t) b] = apvts.getRawParameterValue (ParamIDs::masterBandQ (b));
        masterBandTypeParams[(size_t) b] = apvts.getRawParameterValue (ParamIDs::masterBandType (b));
        masterBandSlopeParams[(size_t) b] = apvts.getRawParameterValue (ParamIDs::masterBandSlope (b));

        apvts.addParameterListener (ParamIDs::masterBandActive (b), this);
        apvts.addParameterListener (ParamIDs::masterBandFreq (b), this);
        apvts.addParameterListener (ParamIDs::masterBandGain (b), this);
        apvts.addParameterListener (ParamIDs::masterBandQ (b), this);
        apvts.addParameterListener (ParamIDs::masterBandType (b), this);
        apvts.addParameterListener (ParamIDs::masterBandSlope (b), this);
    }
}

IRComposerAudioProcessor::~IRComposerAudioProcessor()
{
    fileLoader.shutdown();
    blendEngine.shutdown();
    // Must unregister before any member (blendEngine in particular) starts being torn
    // down -- parameterChanged() below reaches into blendEngine, so a listener firing
    // mid-destruction would touch an already-destroyed object.
    for (int s = 0; s < IRComposerConstants::numSlots; ++s)
    {
        apvts.removeParameterListener (ParamIDs::slotGain (s), this);
        apvts.removeParameterListener (ParamIDs::slotPolarity (s), this);
        apvts.removeParameterListener (ParamIDs::slotDelay (s), this);
        apvts.removeParameterListener (ParamIDs::slotRotate (s), this);
        apvts.removeParameterListener (ParamIDs::slotMute (s), this);
        apvts.removeParameterListener (ParamIDs::slotSolo (s), this);
    }
    for (int b = 0; b < IRComposerConstants::numMasterBands; ++b)
    {
        apvts.removeParameterListener (ParamIDs::masterBandActive (b), this);
        apvts.removeParameterListener (ParamIDs::masterBandFreq (b), this);
        apvts.removeParameterListener (ParamIDs::masterBandGain (b), this);
        apvts.removeParameterListener (ParamIDs::masterBandQ (b), this);
        apvts.removeParameterListener (ParamIDs::masterBandType (b), this);
        apvts.removeParameterListener (ParamIDs::masterBandSlope (b), this);
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout IRComposerAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    for (int s = 0; s < IRComposerConstants::numSlots; ++s)
    {
        const auto suffix = " " + juce::String (s + 1);
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            ParamIDs::slotGain (s), "Slot" + suffix + " Gain",
            juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));
        params.push_back (std::make_unique<juce::AudioParameterBool> (
            ParamIDs::slotPolarity (s), "Slot" + suffix + " Polarity", false));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            ParamIDs::slotDelay (s), "Slot" + suffix + " Delay",
            juce::NormalisableRange<float> (0.0f, IRComposerConstants::maxFineDelayMs, 0.001f), 0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("ms")));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            ParamIDs::slotRotate (s), "Slot" + suffix + " Phase Rotate",
            juce::NormalisableRange<float> (-IRComposerConstants::maxPhaseRotateDegrees,
                                             IRComposerConstants::maxPhaseRotateDegrees, 0.1f), 0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("deg")));
        params.push_back (std::make_unique<juce::AudioParameterBool> (
            ParamIDs::slotMute (s), "Slot" + suffix + " Mute", false));
        params.push_back (std::make_unique<juce::AudioParameterBool> (
            ParamIDs::slotSolo (s), "Slot" + suffix + " Solo", false));
    }

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        ParamIDs::blendX, "Blend X", juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        ParamIDs::blendY, "Blend Y", juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));

    for (int b = 0; b < IRComposerConstants::numMasterBands; ++b)
    {
        const auto suffix = " " + juce::String (b + 1);
        params.push_back (std::make_unique<juce::AudioParameterBool> (
            ParamIDs::masterBandActive (b), "Master Band" + suffix + " Active", false));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            ParamIDs::masterBandFreq (b), "Master Band" + suffix + " Frequency",
            frequencyRange(), defaultBandFrequency (b),
            juce::AudioParameterFloatAttributes().withLabel ("Hz")));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            ParamIDs::masterBandGain (b), "Master Band" + suffix + " Gain",
            juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            ParamIDs::masterBandQ (b), "Master Band" + suffix + " Q",
            juce::NormalisableRange<float> (0.1f, 10.0f, 0.01f, 0.4f), 0.71f));
        params.push_back (std::make_unique<juce::AudioParameterChoice> (
            ParamIDs::masterBandType (b), "Master Band" + suffix + " Type",
            filterTypeNames(), 0));
        params.push_back (std::make_unique<juce::AudioParameterChoice> (
            ParamIDs::masterBandSlope (b), "Master Band" + suffix + " Slope",
            filterSlopeNames(), 1)); // default Slope12, matches SlopeFilter's own default
    }

    return { params.begin(), params.end() };
}

void IRComposerAudioProcessor::parameterChanged (const juce::String& parameterID, float)
{
    // Only delay and phase-rotate are baked into the per-slot kernels BlendEngine
    // builds. Gain/polarity/mute/solo and the blend pad are live gains, and master EQ
    // parameters are read fresh every block by LiveMasterEQ -- none of those need a
    // kernel rebuild (see processBlock()).
    if (parameterID.startsWith ("slotDelay") || parameterID.startsWith ("slotRotate"))
        blendEngine.markDirty();
}

double IRComposerAudioProcessor::getTailLengthSeconds() const
{
    return (double) combineLengthSamples.load() / juce::jmax (1.0, getSampleRate());
}

void IRComposerAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    for (auto& slot : irSlots)
        slot.prepare (sampleRate);

    const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) samplesPerBlock, 2 };
    for (auto& convolution : slotConvolutions)
    {
        // A fresh juce::dsp::Convolution passes audio through dry (its default kernel is
        // a unit impulse), and would crossfade from that dry signal into the first real
        // kernel. Queue a silent kernel first -- prepare() runs any queued load
        // synchronously -- so an engine starts silent instead.
        juce::AudioBuffer<float> silence (2, 1);
        silence.clear();
        convolution.loadImpulseResponse (std::move (silence), sampleRate,
                                          juce::dsp::Convolution::Stereo::yes,
                                          juce::dsp::Convolution::Trim::no,
                                          juce::dsp::Convolution::Normalise::no);
        convolution.prepare (spec);
        convolution.reset();
    }
    slotHasKernel.fill (false);
    slotEngineRunning.fill (false);
    for (auto& gain : slotLiveGains)
    {
        gain.reset (sampleRate, 0.05);
        gain.setCurrentAndTargetValue (0.0f);
    }
    dryBuffer.setSize (2, samplesPerBlock);
    slotBuffer.setSize (2, samplesPerBlock);

    liveMasterEQ.prepare (sampleRate);

    blendEngine.prepare (sampleRate);
}

bool IRComposerAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void IRComposerAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    auto newKernels = blendEngine.tryTakeReadyKernels();
    for (int s = 0; s < IRComposerConstants::numSlots; ++s)
    {
        auto& kernel = newKernels[(size_t) s];
        if (kernel == nullptr)
            continue;

        slotHasKernel[(size_t) s] = kernel->getNumSamples() > 0;
        if (slotHasKernel[(size_t) s])
            slotConvolutions[(size_t) s].loadImpulseResponse (std::move (*kernel), getSampleRate(),
                                                               juce::dsp::Convolution::Stereo::yes,
                                                               juce::dsp::Convolution::Trim::no,
                                                               juce::dsp::Convolution::Normalise::no);
    }

    BlendWeights::ActiveMask active {};
    for (int s = 0; s < IRComposerConstants::numSlots; ++s)
        active[(size_t) s] = slotHasKernel[(size_t) s] && shouldIncludeSlot (s);
    const auto weights = BlendWeights::compute (blendXParam->load(), blendYParam->load(), active);

    const auto numSamples = buffer.getNumSamples();
    // Hosts are allowed to exceed the prepared block size occasionally.
    if (numSamples > dryBuffer.getNumSamples())
    {
        dryBuffer.setSize (2, numSamples, false, false, true);
        slotBuffer.setSize (2, numSamples, false, false, true);
    }
    for (int ch = 0; ch < 2; ++ch)
        dryBuffer.copyFrom (ch, 0, buffer, ch, 0, numSamples);
    buffer.clear();

    for (int s = 0; s < IRComposerConstants::numSlots; ++s)
    {
        auto& gain = slotLiveGains[(size_t) s];
        const auto p = getSlotRenderParams (s);
        gain.setTargetValue (active[(size_t) s]
            ? weights[(size_t) s] * juce::Decibels::decibelsToGain (p.gainDb) * (p.polarityInverted ? -1.0f : 1.0f)
            : 0.0f);

        auto& convolution = slotConvolutions[(size_t) s];
        if (! gain.isSmoothing() && gain.getTargetValue() == 0.0f)
        {
            // Silent slot: skip its convolution entirely. Its history is cleared so that
            // when it comes back it doesn't replay a stale snippet of old input.
            if (slotEngineRunning[(size_t) s])
            {
                convolution.reset();
                slotEngineRunning[(size_t) s] = false;
            }
            continue;
        }
        slotEngineRunning[(size_t) s] = true;

        for (int ch = 0; ch < 2; ++ch)
            slotBuffer.copyFrom (ch, 0, dryBuffer, ch, 0, numSamples);
        {
            auto block = juce::dsp::AudioBlock<float> (slotBuffer).getSubBlock (0, (size_t) numSamples);
            juce::dsp::ProcessContextReplacing<float> context (block);
            convolution.process (context);
        }

        if (gain.isSmoothing())
        {
            auto* outL = buffer.getWritePointer (0);
            auto* outR = buffer.getWritePointer (1);
            const auto* inL = slotBuffer.getReadPointer (0);
            const auto* inR = slotBuffer.getReadPointer (1);
            for (int i = 0; i < numSamples; ++i)
            {
                const auto g = gain.getNextValue();
                outL[i] += inL[i] * g;
                outR[i] += inR[i] * g;
            }
        }
        else
        {
            for (int ch = 0; ch < 2; ++ch)
                buffer.addFrom (ch, 0, slotBuffer, ch, 0, numSamples, gain.getTargetValue());
        }
    }

    // Master EQ lives here, at the very end of the chain, processing the actual
    // convolved host signal in real time -- not baked into the kernel (see the plan's
    // "a teljes lánc végén" requirement, and LiveMasterEQ.h's header comment for why
    // that also makes a live in/out spectrum comparison meaningful). Input is captured
    // pre-EQ (i.e. straight out of the slot convolutions above), output post-EQ.
    for (int i = 0; i < numSamples; ++i)
        inputSpectrumFifo.push (buffer.getSample (0, i));

    juce::dsp::AudioBlock<float> eqBlock (buffer);
    liveMasterEQ.processBlock (eqBlock, getMasterBandParams());

    // Last-resort output protection: a NaN/Inf (e.g. from a corrupt IR file) would
    // otherwise poison the host's whole mix bus, and nothing legitimate needs more
    // than +12dBFS out of an IR loader -- this only ever engages on runaway levels.
    constexpr float safetyCeiling = 4.0f; // +12dBFS
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto* d = buffer.getWritePointer (ch);
        for (int i = 0; i < numSamples; ++i)
            d[i] = std::isfinite (d[i]) ? juce::jlimit (-safetyCeiling, safetyCeiling, d[i]) : 0.0f;
    }

    for (int i = 0; i < numSamples; ++i)
        outputSpectrumFifo.push (buffer.getSample (0, i));
}

juce::AudioProcessorEditor* IRComposerAudioProcessor::createEditor()
{
    return new IRComposerAudioProcessorEditor (*this);
}

IRSlot::RenderParams IRComposerAudioProcessor::getSlotRenderParams (int slot) const noexcept
{
    IRSlot::RenderParams p;
    p.gainDb = slotGainParams[(size_t) slot]->load();
    p.polarityInverted = slotPolarityParams[(size_t) slot]->load() > 0.5f;
    p.delayMs = slotDelayParams[(size_t) slot]->load();
    p.rotateDegrees = slotRotateParams[(size_t) slot]->load();
    return p;
}

bool IRComposerAudioProcessor::shouldIncludeSlot (int slot) const noexcept
{
    if (slotMuteParams[(size_t) slot]->load() > 0.5f)
        return false;

    bool anySolo = false;
    for (int i = 0; i < IRComposerConstants::numSlots; ++i)
    {
        if (slotSoloParams[(size_t) i]->load() > 0.5f)
        {
            anySolo = true;
            break;
        }
    }

    if (anySolo)
        return slotSoloParams[(size_t) slot]->load() > 0.5f;

    return true;
}

std::array<MasterEQ::BandParams, IRComposerConstants::numMasterBands> IRComposerAudioProcessor::getMasterBandParams() const noexcept
{
    std::array<MasterEQ::BandParams, IRComposerConstants::numMasterBands> bands;
    for (int b = 0; b < IRComposerConstants::numMasterBands; ++b)
    {
        auto& band = bands[(size_t) b];
        band.active = masterBandActiveParams[(size_t) b]->load() > 0.5f;
        band.frequencyHz = masterBandFreqParams[(size_t) b]->load();
        band.gainDb = masterBandGainParams[(size_t) b]->load();
        band.q = masterBandQParams[(size_t) b]->load();
        band.type = (FilterType) (int) masterBandTypeParams[(size_t) b]->load();
        band.slope = (FilterSlope) (int) masterBandSlopeParams[(size_t) b]->load();
    }
    return bands;
}

void IRComposerAudioProcessor::recomputeDefaultCombineLength()
{
    juce::int64 longest = 0;
    for (auto& slot : irSlots)
        longest = juce::jmax (longest, juce::jmax ((juce::int64) 0, slot.getCropEndSample() - slot.getCropStartSample()));

    combineLengthSamples.store (longest);
    appState.setProperty (AppStateIDs::combineLengthSamples, longest, nullptr);
}

void IRComposerAudioProcessor::setCombineLengthSamples (juce::int64 newLength)
{
    combineLengthUserSet = true;
    combineLengthSamples.store (juce::jmax ((juce::int64) 0, newLength));
    appState.setProperty (AppStateIDs::combineLengthSamples, (juce::int64) combineLengthSamples.load(), nullptr);
    blendEngine.markDirty();
}

void IRComposerAudioProcessor::setSlotCropStartSample (int slot, juce::int64 newCropStartSample)
{
    irSlots[(size_t) slot].setCropStartSample (newCropStartSample);
    appState.setProperty (AppStateIDs::slotCropStartSample (slot), (juce::int64) irSlots[(size_t) slot].getCropStartSample(), nullptr);

    if (! combineLengthUserSet)
        recomputeDefaultCombineLength();

    blendEngine.markDirty();
}

void IRComposerAudioProcessor::setSlotCropEndSample (int slot, juce::int64 newCropEndSample)
{
    irSlots[(size_t) slot].setCropEndSample (newCropEndSample);
    appState.setProperty (AppStateIDs::slotCropEndSample (slot), (juce::int64) irSlots[(size_t) slot].getCropEndSample(), nullptr);

    if (! combineLengthUserSet)
        recomputeDefaultCombineLength();

    blendEngine.markDirty();
}

void IRComposerAudioProcessor::autoTrimSlotTail (int slot)
{
    irSlots[(size_t) slot].autoTrimTail();
    appState.setProperty (AppStateIDs::slotCropEndSample (slot), (juce::int64) irSlots[(size_t) slot].getCropEndSample(), nullptr);

    if (! combineLengthUserSet)
        recomputeDefaultCombineLength();

    blendEngine.markDirty();
}

void IRComposerAudioProcessor::loadIRFile (int slot, const juce::File& file, juce::int64 initialCropStartSample,
                                            juce::int64 initialCropEndSample, bool allowAutoAlign)
{
    // Loads run on a 2-thread pool, so rapid-fire requests for the same slot (e.g.
    // clicking the next/previous-IR arrows quickly) can finish out of order. Only the
    // most recent request per slot is allowed to land.
    const auto generation = ++slotLoadGeneration[(size_t) slot];
    pendingSlotFilePath[(size_t) slot] = file.getFullPathName();

    fileLoader.loadAsync (file, [this, slot, generation, initialCropStartSample, initialCropEndSample, allowAutoAlign,
                                  path = file.getFullPathName()]
        (bool success, juce::AudioBuffer<float> buffer, double sr)
    {
        if (generation != slotLoadGeneration[(size_t) slot] || ! success)
            return;

        irSlots[(size_t) slot].setRawBuffer (std::move (buffer), sr);
        irSlots[(size_t) slot].prepare (getSampleRate() > 0.0 ? getSampleRate() : 44100.0);
        // Restore the end first: a saved start can lie beyond the auto-trimmed end.
        if (initialCropEndSample >= 0)
            irSlots[(size_t) slot].setCropEndSample (initialCropEndSample);
        if (initialCropStartSample >= 0)
            irSlots[(size_t) slot].setCropStartSample (initialCropStartSample);
        else
            irSlots[(size_t) slot].autoTrimLeadingSilence();

        appState.setProperty (AppStateIDs::slotFilePath (slot), path, nullptr);
        appState.setProperty (AppStateIDs::slotCropStartSample (slot), (juce::int64) irSlots[(size_t) slot].getCropStartSample(), nullptr);
        appState.setProperty (AppStateIDs::slotCropEndSample (slot), (juce::int64) irSlots[(size_t) slot].getCropEndSample(), nullptr);

        if (! combineLengthUserSet)
            recomputeDefaultCombineLength();

        if (allowAutoAlign && isAutoAlignOnLoad())
            alignSlot (slot);

        blendEngine.markDirty();

        if (onSlotLoaded != nullptr)
            onSlotLoaded (slot);
    });
}

void IRComposerAudioProcessor::setParameterFromUI (const juce::String& parameterID, float plainValue)
{
    if (auto* param = apvts.getParameter (parameterID))
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->convertTo0to1 (plainValue));
        param->endChangeGesture();
    }
}

bool IRComposerAudioProcessor::alignSlot (int slot)
{
    auto& candidate = irSlots[(size_t) slot];
    if (! candidate.hasAudio())
        return false;

    int referenceSlot = -1;
    for (int s = 0; s < IRComposerConstants::numSlots && referenceSlot < 0; ++s)
        if (s != slot && irSlots[(size_t) s].hasAudio())
            referenceSlot = s;
    if (referenceSlot < 0)
        return false;

    const auto sr = getSampleRate() > 0.0 ? getSampleRate() : 44100.0;
    const auto maxLag = (int) std::ceil (IRComposerConstants::maxFineDelayMs * 0.001 * sr);
    // Long enough to hold the whole searchable lag range twice over, plus the part of
    // a cab/room IR that actually defines its phase -- the early response.
    const auto window = juce::jmax (2 * maxLag, (int) (0.1 * sr));

    // The reference as it currently sounds (its own delay/polarity/rotate included);
    // the candidate with only its phase-rotate, since delay and polarity are exactly
    // what's being solved for.
    const auto reference = irSlots[(size_t) referenceSlot].renderProcessed (window, getSlotRenderParams (referenceSlot));
    IRSlot::RenderParams candidateParams;
    candidateParams.rotateDegrees = getSlotRenderParams (slot).rotateDegrees;
    const auto result = IRAlign::estimate (reference, candidate.renderProcessed (window, candidateParams), window, maxLag);
    if (! result.valid)
        return false;

    auto lag = result.lagSamples;
    if (lag < 0.0)
    {
        // The candidate is late: Delay can't advance it, so skip that many whole
        // samples off its start instead, leaving only a fractional delay.
        const auto wholeSamples = (juce::int64) std::ceil (-lag);
        setSlotCropStartSample (slot, candidate.getCropStartSample() + wholeSamples);
        lag += (double) wholeSamples;
    }

    setParameterFromUI (ParamIDs::slotDelay (slot),
                        (float) juce::jlimit (0.0, (double) IRComposerConstants::maxFineDelayMs, lag / sr * 1000.0));
    setParameterFromUI (ParamIDs::slotPolarity (slot), result.invertPolarity ? 1.0f : 0.0f);
    blendEngine.markDirty();
    return true;
}

bool IRComposerAudioProcessor::isAutoAlignOnLoad() const
{
    return (bool) appState.getProperty (AppStateIDs::autoAlignOnLoad, true);
}

void IRComposerAudioProcessor::setAutoAlignOnLoad (bool shouldAutoAlign)
{
    appState.setProperty (AppStateIDs::autoAlignOnLoad, shouldAutoAlign, nullptr);
}

BlendWeights::ActiveMask IRComposerAudioProcessor::getBlendActiveMask() const
{
    BlendWeights::ActiveMask active {};
    for (int s = 0; s < IRComposerConstants::numSlots; ++s)
        active[(size_t) s] = irSlots[(size_t) s].hasAudio() && shouldIncludeSlot (s);
    return active;
}

std::array<float, IRComposerConstants::numSlots> IRComposerAudioProcessor::getSlotMixGains() const
{
    const auto active = getBlendActiveMask();
    const auto weights = BlendWeights::compute (blendXParam->load(), blendYParam->load(), active);

    std::array<float, IRComposerConstants::numSlots> mix {};
    for (int s = 0; s < IRComposerConstants::numSlots; ++s)
    {
        if (! active[(size_t) s])
            continue;
        const auto p = getSlotRenderParams (s);
        mix[(size_t) s] = weights[(size_t) s] * juce::Decibels::decibelsToGain (p.gainDb) * (p.polarityInverted ? -1.0f : 1.0f);
    }
    return mix;
}

juce::File IRComposerAudioProcessor::getSlotRequestedFile (int slot) const
{
    auto path = pendingSlotFilePath[(size_t) slot];
    if (path.isEmpty())
        path = appState.getProperty (AppStateIDs::slotFilePath (slot), juce::String()).toString();
    return path.isNotEmpty() ? juce::File (path) : juce::File();
}

void IRComposerAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree root ("IR_COMPOSER_STATE");
    root.appendChild (apvts.copyState(), nullptr);
    root.appendChild (appState.createCopy(), nullptr);

    std::unique_ptr<juce::XmlElement> xml (root.createXml());
    copyXmlToBinary (*xml, destData);
}

void IRComposerAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml == nullptr || xml->getTagName() != "IR_COMPOSER_STATE")
        return;

    const auto root = juce::ValueTree::fromXml (*xml);
    const auto restoredParams = root.getChildWithName ("PARAMETERS");
    const auto restoredAppState = root.getChildWithName (AppStateIDs::root);

    if (restoredParams.isValid())
        apvts.replaceState (restoredParams);

    if (restoredAppState.isValid())
    {
        // Keep existing UI ValueTree handles attached to the current state.
        appState.copyPropertiesAndChildrenFrom (restoredAppState, nullptr);
        combineLengthUserSet = true;
        combineLengthSamples.store ((juce::int64) appState.getProperty (AppStateIDs::combineLengthSamples, (juce::int64) 0));

        for (int s = 0; s < IRComposerConstants::numSlots; ++s)
        {
            // Invalidate pending loads and clear audio absent from the new state,
            // including slots whose saved files have since disappeared.
            ++slotLoadGeneration[(size_t) s];
            pendingSlotFilePath[(size_t) s].clear();
            irSlots[(size_t) s].setRawBuffer (juce::AudioBuffer<float>(), 44100.0);
            const juce::String path = appState.getProperty (AppStateIDs::slotFilePath (s), juce::String());
            if (path.isNotEmpty())
            {
                const juce::int64 cropStart = appState.getProperty (AppStateIDs::slotCropStartSample (s), (juce::int64) 0);
                const juce::int64 cropEnd = appState.getProperty (AppStateIDs::slotCropEndSample (s), (juce::int64) -1);
                loadIRFile (s, juce::File (path), cropStart, cropEnd, false);
            }
        }
    }

    blendEngine.markDirty();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new IRComposerAudioProcessor();
}
