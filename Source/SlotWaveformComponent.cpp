#include "SlotWaveformComponent.h"
#include "PluginProcessor.h"

namespace
{
    const juce::String irFileWildcard ("*.wav;*.aif;*.aiff;*.flac;*.ogg");
}

SlotWaveformComponent::SlotWaveformComponent (IRComposerAudioProcessor& p, int slotIndex)
    : processor (p), slot (slotIndex)
{
    addAndMakeVisible (loadButton);
    addAndMakeVisible (autoTrimButton);
    addAndMakeVisible (alignButton);
    addAndMakeVisible (fileLabel);
    fileLabel.setJustificationType (juce::Justification::centredLeft);
    fileLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    fileLabel.setInterceptsMouseClicks (false, false);

    loadButton.onClick = [this]
    {
        // Open in the current IR's folder, so browsing a library doesn't start over
        // from the home directory every time.
        fileChooser = std::make_unique<juce::FileChooser> ("Select an IR file...",
                                                             processor.getSlotRequestedFile (slot),
                                                             irFileWildcard);
        constexpr auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
        fileChooser->launchAsync (flags, [this] (const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();
            if (file != juce::File())
                loadFile (file);
        });
    };

    addAndMakeVisible (prevButton);
    addAndMakeVisible (nextButton);
    prevButton.setTooltip ("Previous IR in this folder");
    nextButton.setTooltip ("Next IR in this folder");
    prevButton.onClick = [this] { stepFile (-1); };
    nextButton.onClick = [this] { stepFile (+1); };

    autoTrimButton.setTooltip ("Snap the end marker back to where the tail decays below -60dB "
                                "(what a fresh load already does by default)");
    autoTrimButton.onClick = [this]
    {
        processor.autoTrimSlotTail (slot);
        repaint();
    };

    alignButton.setTooltip ("Time- and polarity-align this IR to the lowest-numbered other loaded slot "
                            "(sets Delay/Invert, or trims the start if this IR is late)");
    alignButton.onClick = [this]
    {
        processor.alignSlot (slot);
        repaint();
    };

    refreshFromProcessor();
}

void SlotWaveformComponent::refreshFromProcessor()
{
    updateFileLabel();
    rebuildPeaks();
    repaint();
}

void SlotWaveformComponent::updateFileLabel()
{
    const auto file = processor.getSlotRequestedFile (slot);
    fileLabel.setText (file == juce::File() ? juce::String ("(no IR loaded)") : file.getFileName(),
                       juce::dontSendNotification);
    fileLabel.setTooltip (file.getFullPathName());

    const auto hasFile = file != juce::File();
    prevButton.setEnabled (hasFile);
    nextButton.setEnabled (hasFile);
}

void SlotWaveformComponent::loadFile (const juce::File& file)
{
    processor.loadIRFile (slot, file);
    updateFileLabel();
}

void SlotWaveformComponent::stepFile (int direction)
{
    const auto current = processor.getSlotRequestedFile (slot);
    const auto folder = current.getParentDirectory();
    if (current == juce::File() || ! folder.isDirectory())
        return;

    auto files = folder.findChildFiles (juce::File::findFiles, false, irFileWildcard);
    if (files.isEmpty())
        return;

    // Same order as Finder: case-insensitive, with numbers compared naturally
    // ("IR 2" before "IR 10").
    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
    {
        return a.getFileName().compareNatural (b.getFileName()) < 0;
    });

    auto index = files.indexOf (current);
    if (index < 0)
    {
        // The current file was moved/renamed: start from where it would have sorted.
        index = 0;
        while (index < files.size() && files[index].getFileName().compareNatural (current.getFileName()) < 0)
            ++index;
        if (direction > 0)
            --index;
    }

    const auto count = files.size();
    const auto target = ((index + direction) % count + count) % count;
    if (files[target] != current)
        loadFile (files[target]);
}

void SlotWaveformComponent::rebuildPeaks()
{
    const auto raw = processor.getIRSlot (slot).getRawBufferAtHostRateCopy();
    rawLengthAtLastBuild = raw.getNumSamples();
    const auto width = juce::jmax (1, getWidth());
    peakCache.rebuild (raw, width);
}

void SlotWaveformComponent::resized()
{
    auto area = getLocalBounds();
    auto topRow = area.removeFromTop (waveformTopMargin);
    loadButton.setBounds (topRow.removeFromLeft (64).reduced (1));
    topRow.removeFromLeft (4);
    autoTrimButton.setBounds (topRow.removeFromLeft (48).reduced (1));
    topRow.removeFromLeft (4);
    alignButton.setBounds (topRow.removeFromLeft (48).reduced (1));
    topRow.removeFromLeft (4);
    nextButton.setBounds (topRow.removeFromRight (24).reduced (1));
    prevButton.setBounds (topRow.removeFromRight (24).reduced (1));
    topRow.removeFromRight (4);
    fileLabel.setBounds (topRow);

    rebuildPeaks();
}

juce::int64 SlotWaveformComponent::xToSample (int x) const noexcept
{
    if (rawLengthAtLastBuild <= 0 || getWidth() <= 0)
        return 0;
    const auto proportion = juce::jlimit (0.0, 1.0, (double) x / (double) getWidth());
    return (juce::int64) (proportion * (double) rawLengthAtLastBuild);
}

int SlotWaveformComponent::sampleToX (juce::int64 sample) const noexcept
{
    if (rawLengthAtLastBuild <= 0)
        return 0;
    return (int) ((double) sample / (double) rawLengthAtLastBuild * (double) getWidth());
}

void SlotWaveformComponent::mouseDown (const juce::MouseEvent& e)
{
    if (e.y < waveformTopMargin || rawLengthAtLastBuild <= 0)
    {
        activeDrag = DragTarget::none;
        return;
    }

    const auto& irSlot = processor.getIRSlot (slot);
    const auto startX = sampleToX (irSlot.getCropStartSample());
    const auto endX = sampleToX (irSlot.getCropEndSample());

    // Whichever marker is closer to the click point is the one being dragged, for the
    // rest of this gesture -- lets both markers live in the same narrow strip without
    // needing separate hit-zones.
    activeDrag = std::abs (e.x - startX) <= std::abs (e.x - endX) ? DragTarget::start : DragTarget::end;
    mouseDrag (e);
}

void SlotWaveformComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (activeDrag == DragTarget::none || rawLengthAtLastBuild <= 0)
        return;

    const auto sample = xToSample (e.x);
    if (activeDrag == DragTarget::start)
        processor.setSlotCropStartSample (slot, sample);
    else
        processor.setSlotCropEndSample (slot, sample);

    repaint();
}

void SlotWaveformComponent::paint (juce::Graphics& g)
{
    auto waveformArea = getLocalBounds().withTrimmedTop (waveformTopMargin);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillRect (waveformArea);

    if (rawLengthAtLastBuild <= 0)
    {
        g.setColour (juce::Colours::grey);
        g.drawText ("Load an IR to see its waveform", waveformArea, juce::Justification::centred);
        return;
    }

    const auto centreY = (float) waveformArea.getCentreY();
    const auto halfHeight = (float) waveformArea.getHeight() * 0.5f;
    const auto& irSlot = processor.getIRSlot (slot);
    const auto startX = sampleToX (irSlot.getCropStartSample());
    const auto endX = sampleToX (irSlot.getCropEndSample());

    // A barely-there zero-reference line drawn first, so it reads as background, not
    // signal -- the waveform itself is pure white at full opacity, as bright/contrasty
    // against the dark background as this UI can make it. Relying on a brightness gap
    // this large (rather than a hue difference) also keeps it legible regardless of
    // colour vision.
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawHorizontalLine ((int) centreY, (float) waveformArea.getX(), (float) waveformArea.getRight());

    g.setColour (juce::Colours::white);
    for (int x = 0; x < waveformArea.getWidth(); ++x)
    {
        const auto peak = peakCache.getPeakAt (x);
        const auto top = centreY - waveformVisualScale (peak.getEnd()) * halfHeight;
        const auto bottom = centreY - waveformVisualScale (peak.getStart()) * halfHeight;
        g.fillRect (juce::Rectangle<float> ((float) (waveformArea.getX() + x), top, 2.0f, juce::jmax (2.0f, bottom - top)));
    }

    // Dim the discarded regions before the start marker and after the end marker.
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    if (startX > 0)
        g.fillRect (waveformArea.getX(), waveformArea.getY(), startX, waveformArea.getHeight());
    if (endX < waveformArea.getWidth())
        g.fillRect (waveformArea.getX() + endX, waveformArea.getY(), waveformArea.getWidth() - endX, waveformArea.getHeight());

    g.setColour (juce::Colours::yellow);
    g.fillRect ((float) (waveformArea.getX() + startX) - 1.0f, (float) waveformArea.getY(), 2.0f, (float) waveformArea.getHeight());
    g.setColour (juce::Colours::red);
    g.fillRect ((float) (waveformArea.getX() + endX) - 1.0f, (float) waveformArea.getY(), 2.0f, (float) waveformArea.getHeight());
}
