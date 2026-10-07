#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <memory>
#include "Parameters.h"

class IRComposerAudioProcessor;

// The single, post-blend EQ editor -- one instance for all numMasterBands bands, not
// per slot (see MasterEQ.h and the plan's "EQ csak az összegzett jelre" decision). Fixed
// band count, so unlike the sibling project's BandEditor there's no add/remove logic --
// just band-select buttons and one shared detail panel that rebuilds its attachments
// whenever the selected band changes.
//
// The selected band lives on the processor (getSelectedMasterBand()/
// setSelectedMasterBand()), not as a private member here, since MasterSpectrumAnalyzer
// also selects bands (by clicking a node) and the two need to stay in sync -- a light
// timer polls for changes made from that other side.
class MasterEQEditor final : public juce::Component, private juce::Timer
{
public:
    explicit MasterEQEditor (IRComposerAudioProcessor& processor);
    ~MasterEQEditor() override;

    void resized() override;

private:
    void timerCallback() override;
    void selectBand (int index);
    void rebuildAttachments();
    void updateBandButtonStates();

    IRComposerAudioProcessor& audioProcessor;
    int selectedBand = 0;

    std::array<std::unique_ptr<juce::TextButton>, IRComposerConstants::numMasterBands> bandButtons;

    juce::ToggleButton activeButton { "Active" };
    juce::Slider freqSlider, gainSlider, qSlider;
    juce::Label freqLabel { {}, "Freq" }, gainLabel { {}, "Gain" }, qLabel { {}, "Q" };
    juce::ComboBox typeBox, slopeBox;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    std::unique_ptr<ButtonAttachment> activeAttachment;
    std::unique_ptr<SliderAttachment> freqAttachment, gainAttachment, qAttachment;
    std::unique_ptr<ComboAttachment> typeAttachment, slopeAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MasterEQEditor)
};
