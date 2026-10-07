#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include "ExportEngine.h"

class IRComposerAudioProcessor;

// Length/format/bit-depth/sample-rate/normalize/fade-out controls plus the "Export..."
// button. Bound to appState (plain ValueTree properties, not APVTS parameters, since
// none of this is meaningfully automatable) via manual listeners rather than
// AudioProcessorValueTreeState attachments. Uses the shared combine length the user
// already set on CombinedWaveformComponent -- no separate, second length control.
class ExportPanel final : public juce::Component
{
public:
    explicit ExportPanel (IRComposerAudioProcessor& processor);

private:
    void resized() override;
    ExportSettings readSettingsFromControls() const;
    void loadInitialValuesFromAppState();
    void updateBitDepthOptions();
    void startExport();

    IRComposerAudioProcessor& audioProcessor;
    ExportEngine exportEngine;

    juce::ComboBox formatBox, bitDepthBox, sampleRateBox;
    juce::ToggleButton normalizeButton { "Normalize" };
    juce::Slider fadeOutSlider;
    juce::Label fadeOutLabel { {}, "Fade out (ms)" };
    juce::TextButton exportButton { "Export..." };
    juce::Label statusLabel;
    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ExportPanel)
};
