#pragma once

#include <juce_core/juce_core.h>

namespace IRComposerConstants
{
    inline constexpr int numSlots = 4;
    inline constexpr int numMasterBands = 8;
    inline constexpr float minFrequencyHz = 20.0f;
    inline constexpr float maxFrequencyHz = 20000.0f;
    inline constexpr float maxFineDelayMs = 50.0f;
    inline constexpr float maxPhaseRotateDegrees = 180.0f;
}

// Mirrors parametric-dynamic-eq-VST/Source/Parameters.h's FilterType -- the SVFilter
// core (Source/SVFilter.h) is a near-verbatim copy of that sibling project's filter,
// so it expects the same enum shape.
enum class FilterType
{
    Bell = 0,
    LowShelf,
    HighShelf,
    HighPass,
    LowPass
};

inline const juce::StringArray& filterTypeNames()
{
    static const juce::StringArray names { "Bell", "Low Shelf", "High Shelf", "High Pass", "Low Pass" };
    return names;
}

// Rolloff steepness for High Pass / Low Pass bands only -- meaningless for Bell/Shelf
// types, which stay a single 2-pole (12dB/oct) stage regardless. Mirrors the sibling
// project's FilterSlope/SlopeFilter (Source/SlopeFilter.h is a near-verbatim copy),
// per the user's request for the same parametric depth as that source EQ.
enum class FilterSlope
{
    Slope6 = 0,
    Slope12,
    Slope18,
    Slope24
};

inline const juce::StringArray& filterSlopeNames()
{
    static const juce::StringArray names { "6 dB/oct", "12 dB/oct", "18 dB/oct", "24 dB/oct" };
    return names;
}

// Index-suffixed parameter ID helpers, one indexed by slot (0..numSlots-1) for the
// per-slot controls, and one indexed by master EQ band (0..numMasterBands-1) for the
// single, post-blend MasterEQ -- see the plan's "EQ csak az összegzett jelre" decision.
// Same pattern as the sibling project's ParamIDs, just split across two index spaces
// instead of one.
namespace ParamIDs
{
    inline juce::String slotGain (int s) { return "slotGain" + juce::String (s); }
    inline juce::String slotPolarity (int s) { return "slotPolarity" + juce::String (s); }
    inline juce::String slotDelay (int s) { return "slotDelay" + juce::String (s); }
    inline juce::String slotRotate (int s) { return "slotRotate" + juce::String (s); }
    inline juce::String slotMute (int s) { return "slotMute" + juce::String (s); }
    inline juce::String slotSolo (int s) { return "slotSolo" + juce::String (s); }

    // Blend pad puck position, 0..1 on each axis -- see BlendWeights.h.
    inline const juce::String blendX { "blendX" };
    inline const juce::String blendY { "blendY" };

    inline juce::String masterBandActive (int b) { return "masterBandActive" + juce::String (b); }
    inline juce::String masterBandFreq (int b) { return "masterBandFreq" + juce::String (b); }
    inline juce::String masterBandGain (int b) { return "masterBandGain" + juce::String (b); }
    inline juce::String masterBandQ (int b) { return "masterBandQ" + juce::String (b); }
    inline juce::String masterBandType (int b) { return "masterBandType" + juce::String (b); }
    inline juce::String masterBandSlope (int b) { return "masterBandSlope" + juce::String (b); }
}
