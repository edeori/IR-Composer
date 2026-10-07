#include "PluginEditor.h"
#include "PluginProcessor.h"

IRComposerAudioProcessorEditor::IRComposerAudioProcessorEditor (IRComposerAudioProcessor& p)
    : AudioProcessorEditor (&p)
{
    addAndMakeVisible (tabs);
    tabs.setTabBarDepth (28);
    tabs.addTab ("IR Slots", juce::Colours::transparentBlack, new IRSlotsPage (p), true);
    tabs.addTab ("Master EQ", juce::Colours::transparentBlack, new MasterEQPage (p), true);

    setResizable (true, true);
    setResizeLimits ((int) (designWidth * 0.5f), (int) (designHeight * 0.5f), (int) (designWidth * 2.0f), (int) (designHeight * 2.0f));
    setSize ((int) designWidth, (int) designHeight);
}

IRComposerAudioProcessorEditor::~IRComposerAudioProcessorEditor() = default;

void IRComposerAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void IRComposerAudioProcessorEditor::resized()
{
    tabs.setBounds (getLocalBounds());
}
