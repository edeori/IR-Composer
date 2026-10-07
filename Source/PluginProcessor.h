#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>

#include "AppState.h"
#include "BlendEngine.h"
#include "BlendWeights.h"
#include "IRFileLoader.h"
#include "IRSlot.h"
#include "LiveMasterEQ.h"
#include "MasterEQ.h"
#include "Parameters.h"
#include "SpectrumFifo.h"

class IRComposerAudioProcessor final : public juce::AudioProcessor,
                                        private juce::AudioProcessorValueTreeState::Listener
{
public:
    IRComposerAudioProcessor();
    ~IRComposerAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts { *this, nullptr, "PARAMETERS", createParameterLayout() };

    // Kicks off a background load (see IRFileLoader); on success replaces the slot's
    // audio, applies initialCropStartSample/initialCropEndSample (-1 / -1 for a
    // fresh load: detect leading silence and the tail; explicit values restore
    // saved crop points), records the path, and triggers a BlendEngine rebuild. A
    // failed/missing file just leaves the slot empty -- see the plan's "hiányzó/
    // elmozgatott fájlnál a slot üresen marad" note.
    // With allowAutoAlign (a fresh user load) and auto-align-on-load switched on, the
    // new IR is also aligned to the other loaded slots -- see alignSlot(). Session
    // restore passes false: the saved delay/polarity/crop already are the alignment.
    void loadIRFile (int slot, const juce::File& file, juce::int64 initialCropStartSample = -1,
                      juce::int64 initialCropEndSample = -1, bool allowAutoAlign = true);

    // Time- and polarity-aligns this slot to a reference slot (the lowest-numbered
    // *other* slot with audio loaded) by cross-correlating their starts (IRAlign.h),
    // then writes the result into this slot's Delay and Polarity parameters -- or, if
    // this IR is late relative to the reference, moves its crop start earlier into the
    // file by the whole-sample part instead (Delay can only delay). Message thread only.
    // Returns false if there's nothing to align against.
    bool alignSlot (int slot);
    bool isAutoAlignOnLoad() const;
    void setAutoAlignOnLoad (bool shouldAutoAlign);

    // Which slots take part in the blend (loaded, not muted, not soloed-out), and each
    // one's final linear mix gain: blend-pad weight x Gain x polarity sign (0 for
    // slots not taking part). Message/background threads only -- the audio thread
    // derives the same values from its own state. Used by the export and the pad UI.
    BlendWeights::ActiveMask getBlendActiveMask() const;
    std::array<float, IRComposerConstants::numSlots> getSlotMixGains() const;
    // The file most recently requested for this slot (may still be loading), or the
    // restored/loaded one -- empty if the slot never had a file. Message thread only.
    // Used by the next/previous-IR stepper so rapid clicks step from the latest request.
    juce::File getSlotRequestedFile (int slot) const;

    void setSlotCropStartSample (int slot, juce::int64 newCropStartSample);
    void setSlotCropEndSample (int slot, juce::int64 newCropEndSample);
    void autoTrimSlotTail (int slot);
    void setCombineLengthSamples (juce::int64 newLength);
    juce::int64 getCombineLengthSamples() const noexcept { return combineLengthSamples.load(); }

    // Export settings (format/bit-depth/sample-rate/normalize/fade-out) live here as
    // plain ValueTree properties, not APVTS parameters, since they aren't meaningfully
    // automatable -- see AppState.h and the plan's "Paraméter/state modell" section.
    // ExportPanel binds its controls directly to this tree.
    juce::ValueTree& getAppState() noexcept { return appState; }

    // BlendEngine's read-side interface -- called only from BlendEngine's own
    // background thread (buildMasterBuffer()), never from the audio thread.
    const IRSlot& getIRSlot (int slot) const noexcept { return irSlots[(size_t) slot]; }
    IRSlot::RenderParams getSlotRenderParams (int slot) const noexcept;
    bool shouldIncludeSlot (int slot) const noexcept;
    std::array<MasterEQ::BandParams, IRComposerConstants::numMasterBands> getMasterBandParams() const noexcept;

    // The Master EQ now lives at the end of the live signal chain (post-convolution),
    // processing the actual host audio in real time -- not baked into the convolution
    // kernel any more (that would make an in/out spectrum comparison meaningless, since
    // there'd be no live "before" signal to compare against). ExportEngine still bakes
    // the same curve into the exported IR offline, independently, via MasterEQ.
    SpectrumFifo& getInputSpectrumFifo() noexcept { return inputSpectrumFifo; }
    SpectrumFifo& getOutputSpectrumFifo() noexcept { return outputSpectrumFifo; }

    // Shared between MasterEQEditor's band-select buttons and the spectrum analyzer's
    // draggable nodes, so clicking/dragging a node in either place keeps both in sync.
    int getSelectedMasterBand() const noexcept { return selectedMasterBand.load(); }
    void setSelectedMasterBand (int band) noexcept { selectedMasterBand.store (band); }

    // Notified (message thread only) whenever a slot finishes loading -- lets the
    // editor repaint its waveform/label without polling.
    std::function<void (int slot)> onSlotLoaded;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void recomputeDefaultCombineLength();

    std::array<IRSlot, IRComposerConstants::numSlots> irSlots;
    // Message-thread only -- see loadIRFile().
    std::array<std::atomic<int>, IRComposerConstants::numSlots> slotLoadGeneration {};
    std::array<juce::String, IRComposerConstants::numSlots> pendingSlotFilePath;
    IRFileLoader fileLoader;
    BlendEngine blendEngine { *this };

    juce::ValueTree appState { AppStateIDs::root };
    std::atomic<juce::int64> combineLengthSamples { 0 };
    bool combineLengthUserSet = false;

    std::array<std::atomic<float>*, IRComposerConstants::numSlots>
        slotGainParams {}, slotPolarityParams {}, slotDelayParams {},
        slotRotateParams {}, slotMuteParams {}, slotSoloParams {};

    std::array<std::atomic<float>*, IRComposerConstants::numMasterBands>
        masterBandActiveParams {}, masterBandFreqParams {}, masterBandGainParams {},
        masterBandQParams {}, masterBandTypeParams {}, masterBandSlopeParams {};

    // Live convolution audition: one engine per slot, summed with per-slot live gains
    // (blend weight x Gain x polarity x mute/solo), each smoothed per sample. Linear
    // convolution means sum(g_i * (x * h_i)) == x * sum(g_i * h_i), so this sounds the
    // same as one blended kernel -- but moving the blend pad, a Gain fader, Polarity or
    // Mute/Solo is a smooth gain ramp instead of a kernel rebuild + engine swap. Only
    // crop/delay/rotate/new-file changes rebuild that one slot's kernel (BlendEngine),
    // which juce::dsp::Convolution then crossfades in internally (~50ms), always
    // installing the most recently queued kernel. (An earlier outer A/B engine pair
    // crossfaded into an engine still playing an older, stale kernel -- the async load
    // hadn't landed yet -- so previous IR states re-appeared as echo-like artefacts.)
    std::array<juce::dsp::Convolution, IRComposerConstants::numSlots> slotConvolutions;
    std::array<bool, IRComposerConstants::numSlots> slotHasKernel {}, slotEngineRunning {};
    std::array<juce::SmoothedValue<float>, IRComposerConstants::numSlots> slotLiveGains;
    juce::AudioBuffer<float> dryBuffer, slotBuffer;
    std::atomic<float>* blendXParam = nullptr;
    std::atomic<float>* blendYParam = nullptr;

    void setParameterFromUI (const juce::String& parameterID, float plainValue);

    // Post-convolution, real-time Master EQ + its input/output spectrum feeds -- see
    // getInputSpectrumFifo()/getOutputSpectrumFifo() above.
    LiveMasterEQ liveMasterEQ;
    SpectrumFifo inputSpectrumFifo, outputSpectrumFifo;
    std::atomic<int> selectedMasterBand { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IRComposerAudioProcessor)
};
