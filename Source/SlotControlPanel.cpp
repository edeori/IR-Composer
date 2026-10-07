#include "SlotControlPanel.h"
#include "PluginProcessor.h"

SlotControlPanel::SlotControlPanel (IRComposerAudioProcessor& processor, int slotIndex)
    : slot (slotIndex), waveform (processor, slotIndex)
{
    slotLabel.setText ("Slot " + juce::String (slot + 1), juce::dontSendNotification);
    slotLabel.setFont (juce::Font (juce::FontOptions (16.0f, juce::Font::bold)));
    addAndMakeVisible (slotLabel);

    addAndMakeVisible (waveform);

    for (auto* s : { &gainSlider, &delaySlider, &rotateSlider })
    {
        s->setSliderStyle (juce::Slider::LinearHorizontal);
        s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 55, 20);
        addAndMakeVisible (s);
    }
    for (auto* l : { &gainLabel, &delayLabel, &rotateLabel })
        addAndMakeVisible (l);
    for (auto* b : { &polarityButton, &muteButton, &soloButton })
        addAndMakeVisible (b);

    auto& apvts = processor.apvts;
    gainAttachment = std::make_unique<SliderAttachment> (apvts, ParamIDs::slotGain (slot), gainSlider);
    delayAttachment = std::make_unique<SliderAttachment> (apvts, ParamIDs::slotDelay (slot), delaySlider);
    rotateAttachment = std::make_unique<SliderAttachment> (apvts, ParamIDs::slotRotate (slot), rotateSlider);
    polarityAttachment = std::make_unique<ButtonAttachment> (apvts, ParamIDs::slotPolarity (slot), polarityButton);
    muteAttachment = std::make_unique<ButtonAttachment> (apvts, ParamIDs::slotMute (slot), muteButton);
    soloAttachment = std::make_unique<ButtonAttachment> (apvts, ParamIDs::slotSolo (slot), soloButton);
}

void SlotControlPanel::resized()
{
    auto area = getLocalBounds().reduced (4);

    slotLabel.setBounds (area.removeFromTop (22));
    area.removeFromTop (2);

    waveform.setBounds (area.removeFromTop (80));
    area.removeFromTop (4);

    auto layoutRow = [&area] (juce::Component& label, juce::Component& control)
    {
        auto row = area.removeFromTop (22);
        label.setBounds (row.removeFromLeft (48));
        control.setBounds (row);
        area.removeFromTop (2);
    };
    layoutRow (gainLabel, gainSlider);
    layoutRow (delayLabel, delaySlider);
    layoutRow (rotateLabel, rotateSlider);

    auto toggleRow = area.removeFromTop (22);
    const auto toggleWidth = toggleRow.getWidth() / 3;
    polarityButton.setBounds (toggleRow.removeFromLeft (toggleWidth));
    muteButton.setBounds (toggleRow.removeFromLeft (toggleWidth));
    soloButton.setBounds (toggleRow);
}
