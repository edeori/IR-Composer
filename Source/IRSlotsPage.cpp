#include "IRSlotsPage.h"
#include "PluginProcessor.h"

IRSlotsPage::IRSlotsPage (IRComposerAudioProcessor& p)
    : audioProcessor (p), combinedWaveform (p), blendPad (p), exportPanel (p)
{
    for (int s = 0; s < IRComposerConstants::numSlots; ++s)
    {
        slotPanels[(size_t) s] = std::make_unique<SlotControlPanel> (audioProcessor, s);
        addAndMakeVisible (*slotPanels[(size_t) s]);
    }

    addAndMakeVisible (combinedWaveform);
    addAndMakeVisible (blendPad);
    addAndMakeVisible (exportPanel);

    audioProcessor.onSlotLoaded = [this] (int slot)
    {
        if (slot >= 0 && slot < IRComposerConstants::numSlots)
            slotPanels[(size_t) slot]->getWaveformComponent().refreshFromProcessor();
    };
}

IRSlotsPage::~IRSlotsPage()
{
    audioProcessor.onSlotLoaded = nullptr;
}

void IRSlotsPage::resized()
{
    const auto sx = (float) getWidth() / designWidth;
    const auto sy = (float) getHeight() / designHeight;

    const auto scaleRect = [sx, sy] (float x, float y, float w, float h)
    {
        return juce::Rectangle<int> ((int) (x * sx), (int) (y * sy), (int) (w * sx), (int) (h * sy));
    };

    const float margin = 8.0f;
    const float slotWidth = (designWidth - margin * 5.0f) / 4.0f;
    const float slotHeight = 260.0f;

    for (int s = 0; s < IRComposerConstants::numSlots; ++s)
    {
        const auto x = margin + (float) s * (slotWidth + margin);
        slotPanels[(size_t) s]->setBounds (scaleRect (x, margin, slotWidth, slotHeight));
    }

    // The blend pad sits to the right of the combined preview and runs down beside
    // the export panel too, so the pad itself gets a usable square.
    const float padWidth = 230.0f;
    const float contentWidth = designWidth - margin * 3.0f - padWidth;
    float y = margin * 2.0f + slotHeight;
    combinedWaveform.setBounds (scaleRect (margin, y, contentWidth, 160.0f));
    blendPad.setBounds (scaleRect (margin * 2.0f + contentWidth, y, padWidth, 160.0f + margin + 90.0f));

    y += 160.0f + margin;
    exportPanel.setBounds (scaleRect (margin, y, contentWidth, 90.0f));
}
