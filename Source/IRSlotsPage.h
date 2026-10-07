#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <memory>
#include "BlendPadComponent.h"
#include "CombinedWaveformComponent.h"
#include "ExportPanel.h"
#include "Parameters.h"
#include "SlotControlPanel.h"

class IRComposerAudioProcessor;

// First tab: the 4 source-IR slots, the combined alignment view, and export -- the
// Master EQ lives on its own tab (MasterEQPage) instead of sharing this page, per the
// user's request to keep the window from being "one giant page".
class IRSlotsPage final : public juce::Component
{
public:
    explicit IRSlotsPage (IRComposerAudioProcessor& processor);
    ~IRSlotsPage() override;

    void resized() override;

private:
    static constexpr float designWidth = 1180.0f;
    static constexpr float designHeight = 550.0f;

    IRComposerAudioProcessor& audioProcessor;

    std::array<std::unique_ptr<SlotControlPanel>, IRComposerConstants::numSlots> slotPanels;
    CombinedWaveformComponent combinedWaveform;
    BlendPadComponent blendPad;
    ExportPanel exportPanel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IRSlotsPage)
};
