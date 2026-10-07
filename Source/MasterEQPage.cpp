#include "MasterEQPage.h"

MasterEQPage::MasterEQPage (IRComposerAudioProcessor& p)
    : spectrumAnalyzer (p), masterEqEditor (p)
{
    addAndMakeVisible (spectrumAnalyzer);
    addAndMakeVisible (masterEqEditor);
}

void MasterEQPage::resized()
{
    const auto sx = (float) getWidth() / designWidth;
    const auto sy = (float) getHeight() / designHeight;

    const auto scaleRect = [sx, sy] (float x, float y, float w, float h)
    {
        return juce::Rectangle<int> ((int) (x * sx), (int) (y * sy), (int) (w * sx), (int) (h * sy));
    };

    const float margin = 8.0f;
    float y = margin;
    spectrumAnalyzer.setBounds (scaleRect (margin, y, designWidth - margin * 2.0f, 300.0f));

    y += 300.0f + margin;
    masterEqEditor.setBounds (scaleRect (margin, y, designWidth - margin * 2.0f, 190.0f));
}
