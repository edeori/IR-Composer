#include "MasterEQEditor.h"
#include "PluginProcessor.h"

MasterEQEditor::MasterEQEditor (IRComposerAudioProcessor& processor)
    : audioProcessor (processor)
{
    selectedBand = audioProcessor.getSelectedMasterBand();

    for (int b = 0; b < IRComposerConstants::numMasterBands; ++b)
    {
        auto button = std::make_unique<juce::TextButton> (juce::String (b + 1));
        button->setClickingTogglesState (false);
        button->onClick = [this, b]
        {
            audioProcessor.setSelectedMasterBand (b);
            selectBand (b);
        };
        addAndMakeVisible (*button);
        bandButtons[(size_t) b] = std::move (button);
    }

    addAndMakeVisible (activeButton);
    for (auto* s : { &freqSlider, &gainSlider, &qSlider })
    {
        s->setSliderStyle (juce::Slider::LinearHorizontal);
        s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 22);
        addAndMakeVisible (s);
    }
    for (auto* l : { &freqLabel, &gainLabel, &qLabel })
        addAndMakeVisible (l);
    addAndMakeVisible (typeBox);
    typeBox.addItemList (filterTypeNames(), 1);
    addAndMakeVisible (slopeBox);
    slopeBox.addItemList (filterSlopeNames(), 1);

    rebuildAttachments();
    updateBandButtonStates();

    startTimerHz (15);
}

MasterEQEditor::~MasterEQEditor()
{
    stopTimer();
}

void MasterEQEditor::timerCallback()
{
    // Picks up band selections made elsewhere (clicking a node on
    // MasterSpectrumAnalyzer) -- see this class's header comment.
    const auto processorSelection = audioProcessor.getSelectedMasterBand();
    if (processorSelection != selectedBand)
        selectBand (processorSelection);
}

void MasterEQEditor::selectBand (int index)
{
    if (index == selectedBand)
        return;
    selectedBand = index;
    rebuildAttachments();
    updateBandButtonStates();
}

void MasterEQEditor::rebuildAttachments()
{
    // Destroy old attachments before creating new ones bound to the newly-selected
    // band's parameter IDs -- same rebuild-on-selection pattern as the sibling
    // project's BandEditor::rebuildForSelectedBand.
    activeAttachment.reset();
    freqAttachment.reset();
    gainAttachment.reset();
    qAttachment.reset();
    typeAttachment.reset();
    slopeAttachment.reset();

    auto& apvts = audioProcessor.apvts;
    activeAttachment = std::make_unique<ButtonAttachment> (apvts, ParamIDs::masterBandActive (selectedBand), activeButton);
    freqAttachment = std::make_unique<SliderAttachment> (apvts, ParamIDs::masterBandFreq (selectedBand), freqSlider);
    gainAttachment = std::make_unique<SliderAttachment> (apvts, ParamIDs::masterBandGain (selectedBand), gainSlider);
    qAttachment = std::make_unique<SliderAttachment> (apvts, ParamIDs::masterBandQ (selectedBand), qSlider);
    typeAttachment = std::make_unique<ComboAttachment> (apvts, ParamIDs::masterBandType (selectedBand), typeBox);
    slopeAttachment = std::make_unique<ComboAttachment> (apvts, ParamIDs::masterBandSlope (selectedBand), slopeBox);
}

void MasterEQEditor::updateBandButtonStates()
{
    for (int b = 0; b < IRComposerConstants::numMasterBands; ++b)
    {
        auto& button = *bandButtons[(size_t) b];
        const auto isSelected = (b == selectedBand);
        button.setColour (juce::TextButton::buttonColourId,
                           isSelected ? juce::Colours::cyan.withAlpha (0.6f) : juce::Colours::darkgrey);
    }
}

void MasterEQEditor::resized()
{
    auto area = getLocalBounds().reduced (4);

    auto bandRow = area.removeFromTop (28);
    const auto buttonWidth = bandRow.getWidth() / IRComposerConstants::numMasterBands;
    for (auto& button : bandButtons)
        button->setBounds (bandRow.removeFromLeft (buttonWidth).reduced (1));

    area.removeFromTop (6);

    auto activeRow = area.removeFromTop (24);
    activeButton.setBounds (activeRow.removeFromLeft (80));
    slopeBox.setBounds (activeRow.removeFromRight (120));
    activeRow.removeFromRight (4);
    typeBox.setBounds (activeRow.removeFromRight (140));
    area.removeFromTop (4);

    auto layoutRow = [&area] (juce::Component& label, juce::Component& control)
    {
        auto row = area.removeFromTop (24);
        label.setBounds (row.removeFromLeft (50));
        control.setBounds (row);
        area.removeFromTop (3);
    };
    layoutRow (freqLabel, freqSlider);
    layoutRow (gainLabel, gainSlider);
    layoutRow (qLabel, qSlider);
}
