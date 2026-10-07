#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "MasterEQEditor.h"
#include "MasterSpectrumAnalyzer.h"

class IRComposerAudioProcessor;

// Second tab: the post-convolution, real-time Master EQ -- spectrum analyzer (input vs
// output vs the EQ's own response curve) above the band strip/detail editor, same
// layout relationship as parametric-dynamic-eq-VST's own editor.
class MasterEQPage final : public juce::Component
{
public:
    explicit MasterEQPage (IRComposerAudioProcessor& processor);

    void resized() override;

private:
    static constexpr float designWidth = 1180.0f;
    static constexpr float designHeight = 520.0f;

    MasterSpectrumAnalyzer spectrumAnalyzer;
    MasterEQEditor masterEqEditor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MasterEQPage)
};
