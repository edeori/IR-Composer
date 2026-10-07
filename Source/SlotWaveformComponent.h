#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "WaveformPeakCache.h"

class IRComposerAudioProcessor;

// Per-slot mini waveform preview: shows the slot's *raw* (pre-crop) loaded buffer with
// draggable crop-start AND crop-end markers. The dimmed regions outside the markers are
// what gets discarded -- everything is measured relative to the full loaded file so the
// user can see and adjust exactly where the kept region starts and ends.
class SlotWaveformComponent final : public juce::Component
{
public:
    SlotWaveformComponent (IRComposerAudioProcessor& processor, int slotIndex);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

    // Call after the slot's loaded audio changes (e.g. from
    // IRComposerAudioProcessor::onSlotLoaded) to rebuild the peak cache and repaint.
    void refreshFromProcessor();

private:
    enum class DragTarget { none, start, end };

    void rebuildPeaks();
    void loadFile (const juce::File& file);
    // Loads the previous (-1) / next (+1) audio file in the current IR's folder,
    // wrapping around at either end.
    void stepFile (int direction);
    void updateFileLabel();
    juce::int64 xToSample (int x) const noexcept;
    int sampleToX (juce::int64 sample) const noexcept;

    IRComposerAudioProcessor& processor;
    int slot;

    WaveformPeakCache peakCache;
    juce::int64 rawLengthAtLastBuild = 0;
    DragTarget activeDrag = DragTarget::none;

    juce::TextButton loadButton { "Load..." };
    juce::TextButton autoTrimButton { "Auto" };
    juce::TextButton alignButton { "Align" };
    juce::TextButton prevButton { "<" }, nextButton { ">" };
    juce::Label fileLabel;
    std::unique_ptr<juce::FileChooser> fileChooser;

    static constexpr int waveformTopMargin = 26; // leaves room for the load button row

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SlotWaveformComponent)
};
