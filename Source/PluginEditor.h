#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "IRSlotsPage.h"
#include "MasterEQPage.h"

class IRComposerAudioProcessor;

// Top-level editor: just a tab host. "IR Slots" (the 4 source IRs, combined alignment
// view, export) and "Master EQ" (post-convolution real-time EQ + spectrum analyzer)
// each get their own tab instead of sharing one crowded page.
class IRComposerAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit IRComposerAudioProcessorEditor (IRComposerAudioProcessor&);
    ~IRComposerAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    static constexpr float designWidth = 1180.0f;
    static constexpr float designHeight = 590.0f;

    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IRComposerAudioProcessorEditor)
};
