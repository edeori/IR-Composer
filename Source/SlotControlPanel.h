#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include "SlotWaveformComponent.h"

class IRComposerAudioProcessor;

// One source-IR slot's full control strip: gain/delay/phase-rotate sliders, polarity/
// mute/solo toggles, and the embedded waveform+crop preview. No EQ here -- EQ is only
// ever on the blended sum, see MasterEQEditor.
class SlotControlPanel final : public juce::Component
{
public:
    SlotControlPanel (IRComposerAudioProcessor& processor, int slotIndex);

    void resized() override;

    // Lets the owning PluginEditor forward IRComposerAudioProcessor::onSlotLoaded
    // notifications to this slot's waveform preview so it repaints once loading finishes.
    SlotWaveformComponent& getWaveformComponent() noexcept { return waveform; }

private:
    int slot;
    SlotWaveformComponent waveform;

    juce::Slider gainSlider, delaySlider, rotateSlider;
    juce::Label slotLabel, gainLabel { {}, "Gain" }, delayLabel { {}, "Delay" }, rotateLabel { {}, "Rotate" };
    juce::ToggleButton polarityButton { "Invert" }, muteButton { "Mute" }, soloButton { "Solo" };

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<SliderAttachment> gainAttachment, delayAttachment, rotateAttachment;
    std::unique_ptr<ButtonAttachment> polarityAttachment, muteAttachment, soloAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SlotControlPanel)
};
