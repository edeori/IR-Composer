#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <vector>
#include "Parameters.h"
#include "WaveformPeakCache.h"

class IRComposerAudioProcessor;

// The big, shared preview: all 4 slots' *processed* (crop + polarity + delay + phase-
// rotate, but deliberately NOT EQ'd -- see MasterEQ.h/the plan) waveforms overlaid in
// distinct colours, all aligned to crop-start = pixel 0, so the user can visually match
// their timing/phase. A draggable end-marker sets the shared combine length. Refreshed
// on a low-rate timer rather than wired to every individual control, since the relevant
// changes (any of the 4 slots' gain/polarity/delay/rotate/crop) come from many different
// UI controls. The timer only polls a cheap key of everything the preview depends on
// and rebuilds when it changed -- rendering all 4 slots (delay line + phase-rotate over
// a possibly seconds-long IR) unconditionally 12x a second on the message thread made
// every slider drag in the editor stutter.
//
// Horizontal zoom/pan (mouse wheel to zoom anchored at the cursor, trackpad horizontal
// swipe to pan, double-click to reset to the full view) lets the user zoom right into
// the very start of the overlay to check the 4 slots' transients line up precisely --
// rebuildPeaks() re-derives the peak cache from just the visible sample window each
// time, so zooming in actually reveals more real detail, not just a stretched view of
// the same low-resolution peaks.
class CombinedWaveformComponent final : public juce::Component, private juce::Timer
{
public:
    explicit CombinedWaveformComponent (IRComposerAudioProcessor& processor);
    ~CombinedWaveformComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    // One colour per slot, shared with BlendPadComponent so a slot reads as the same
    // colour everywhere.
    static const std::array<juce::Colour, IRComposerConstants::numSlots> slotColours;

private:
    void timerCallback() override;
    void rebuildPeaks();
    std::vector<double> makeRenderKey() const;
    juce::int64 getDisplayRangeSamples() const noexcept;
    int sampleToX (juce::int64 sample) const noexcept;
    juce::int64 xToSample (int x) const noexcept;
    void clampVisibleWindow() noexcept;

    IRComposerAudioProcessor& processor;
    std::array<WaveformPeakCache, IRComposerConstants::numSlots> slotPeaks;
    juce::int64 lastCombineLength = 0;
    juce::int64 lastDisplayRange = 0;
    std::vector<double> lastRenderKey;

    // The horizontally zoomed/panned sample window currently on screen -- always a
    // sub-range of [0, lastDisplayRange). Until the user actually zooms/pans (or after
    // a double-click reset), this keeps auto-tracking the full range on every rebuild --
    // *not* just once at "first load", since lastDisplayRange legitimately starts at a
    // placeholder value of 1 (nothing loaded yet) and grows once a real IR loads; a
    // one-shot "was it zero" check would latch onto that placeholder and never update.
    juce::int64 visibleStartSample = 0;
    juce::int64 visibleLengthSamples = 0;
    bool horizontalZoomUserSet = false;

    // Vertical zoom, for material whose interesting shape is still hard to read even
    // after WaveformPeakCache's own peak-normalisation and sqrt companding -- lets the
    // user amplify the display further without touching the audio.
    juce::Slider verticalZoomSlider;
    juce::Label verticalZoomLabel { {}, "Zoom" };
    juce::Label horizontalZoomInfoLabel; // "Hzoom: Nx -- scroll to zoom, drag to pan, dbl-click to reset"
    static constexpr int headerHeight = 22;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CombinedWaveformComponent)
};
