#include "CombinedWaveformComponent.h"
#include "PluginProcessor.h"
#include <cmath>

// Maximally bright/saturated and spread across both hue and brightness -- not just
// picked for hue variety, since relying on hue alone is illegible to colour-blind
// users and too subtle against a dark background even for typical vision.
const std::array<juce::Colour, IRComposerConstants::numSlots> CombinedWaveformComponent::slotColours {
    juce::Colours::white, juce::Colours::yellow, juce::Colours::cyan, juce::Colours::magenta
};

CombinedWaveformComponent::CombinedWaveformComponent (IRComposerAudioProcessor& p)
    : processor (p)
{
    addAndMakeVisible (verticalZoomLabel);
    addAndMakeVisible (verticalZoomSlider);
    verticalZoomSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    verticalZoomSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 50, headerHeight);
    verticalZoomSlider.setRange (1.0, 10.0, 0.1);
    verticalZoomSlider.setValue (1.0, juce::dontSendNotification);
    verticalZoomSlider.onValueChange = [this] { repaint(); };

    addAndMakeVisible (horizontalZoomInfoLabel);
    horizontalZoomInfoLabel.setJustificationType (juce::Justification::centredRight);
    horizontalZoomInfoLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    horizontalZoomInfoLabel.setFont (juce::Font (juce::FontOptions (13.0f)));
    horizontalZoomInfoLabel.setText ("scroll = zoom, trackpad swipe = pan, dbl-click = reset",
                                      juce::dontSendNotification);

    setWantsKeyboardFocus (false);
    startTimerHz (12);
}

CombinedWaveformComponent::~CombinedWaveformComponent()
{
    stopTimer();
}

void CombinedWaveformComponent::timerCallback()
{
    if (makeRenderKey() == lastRenderKey)
        return;

    rebuildPeaks();
    repaint();
}

std::vector<double> CombinedWaveformComponent::makeRenderKey() const
{
    std::vector<double> key { (double) processor.getCombineLengthSamples(), (double) getWidth(),
                              (double) visibleStartSample, (double) visibleLengthSamples,
                              horizontalZoomUserSet ? 1.0 : 0.0 };
    for (int s = 0; s < IRComposerConstants::numSlots; ++s)
    {
        const auto& slot = processor.getIRSlot (s);
        const auto p = processor.getSlotRenderParams (s);
        key.insert (key.end(), { (double) slot.getContentVersion(), (double) slot.getRawLengthSamples(),
                                 (double) p.gainDb, p.polarityInverted ? 1.0 : 0.0,
                                 (double) p.delayMs, (double) p.rotateDegrees });
    }
    return key;
}

juce::int64 CombinedWaveformComponent::getDisplayRangeSamples() const noexcept
{
    juce::int64 longest = processor.getCombineLengthSamples();
    for (int s = 0; s < IRComposerConstants::numSlots; ++s)
        longest = juce::jmax (longest, processor.getIRSlot (s).getRawLengthSamples());
    return juce::jmax ((juce::int64) 1, longest);
}

void CombinedWaveformComponent::clampVisibleWindow() noexcept
{
    visibleLengthSamples = juce::jlimit ((juce::int64) 1, lastDisplayRange, visibleLengthSamples);
    const auto maxStart = juce::jmax ((juce::int64) 0, lastDisplayRange - visibleLengthSamples);
    visibleStartSample = juce::jlimit ((juce::int64) 0, maxStart, visibleStartSample);
}

void CombinedWaveformComponent::rebuildPeaks()
{
    lastCombineLength = processor.getCombineLengthSamples();
    lastDisplayRange = getDisplayRangeSamples();
    if (! horizontalZoomUserSet)
        visibleStartSample = 0; // keep auto-tracking the full (possibly still-growing) range
    visibleLengthSamples = horizontalZoomUserSet ? visibleLengthSamples : lastDisplayRange;
    clampVisibleWindow();

    // Taken once the visible window has been (re)clamped, so an unchanged state reads
    // back as the same key on the next timer tick -- but before rendering, so a change
    // landing mid-render still shows up as a mismatch next tick.
    lastRenderKey = makeRenderKey();

    const auto width = juce::jmax (1, getWidth());

    for (int s = 0; s < IRComposerConstants::numSlots; ++s)
    {
        const auto& slot = processor.getIRSlot (s);
        if (! slot.hasAudio())
        {
            slotPeaks[(size_t) s].rebuild (juce::AudioBuffer<float>(), 0);
            continue;
        }
        // Rendered across the full display range, then sliced down to just the
        // currently visible (possibly zoomed-in) window before building peaks -- so
        // zooming in reveals genuinely more detail (each pixel column now averages
        // fewer real samples), not just a stretched view of the same low-res peaks.
        // renderProcessed() already applies each slot's own crop start/end, so a
        // trimmed slot's silence outside its crop region shows here too.
        const auto rendered = slot.renderProcessed (lastDisplayRange, processor.getSlotRenderParams (s));
        juce::AudioBuffer<float> visibleSlice (rendered.getNumChannels(), (int) visibleLengthSamples);
        for (int ch = 0; ch < rendered.getNumChannels(); ++ch)
            visibleSlice.copyFrom (ch, 0, rendered, ch, (int) visibleStartSample, (int) visibleLengthSamples);
        slotPeaks[(size_t) s].rebuild (visibleSlice, width);
    }

    const auto zoomRatio = lastDisplayRange > 0 ? (double) lastDisplayRange / (double) visibleLengthSamples : 1.0;
    horizontalZoomInfoLabel.setText (juce::String (zoomRatio, 1) + "x -- scroll = zoom, trackpad swipe = pan, dbl-click = reset",
                                      juce::dontSendNotification);
}

void CombinedWaveformComponent::resized()
{
    auto area = getLocalBounds();
    auto header = area.removeFromTop (headerHeight);
    verticalZoomLabel.setBounds (header.removeFromLeft (40));
    verticalZoomSlider.setBounds (header.removeFromLeft (200));
    header.removeFromLeft (8);
    horizontalZoomInfoLabel.setBounds (header);

    rebuildPeaks();
}

int CombinedWaveformComponent::sampleToX (juce::int64 sample) const noexcept
{
    if (visibleLengthSamples <= 0)
        return 0;
    return (int) ((double) (sample - visibleStartSample) / (double) visibleLengthSamples * (double) getWidth());
}

juce::int64 CombinedWaveformComponent::xToSample (int x) const noexcept
{
    if (getWidth() <= 0 || visibleLengthSamples <= 0)
        return visibleStartSample;
    const auto proportion = juce::jlimit (0.0, 1.0, (double) x / (double) getWidth());
    return visibleStartSample + (juce::int64) (proportion * (double) visibleLengthSamples);
}

void CombinedWaveformComponent::mouseDown (const juce::MouseEvent& e)
{
    if (e.y >= headerHeight)
        mouseDrag (e);
}

void CombinedWaveformComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (e.y < headerHeight)
        return;
    processor.setCombineLengthSamples (juce::jmax ((juce::int64) 1, xToSample (e.x)));
}

void CombinedWaveformComponent::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (e.y < headerHeight || lastDisplayRange <= 0)
        return;

    // Trackpad horizontal swipe (or a shift-scroll wheel) pans; a plain vertical wheel
    // zooms. Distinguished by which axis actually moved more, the usual convention.
    if (std::abs (wheel.deltaX) > std::abs (wheel.deltaY) * 1.5f && wheel.deltaX != 0.0f)
    {
        horizontalZoomUserSet = true;
        const auto panSamples = (juce::int64) ((double) -wheel.deltaX * (double) visibleLengthSamples * 0.5);
        visibleStartSample += panSamples;
        clampVisibleWindow();
        rebuildPeaks();
        repaint();
        return;
    }

    if (wheel.deltaY == 0.0f)
        return;

    horizontalZoomUserSet = true;

    // Zoom anchored at the cursor's sample position, so the point under the mouse
    // stays put -- lets the user zoom precisely into the very start of the overlay
    // (or any other point) to check the slots' transients line up.
    const auto proportion = juce::jlimit (0.0, 1.0, (double) e.x / (double) juce::jmax (1, getWidth()));
    const auto anchorSample = visibleStartSample + (juce::int64) (proportion * (double) visibleLengthSamples);

    const auto zoomFactor = wheel.deltaY > 0.0f ? 0.8 : 1.25;
    const auto minVisibleSamples = (juce::int64) juce::jmax (64.0, (double) lastDisplayRange * 0.0005);
    visibleLengthSamples = juce::jlimit (minVisibleSamples, lastDisplayRange,
                                          (juce::int64) ((double) visibleLengthSamples * zoomFactor));
    visibleStartSample = anchorSample - (juce::int64) (proportion * (double) visibleLengthSamples);
    clampVisibleWindow();

    rebuildPeaks();
    repaint();
}

void CombinedWaveformComponent::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.y < headerHeight)
        return;
    // Back to auto-tracking the full range, not just a one-off snap to whatever the
    // range happens to be right now -- so it keeps following if more/longer IRs load
    // afterward, same as before the user ever touched zoom/pan.
    horizontalZoomUserSet = false;
    visibleStartSample = 0;
    visibleLengthSamples = lastDisplayRange;
    rebuildPeaks();
    repaint();
}

void CombinedWaveformComponent::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().withTrimmedTop (headerHeight);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillRect (area);

    if (lastDisplayRange <= 0 || lastCombineLength <= 0)
    {
        g.setColour (juce::Colours::grey);
        g.drawText ("Load IRs to see the combined preview", area, juce::Justification::centred);
        return;
    }

    const auto centreY = (float) area.getCentreY();
    const auto halfHeight = (float) area.getHeight() * 0.5f * (float) verticalZoomSlider.getValue();

    // See SlotWaveformComponent::paint() for why this needs to exist: without a
    // visibly different zero-reference line, a quiet stretch of any slot's trace is
    // indistinguishable from "no waveform drawn here at all".
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawHorizontalLine ((int) centreY, (float) area.getX(), (float) area.getRight());

    for (int s = 0; s < IRComposerConstants::numSlots; ++s)
    {
        const auto& peaks = slotPeaks[(size_t) s];
        if (peaks.getNumColumns() == 0)
            continue;

        g.setColour (slotColours[(size_t) s]);
        for (int x = 0; x < area.getWidth(); ++x)
        {
            const auto peak = peaks.getPeakAt (x);
            const auto top = centreY - waveformVisualScale (peak.getEnd()) * halfHeight;
            const auto bottom = centreY - waveformVisualScale (peak.getStart()) * halfHeight;
            g.fillRect (juce::Rectangle<float> ((float) (area.getX() + x), top, 2.0f, juce::jmax (2.0f, bottom - top)));
        }
    }

    // Dim everything past the combine-length marker -- that's the part that gets cut
    // off from the live-audition/export master IR. Both markers below are only drawn
    // when they actually fall within the current (possibly zoomed-in) visible window.
    const auto markerX = sampleToX (lastCombineLength);
    if (markerX >= 0 && markerX < area.getWidth())
    {
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRect (area.getX() + markerX, area.getY(), area.getWidth() - markerX, area.getHeight());

        // Red for the length/end marker (matching SlotWaveformComponent's crop-end
        // colour) so it never gets confused with any of the bright slot-trace colours.
        g.setColour (juce::Colours::red);
        g.fillRect ((float) (area.getX() + markerX) - 1.0f, (float) area.getY(), 2.0f, (float) area.getHeight());
    }

    const auto startX = sampleToX (0);
    if (startX >= 0 && startX < area.getWidth())
    {
        g.setColour (juce::Colours::yellow);
        g.fillRect ((float) (area.getX() + startX), (float) area.getY(), 2.0f, (float) area.getHeight());
    }
}
