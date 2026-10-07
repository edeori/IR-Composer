#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "BlendWeights.h"

class IRComposerAudioProcessor;

// The blend pad: a square with one slot on each corner (see BlendWeights.h) and a
// draggable puck whose position sets the mix ratio between the active slots -- instead
// of balancing the slots against each other with their Gain faders. Bound to the
// automatable Blend X/Y parameters; double-click re-centres the puck (equal blend).
//
// Also hosts the alignment controls, since aligning the slots is what makes blending
// them sound right: an "auto-align on load" toggle and an "Align all" button.
class BlendPadComponent final : public juce::Component, private juce::Timer
{
public:
    explicit BlendPadComponent (IRComposerAudioProcessor& processor);
    ~BlendPadComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    juce::Rectangle<float> getPadArea() const;
    juce::Point<float> toScreen (BlendWeights::Point p) const;
    void setPuckFromMouse (juce::Point<float> position);

    IRComposerAudioProcessor& processor;
    juce::RangedAudioParameter* xParam = nullptr;
    juce::RangedAudioParameter* yParam = nullptr;
    bool dragging = false;

    juce::ToggleButton autoAlignToggle { "Auto-align on load" };
    juce::TextButton alignAllButton { "Align all" };

    static constexpr int controlsHeight = 24;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BlendPadComponent)
};
