#include "BlendPadComponent.h"
#include "CombinedWaveformComponent.h"
#include "PluginProcessor.h"

BlendPadComponent::BlendPadComponent (IRComposerAudioProcessor& p)
    : processor (p),
      xParam (p.apvts.getParameter (ParamIDs::blendX)),
      yParam (p.apvts.getParameter (ParamIDs::blendY))
{
    addAndMakeVisible (autoAlignToggle);
    autoAlignToggle.setToggleState (processor.isAutoAlignOnLoad(), juce::dontSendNotification);
    autoAlignToggle.setTooltip ("Time- and polarity-align every newly loaded IR to the other loaded slots");
    autoAlignToggle.onClick = [this] { processor.setAutoAlignOnLoad (autoAlignToggle.getToggleState()); };

    addAndMakeVisible (alignAllButton);
    alignAllButton.setTooltip ("Align every loaded slot to the lowest-numbered loaded slot");
    alignAllButton.onClick = [this]
    {
        int referenceSlot = -1;
        for (int s = 0; s < IRComposerConstants::numSlots; ++s)
        {
            if (! processor.getIRSlot (s).hasAudio())
                continue;
            if (referenceSlot < 0)
                referenceSlot = s;
            else
                processor.alignSlot (s);
        }
    };

    startTimerHz (30);
}

BlendPadComponent::~BlendPadComponent()
{
    stopTimer();
}

void BlendPadComponent::timerCallback()
{
    // Picks up automation, session restore and slots loading/muting -- all cheap to
    // just repaint for at this size.
    const auto autoAlign = processor.isAutoAlignOnLoad();
    if (autoAlignToggle.getToggleState() != autoAlign)
        autoAlignToggle.setToggleState (autoAlign, juce::dontSendNotification);
    repaint();
}

juce::Rectangle<float> BlendPadComponent::getPadArea() const
{
    auto area = getLocalBounds().withTrimmedBottom (controlsHeight + 4).toFloat().reduced (4.0f);
    const auto side = juce::jmin (area.getWidth(), area.getHeight());
    return area.withSizeKeepingCentre (side, side).reduced (14.0f); // room for the corner badges
}

juce::Point<float> BlendPadComponent::toScreen (BlendWeights::Point p) const
{
    const auto pad = getPadArea();
    return { pad.getX() + p.x * pad.getWidth(), pad.getY() + p.y * pad.getHeight() };
}

void BlendPadComponent::resized()
{
    auto controls = getLocalBounds().removeFromBottom (controlsHeight);
    alignAllButton.setBounds (controls.removeFromRight (70).reduced (1));
    autoAlignToggle.setBounds (controls);
}

void BlendPadComponent::paint (juce::Graphics& g)
{
    const auto pad = getPadArea();
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillRoundedRectangle (pad.expanded (14.0f), 6.0f);
    g.setColour (juce::Colours::white.withAlpha (0.15f));
    g.drawRect (pad, 1.0f);
    g.drawLine (pad.getCentreX(), pad.getY(), pad.getCentreX(), pad.getBottom(), 0.5f);
    g.drawLine (pad.getX(), pad.getCentreY(), pad.getRight(), pad.getCentreY(), 0.5f);

    const auto active = processor.getBlendActiveMask();
    const BlendWeights::Point puck { xParam->convertFrom0to1 (xParam->getValue()),
                                     yParam->convertFrom0to1 (yParam->getValue()) };
    const auto weights = BlendWeights::compute (puck.x, puck.y, active);
    const auto puckScreen = toScreen (puck);

    // A line from the puck to each active corner, as thick/bright as that slot's share.
    for (int s = 0; s < IRComposerConstants::numSlots; ++s)
    {
        if (! active[(size_t) s])
            continue;
        const auto w = weights[(size_t) s];
        g.setColour (CombinedWaveformComponent::slotColours[(size_t) s].withAlpha (0.25f + 0.6f * w));
        g.drawLine ({ puckScreen, toScreen (BlendWeights::cornerFor (s)) }, 1.0f + 5.0f * w);
    }

    g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
    for (int s = 0; s < IRComposerConstants::numSlots; ++s)
    {
        const auto corner = toScreen (BlendWeights::cornerFor (s));
        const auto badge = juce::Rectangle<float> (26.0f, 26.0f).withCentre (corner);
        const auto colour = CombinedWaveformComponent::slotColours[(size_t) s];
        const auto isActive = active[(size_t) s];

        g.setColour (isActive ? colour : juce::Colours::darkgrey);
        g.fillEllipse (badge);
        g.setColour (juce::Colours::black);
        g.drawText (juce::String (s + 1), badge, juce::Justification::centred);

        if (isActive)
        {
            // Percentage label on the inside of the corner, so it never leaves the pad.
            const auto inward = juce::Point<float> (s % 2 == 0 ? 1.0f : -1.0f, s < 2 ? 1.0f : -1.0f);
            const auto labelCentre = corner + inward * 30.0f;
            g.setColour (colour);
            g.drawText (juce::String (juce::roundToInt (weights[(size_t) s] * 100.0f)) + "%",
                        juce::Rectangle<float> (44.0f, 16.0f).withCentre (labelCentre), juce::Justification::centred);
        }
    }

    g.setColour (juce::Colours::white);
    g.fillEllipse (juce::Rectangle<float> (14.0f, 14.0f).withCentre (puckScreen));
    g.setColour (juce::Colours::cyan);
    g.drawEllipse (juce::Rectangle<float> (14.0f, 14.0f).withCentre (puckScreen), 2.0f);
}

void BlendPadComponent::setPuckFromMouse (juce::Point<float> position)
{
    const auto pad = getPadArea();
    const auto x = juce::jlimit (0.0f, 1.0f, (position.x - pad.getX()) / pad.getWidth());
    const auto y = juce::jlimit (0.0f, 1.0f, (position.y - pad.getY()) / pad.getHeight());
    xParam->setValueNotifyingHost (xParam->convertTo0to1 (x));
    yParam->setValueNotifyingHost (yParam->convertTo0to1 (y));
    repaint();
}

void BlendPadComponent::mouseDown (const juce::MouseEvent& e)
{
    if (! getPadArea().expanded (14.0f).contains (e.position))
        return;
    dragging = true;
    xParam->beginChangeGesture();
    yParam->beginChangeGesture();
    setPuckFromMouse (e.position);
}

void BlendPadComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging)
        setPuckFromMouse (e.position);
}

void BlendPadComponent::mouseUp (const juce::MouseEvent&)
{
    if (! dragging)
        return;
    dragging = false;
    xParam->endChangeGesture();
    yParam->endChangeGesture();
}

void BlendPadComponent::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! getPadArea().expanded (14.0f).contains (e.position))
        return;
    xParam->beginChangeGesture();
    yParam->beginChangeGesture();
    xParam->setValueNotifyingHost (xParam->convertTo0to1 (0.5f));
    yParam->setValueNotifyingHost (yParam->convertTo0to1 (0.5f));
    xParam->endChangeGesture();
    yParam->endChangeGesture();
    repaint();
}
