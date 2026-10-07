#pragma once

#include <juce_dsp/juce_dsp.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <vector>
#include "MasterEQ.h"
#include "Parameters.h"
#include "SpectrumFifo.h"

class IRComposerAudioProcessor;

// FFT spectrum panel for the post-convolution Master EQ: draws the pre-EQ and post-EQ
// signal as two distinctly-coloured traces, a summed EQ response curve, and one
// draggable node per *active* band (horizontal drag -> frequency, vertical drag ->
// gain, right-click/ctrl-drag -> gain only, scroll -> Q). Adapted from
// parametric-dynamic-eq-VST/Source/SpectrumAnalyzer.h -- same look and interaction
// model, but for MasterEQ's fixed numMasterBands slots (no dynamic add/remove of bands,
// no M/S routing, no dynamic/compressive EQ, no oversampling; see the plan's non-goals).
// Double-clicking a node deactivates that band (standing in for "remove"); double-
// clicking empty graph space activates the first currently-inactive band at that
// frequency (standing in for "add") -- the same gesture as the source EQ, adapted to a
// fixed-slot model instead of a dynamically-sized band list.
class MasterSpectrumAnalyzer final : public juce::Component, private juce::Timer
{
public:
    explicit MasterSpectrumAnalyzer (IRComposerAudioProcessor&);
    ~MasterSpectrumAnalyzer() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    static juce::String formatFrequency (float);

    void setGainRangeDb (float newRangeDb) noexcept;
    float getGainRangeDb() const noexcept { return gainRangeDb; }

private:
    void timerCallback() override;
    juce::Rectangle<float> graphBounds() const;
    float frequencyToX (float) const;
    float xToFrequency (float) const;
    float spectrumDbToY (float) const;
    float gainToY (float) const;
    float yToGain (float) const;
    int bandNodeNear (float x, float y) const;
    int firstInactiveBand() const;
    void updatePath (const std::array<float, SpectrumFifo::fftSize>&, juce::Path&, std::array<float, SpectrumFifo::fftSize / 2>&);
    float bandMagnitudeResponseDb (const MasterEQ::BandParams&, float frequencyHz) const;
    float summedMagnitudeResponseDb (float frequencyHz) const;
    void updateResponseCurve();

    IRComposerAudioProcessor& processor;
    juce::dsp::FFT fft { SpectrumFifo::fftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) SpectrumFifo::fftSize,
                                                  juce::dsp::WindowingFunction<float>::hann };
    std::array<float, SpectrumFifo::fftSize> inputSamples {}, outputSamples {};
    std::array<float, SpectrumFifo::fftSize * 2> fftData {};
    std::array<float, SpectrumFifo::fftSize / 2> inputMagnitudes {}, outputMagnitudes {};
    juce::Path inputPath, outputPath, responseCurve;

    int draggedBand = -1;
    bool draggingGain = false;
    bool verticalOnlyDrag = false;
    bool freqGestureStarted = false;
    bool gainGestureStarted = false;
    float gainRangeDb = 24.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MasterSpectrumAnalyzer)
};
