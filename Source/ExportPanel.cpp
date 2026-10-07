#include "ExportPanel.h"
#include "AppState.h"
#include "PluginProcessor.h"

ExportPanel::ExportPanel (IRComposerAudioProcessor& processor)
    : audioProcessor (processor)
{
    addAndMakeVisible (formatBox);
    formatBox.addItem ("WAV", 1);
    formatBox.addItem ("AIFF", 2);

    addAndMakeVisible (bitDepthBox);
    bitDepthBox.addItem ("16-bit", 16);
    bitDepthBox.addItem ("24-bit", 24);
    bitDepthBox.addItem ("32-bit float", 32);

    addAndMakeVisible (sampleRateBox);
    sampleRateBox.addItem ("Same as project", 1);
    sampleRateBox.addItem ("44100 Hz", 44100);
    sampleRateBox.addItem ("48000 Hz", 48000);
    sampleRateBox.addItem ("88200 Hz", 88200);
    sampleRateBox.addItem ("96000 Hz", 96000);

    addAndMakeVisible (normalizeButton);

    addAndMakeVisible (fadeOutLabel);
    addAndMakeVisible (fadeOutSlider);
    fadeOutSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    fadeOutSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 55, 20);
    fadeOutSlider.setRange (0.0, 500.0, 1.0);
    const juce::String fadeOutTooltip =
        "Only smooths the very end of the export -- the combine length (set on the "
        "combined waveform view, or per-slot via each slot's own auto-trimmed end "
        "marker) already excludes long silent tails, so this doesn't cut into real "
        "signal like a sweep's decay. Raise it only if you still hear a click at the "
        "very end of the exported file.";
    fadeOutLabel.setTooltip (fadeOutTooltip);
    fadeOutSlider.setTooltip (fadeOutTooltip);

    addAndMakeVisible (exportButton);
    addAndMakeVisible (statusLabel);
    statusLabel.setJustificationType (juce::Justification::centredLeft);
    statusLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);

    loadInitialValuesFromAppState();

    // Captured by value, not by reference: juce::ValueTree is a cheap, reference-
    // counted handle to shared underlying data (like a smart pointer), so a copy still
    // mutates the same tree the processor holds. Capturing audioProcessor.getAppState()
    // by reference here would dangle once the constructor returns, since the reference
    // itself (not the tree it refers to) is a local variable.
    auto appState = audioProcessor.getAppState();
    formatBox.onChange = [this, appState]() mutable
    {
        appState.setProperty (AppStateIDs::exportFormat, formatBox.getSelectedId(), nullptr);
        updateBitDepthOptions();
    };
    bitDepthBox.onChange = [this, appState]() mutable
    {
        appState.setProperty (AppStateIDs::exportBitDepth, bitDepthBox.getSelectedId(), nullptr);
    };
    sampleRateBox.onChange = [this, appState]() mutable
    {
        appState.setProperty (AppStateIDs::exportSampleRate, sampleRateBox.getSelectedId(), nullptr);
    };
    normalizeButton.onClick = [this, appState]() mutable
    {
        appState.setProperty (AppStateIDs::exportNormalize, normalizeButton.getToggleState(), nullptr);
    };
    fadeOutSlider.onValueChange = [this, appState]() mutable
    {
        appState.setProperty (AppStateIDs::exportFadeOutMs, fadeOutSlider.getValue(), nullptr);
    };
    exportButton.onClick = [this] { startExport(); };
}

void ExportPanel::updateBitDepthOptions()
{
    const auto aiff = formatBox.getSelectedId() == 2;
    bitDepthBox.setItemEnabled (32, ! aiff);
    if (aiff && bitDepthBox.getSelectedId() == 32)
    {
        bitDepthBox.setSelectedId (24, juce::dontSendNotification);
        audioProcessor.getAppState().setProperty (AppStateIDs::exportBitDepth, 24, nullptr);
    }
}

void ExportPanel::loadInitialValuesFromAppState()
{
    auto& appState = audioProcessor.getAppState();
    formatBox.setSelectedId ((int) appState.getProperty (AppStateIDs::exportFormat, 1), juce::dontSendNotification);
    bitDepthBox.setSelectedId ((int) appState.getProperty (AppStateIDs::exportBitDepth, 24), juce::dontSendNotification);
    sampleRateBox.setSelectedId ((int) appState.getProperty (AppStateIDs::exportSampleRate, 1), juce::dontSendNotification);
    normalizeButton.setToggleState ((bool) appState.getProperty (AppStateIDs::exportNormalize, true), juce::dontSendNotification);
    fadeOutSlider.setValue ((double) appState.getProperty (AppStateIDs::exportFadeOutMs, 5.0), juce::dontSendNotification);
    updateBitDepthOptions();
}

ExportSettings ExportPanel::readSettingsFromControls() const
{
    ExportSettings settings;
    settings.format = formatBox.getSelectedId() == 2 ? ExportSettings::Format::aiff : ExportSettings::Format::wav;
    settings.bitDepth = bitDepthBox.getSelectedId() > 0 ? bitDepthBox.getSelectedId() : 24;
    const auto srId = sampleRateBox.getSelectedId();
    settings.sampleRate = srId > 1 ? (double) srId : 0.0;
    settings.normalize = normalizeButton.getToggleState();
    settings.fadeOutMs = (float) fadeOutSlider.getValue();
    return settings;
}

void ExportPanel::startExport()
{
    const auto extension = formatBox.getSelectedId() == 2 ? "*.aiff" : "*.wav";
    fileChooser = std::make_unique<juce::FileChooser> ("Export blended IR as...", juce::File(), extension);
    constexpr auto flags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting;

    fileChooser->launchAsync (flags, [this] (const juce::FileChooser& fc)
    {
        auto file = fc.getResult();
        if (file == juce::File())
            return;

        const auto wantsAiff = formatBox.getSelectedId() == 2;
        const auto expectedExt = wantsAiff ? ".aiff" : ".wav";
        if (! file.hasFileExtension (expectedExt))
            file = file.withFileExtension (expectedExt);

        statusLabel.setText ("Exporting...", juce::dontSendNotification);
        exportButton.setEnabled (false);

        exportEngine.exportAsync (audioProcessor, readSettingsFromControls(), file,
            [safeThis = juce::Component::SafePointer<ExportPanel> (this)] (bool success, juce::String error)
            {
                if (safeThis == nullptr)
                    return;
                safeThis->exportButton.setEnabled (true);
                safeThis->statusLabel.setText (success ? "Export complete." : ("Export failed: " + error),
                                      juce::dontSendNotification);
                safeThis->statusLabel.setColour (juce::Label::textColourId,
                                        success ? juce::Colours::lightgreen : juce::Colours::orangered);
            });
    });
}

void ExportPanel::resized()
{
    auto area = getLocalBounds().reduced (4);

    auto row1 = area.removeFromTop (24);
    const auto thirdWidth = row1.getWidth() / 3;
    formatBox.setBounds (row1.removeFromLeft (thirdWidth).reduced (2, 0));
    bitDepthBox.setBounds (row1.removeFromLeft (thirdWidth).reduced (2, 0));
    sampleRateBox.setBounds (row1.reduced (2, 0));
    area.removeFromTop (4);

    auto row2 = area.removeFromTop (24);
    normalizeButton.setBounds (row2.removeFromLeft (120));
    fadeOutLabel.setBounds (row2.removeFromLeft (90));
    fadeOutSlider.setBounds (row2);
    area.removeFromTop (4);

    auto row3 = area.removeFromTop (28);
    exportButton.setBounds (row3.removeFromLeft (120));
    row3.removeFromLeft (8);
    statusLabel.setBounds (row3);
}
