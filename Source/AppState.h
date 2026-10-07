#pragma once

#include <juce_core/juce_core.h>

// Plain juce::ValueTree property identifiers for state that isn't meaningfully
// automatable (file paths, crop points, export settings) -- kept separate from the
// APVTS-driven Parameters.h ParamIDs, per the plan's "Paraméter/state modell" section.
namespace AppStateIDs
{
    inline const juce::Identifier root { "IRComposerAppState" };

    inline juce::Identifier slotFilePath (int s) { return juce::Identifier ("slotFilePath" + juce::String (s)); }
    inline juce::Identifier slotCropStartSample (int s) { return juce::Identifier ("slotCropStartSample" + juce::String (s)); }
    inline juce::Identifier slotCropEndSample (int s) { return juce::Identifier ("slotCropEndSample" + juce::String (s)); }

    inline const juce::Identifier combineLengthSamples { "combineLengthSamples" };

    // Whether a freshly loaded IR gets time/polarity-aligned to the other loaded slots
    // automatically (see IRComposerAudioProcessor::alignSlot). Defaults to on.
    inline const juce::Identifier autoAlignOnLoad { "autoAlignOnLoad" };

    inline const juce::Identifier exportFormat { "exportFormat" };
    inline const juce::Identifier exportBitDepth { "exportBitDepth" };
    inline const juce::Identifier exportSampleRate { "exportSampleRate" };
    inline const juce::Identifier exportNormalize { "exportNormalize" };
    inline const juce::Identifier exportFadeOutMs { "exportFadeOutMs" };
}
