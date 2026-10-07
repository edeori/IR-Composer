#include "MasterSpectrumAnalyzer.h"
#include "PluginProcessor.h"
#include <algorithm>
#include <cmath>
#include <complex>

namespace
{
    constexpr float graphMinDb = -72.0f;
    constexpr float graphMaxDb = 12.0f;
    constexpr float releaseSmoothing = 0.76f;

    // Radically different, non-green hues so input/output are never confused at a
    // glance: input = electric blue, output = hot magenta. Matches the source EQ.
    const juce::Colour inputColour { 0xff2e86ff };
    const juce::Colour outputColour { 0xffff2e8c };
    const juce::Colour responseColour { 0xffffffff };
    const juce::Colour nodeColour { 0xffb9c2cc };
    const juce::Colour selectedNodeColour { 0xffffcc33 };
}

MasterSpectrumAnalyzer::MasterSpectrumAnalyzer (IRComposerAudioProcessor& owner) : processor (owner)
{
    setMouseCursor (juce::MouseCursor::CrosshairCursor);
    startTimerHz (30);
}

MasterSpectrumAnalyzer::~MasterSpectrumAnalyzer() { stopTimer(); }

juce::Rectangle<float> MasterSpectrumAnalyzer::graphBounds() const
{
    auto bounds = getLocalBounds().toFloat();
    bounds.removeFromTop (10.0f);
    bounds.removeFromBottom (22.0f);
    bounds.removeFromLeft (28.0f);
    bounds.removeFromRight (38.0f);
    return bounds;
}

float MasterSpectrumAnalyzer::frequencyToX (float frequency) const
{
    const auto bounds = graphBounds();
    const auto proportion = std::log (juce::jlimit (IRComposerConstants::minFrequencyHz,
                                                     IRComposerConstants::maxFrequencyHz, frequency)
                                       / IRComposerConstants::minFrequencyHz)
                             / std::log (IRComposerConstants::maxFrequencyHz / IRComposerConstants::minFrequencyHz);
    return bounds.getX() + proportion * bounds.getWidth();
}

float MasterSpectrumAnalyzer::xToFrequency (float x) const
{
    const auto bounds = graphBounds();
    const auto proportion = juce::jlimit (0.0f, 1.0f, (x - bounds.getX()) / bounds.getWidth());
    return IRComposerConstants::minFrequencyHz
           * std::pow (IRComposerConstants::maxFrequencyHz / IRComposerConstants::minFrequencyHz, proportion);
}

float MasterSpectrumAnalyzer::spectrumDbToY (float db) const
{
    return juce::jmap (juce::jlimit (graphMinDb, graphMaxDb, db), graphMinDb, graphMaxDb,
                       graphBounds().getBottom(), graphBounds().getY());
}

float MasterSpectrumAnalyzer::gainToY (float gain) const
{
    return juce::jmap (juce::jlimit (-gainRangeDb, gainRangeDb, gain), -gainRangeDb, gainRangeDb,
                       graphBounds().getBottom(), graphBounds().getY());
}

float MasterSpectrumAnalyzer::yToGain (float y) const
{
    return juce::jlimit (-gainRangeDb, gainRangeDb,
                         juce::jmap (y, graphBounds().getBottom(), graphBounds().getY(), -gainRangeDb, gainRangeDb));
}

void MasterSpectrumAnalyzer::setGainRangeDb (float newRangeDb) noexcept
{
    gainRangeDb = juce::jlimit (1.0f, 48.0f, newRangeDb);
    repaint();
}

juce::String MasterSpectrumAnalyzer::formatFrequency (float frequency)
{
    return frequency >= 1000.0f ? juce::String (frequency / 1000.0f, 2) + " kHz"
                                : juce::String (juce::roundToInt (frequency)) + " Hz";
}

int MasterSpectrumAnalyzer::bandNodeNear (float x, float y) const
{
    int nearest = -1;
    auto distance = 12.0f;
    const auto bands = processor.getMasterBandParams();
    for (int b = 0; b < IRComposerConstants::numMasterBands; ++b)
    {
        if (! bands[(size_t) b].active)
            continue;
        const auto nodeX = frequencyToX (bands[(size_t) b].frequencyHz);
        const auto nodeY = gainToY (bands[(size_t) b].gainDb);
        const auto candidate = std::hypot (nodeX - x, nodeY - y);
        if (candidate < distance) { nearest = b; distance = candidate; }
    }
    return nearest;
}

int MasterSpectrumAnalyzer::firstInactiveBand() const
{
    const auto bands = processor.getMasterBandParams();
    for (int b = 0; b < IRComposerConstants::numMasterBands; ++b)
        if (! bands[(size_t) b].active)
            return b;
    return -1; // all numMasterBands slots already in use
}

void MasterSpectrumAnalyzer::mouseDown (const juce::MouseEvent& event)
{
    const auto hitBand = bandNodeNear (event.position.x, event.position.y);
    freqGestureStarted = false;
    gainGestureStarted = false;

    // Ctrl-click / a trackpad secondary-click gesture (JUCE reports both as
    // isPopupMenu(), not isCtrlDown() -- macOS reinterprets a Control-click as a
    // right-click at the OS level) locks the drag to gain only, for precise gain
    // nudges without also smearing frequency sideways.
    draggedBand = hitBand;
    draggingGain = draggedBand >= 0;
    verticalOnlyDrag = event.mods.isPopupMenu();
    if (draggedBand >= 0)
        processor.setSelectedMasterBand (draggedBand);
    repaint();
}

void MasterSpectrumAnalyzer::mouseDrag (const juce::MouseEvent& event)
{
    if (draggedBand < 0 || ! draggingGain)
        return;

    auto& apvts = processor.apvts;
    if (! verticalOnlyDrag)
    {
        if (auto* freqParam = apvts.getParameter (ParamIDs::masterBandFreq (draggedBand)))
        {
            if (! freqGestureStarted) { freqParam->beginChangeGesture(); freqGestureStarted = true; }
            freqParam->setValueNotifyingHost (apvts.getParameterRange (freqParam->getParameterID())
                                                  .convertTo0to1 (xToFrequency (event.position.x)));
        }
    }
    if (auto* gainParam = apvts.getParameter (ParamIDs::masterBandGain (draggedBand)))
    {
        if (! gainGestureStarted) { gainParam->beginChangeGesture(); gainGestureStarted = true; }
        gainParam->setValueNotifyingHost (apvts.getParameterRange (gainParam->getParameterID())
                                              .convertTo0to1 (yToGain (event.position.y)));
    }
    repaint();
}

void MasterSpectrumAnalyzer::mouseUp (const juce::MouseEvent&)
{
    auto& apvts = processor.apvts;
    if (freqGestureStarted)
        if (auto* freqParam = apvts.getParameter (ParamIDs::masterBandFreq (draggedBand)))
            freqParam->endChangeGesture();
    if (gainGestureStarted)
        if (auto* gainParam = apvts.getParameter (ParamIDs::masterBandGain (draggedBand)))
            gainParam->endChangeGesture();
    draggedBand = -1;
    draggingGain = false;
    verticalOnlyDrag = false;
    freqGestureStarted = false;
    gainGestureStarted = false;
    repaint();
}

void MasterSpectrumAnalyzer::mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    // Scrolling directly over a node adjusts that band's Q; scrolling anywhere else on
    // the graph adjusts the currently-selected band's Q instead.
    auto band = bandNodeNear (event.position.x, event.position.y);
    if (band < 0)
        band = processor.getSelectedMasterBand();
    if (band < 0 || wheel.deltaY == 0.0f)
        return;

    auto& apvts = processor.apvts;
    if (auto* qParam = apvts.getParameter (ParamIDs::masterBandQ (band)))
    {
        const auto range = apvts.getParameterRange (qParam->getParameterID());
        const auto currentQ = processor.getMasterBandParams()[(size_t) band].q;
        // Multiplicative step so the same scroll gesture feels equally sized across
        // the whole (skewed) Q range, not just near one end of it.
        const auto factor = std::exp (wheel.deltaY * 2.0f);
        const auto newQ = juce::jlimit (range.start, range.end, currentQ * factor);
        qParam->beginChangeGesture();
        qParam->setValueNotifyingHost (range.convertTo0to1 (newQ));
        qParam->endChangeGesture();
    }
    repaint();
}

void MasterSpectrumAnalyzer::mouseDoubleClick (const juce::MouseEvent& event)
{
    auto& apvts = processor.apvts;

    if (const auto band = bandNodeNear (event.position.x, event.position.y); band >= 0)
    {
        // Stands in for "remove" in a dynamic band list -- deactivates this fixed slot.
        if (auto* activeParam = apvts.getParameter (ParamIDs::masterBandActive (band)))
        {
            activeParam->beginChangeGesture();
            activeParam->setValueNotifyingHost (0.0f);
            activeParam->endChangeGesture();
        }
    }
    else if (graphBounds().contains (event.position))
    {
        // Stands in for "add" -- activates the first free fixed slot at the clicked
        // frequency, or does nothing if all numMasterBands slots are already in use.
        const auto freeBand = firstInactiveBand();
        if (freeBand < 0)
            return;

        if (auto* freqParam = apvts.getParameter (ParamIDs::masterBandFreq (freeBand)))
        {
            freqParam->beginChangeGesture();
            freqParam->setValueNotifyingHost (apvts.getParameterRange (freqParam->getParameterID())
                                                  .convertTo0to1 (xToFrequency (event.position.x)));
            freqParam->endChangeGesture();
        }
        if (auto* activeParam = apvts.getParameter (ParamIDs::masterBandActive (freeBand)))
        {
            activeParam->beginChangeGesture();
            activeParam->setValueNotifyingHost (1.0f);
            activeParam->endChangeGesture();
        }
        processor.setSelectedMasterBand (freeBand);
    }
    repaint();
}

void MasterSpectrumAnalyzer::timerCallback()
{
    if (processor.getInputSpectrumFifo().pullLatestBlock (inputSamples))
        updatePath (inputSamples, inputPath, inputMagnitudes);
    if (processor.getOutputSpectrumFifo().pullLatestBlock (outputSamples))
        updatePath (outputSamples, outputPath, outputMagnitudes);
    updateResponseCurve();
    repaint();
}

void MasterSpectrumAnalyzer::updatePath (const std::array<float, SpectrumFifo::fftSize>& samples, juce::Path& path,
                                          std::array<float, SpectrumFifo::fftSize / 2>& magnitudes)
{
    std::fill (fftData.begin(), fftData.end(), 0.0f);
    std::copy (samples.begin(), samples.end(), fftData.begin());
    window.multiplyWithWindowingTable (fftData.data(), (size_t) SpectrumFifo::fftSize);
    fft.performFrequencyOnlyForwardTransform (fftData.data());

    for (int bin = 0; bin < SpectrumFifo::fftSize / 2; ++bin)
    {
        const auto value = fftData[(size_t) bin] * 2.0f / (float) SpectrumFifo::fftSize;
        magnitudes[(size_t) bin] = value > magnitudes[(size_t) bin]
                                     ? value : magnitudes[(size_t) bin] * releaseSmoothing;
    }

    const auto bounds = graphBounds();
    const auto sampleRate = processor.getSampleRate() > 0.0 ? processor.getSampleRate() : 44100.0;
    path.clear();
    for (int pixel = 0; pixel <= (int) bounds.getWidth(); ++pixel)
    {
        const auto x = bounds.getX() + (float) pixel;
        const auto freqLo = xToFrequency (x - 0.5f);
        const auto freqHi = xToFrequency (x + 0.5f);
        const auto maxBin = SpectrumFifo::fftSize / 2 - 1;
        const auto binPosLo = freqLo * (float) SpectrumFifo::fftSize / (float) sampleRate;
        const auto binPosHi = freqHi * (float) SpectrumFifo::fftSize / (float) sampleRate;

        float value;
        if (binPosHi - binPosLo >= 1.0f)
        {
            auto binLo = juce::jlimit (0, maxBin, juce::roundToInt (binPosLo));
            auto binHi = juce::jlimit (0, maxBin, juce::roundToInt (binPosHi));
            if (binHi < binLo)
                std::swap (binLo, binHi);
            float sum = 0.0f;
            for (int bin = binLo; bin <= binHi; ++bin)
                sum += magnitudes[(size_t) bin];
            value = sum / (float) (binHi - binLo + 1);
        }
        else
        {
            const auto binPos = juce::jlimit (0.0f, (float) maxBin, (binPosLo + binPosHi) * 0.5f);
            const auto binLo = (int) binPos;
            const auto binHi = juce::jmin (maxBin, binLo + 1);
            const auto frac = binPos - (float) binLo;
            value = magnitudes[(size_t) binLo] + (magnitudes[(size_t) binHi] - magnitudes[(size_t) binLo]) * frac;
        }

        const auto db = juce::Decibels::gainToDecibels (value, graphMinDb);
        const auto y = juce::jmap (juce::jlimit (graphMinDb, graphMaxDb, db), graphMinDb, graphMaxDb,
                                   bounds.getBottom(), bounds.getY());
        if (pixel == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
    }
}

// Standard RBJ Audio EQ Cookbook analytic magnitude response, evaluated purely for the
// on-screen curve -- the audio path itself always runs through SlopeFilter/SVFilter
// (see LiveMasterEQ/MasterEQ), not this. Ported verbatim from the source EQ's
// SpectrumAnalyzer::bandMagnitudeResponseDb (minus the dynamic-EQ gain-reduction term,
// which doesn't exist in MasterEQ::BandParams).
float MasterSpectrumAnalyzer::bandMagnitudeResponseDb (const MasterEQ::BandParams& p, float frequencyHz) const
{
    const auto sampleRate = processor.getSampleRate() > 0.0 ? processor.getSampleRate() : 44100.0;
    const auto w0 = juce::MathConstants<double>::twoPi * (double) p.frequencyHz / sampleRate;
    const auto q = juce::jmax (0.05, (double) p.q);
    const auto alpha = std::sin (w0) / (2.0 * q);
    const auto cosw0 = std::cos (w0);
    const auto A = std::pow (10.0, (double) p.gainDb / 40.0);
    const auto sqrtA = std::sqrt (A);
    const auto shelfSlope = juce::jlimit (0.1, 1.0, q / 2.0);
    const auto alphaShelf = std::sin (w0) / 2.0 * std::sqrt ((A + 1.0 / A) * (1.0 / shelfSlope - 1.0) + 2.0);

    double b0, b1, b2, a0, a1, a2;
    switch (p.type)
    {
        case FilterType::LowShelf:
            b0 = A * ((A + 1) - (A - 1) * cosw0 + 2 * sqrtA * alphaShelf);
            b1 = 2 * A * ((A - 1) - (A + 1) * cosw0);
            b2 = A * ((A + 1) - (A - 1) * cosw0 - 2 * sqrtA * alphaShelf);
            a0 = (A + 1) + (A - 1) * cosw0 + 2 * sqrtA * alphaShelf;
            a1 = -2 * ((A - 1) + (A + 1) * cosw0);
            a2 = (A + 1) + (A - 1) * cosw0 - 2 * sqrtA * alphaShelf;
            break;
        case FilterType::HighShelf:
            b0 = A * ((A + 1) + (A - 1) * cosw0 + 2 * sqrtA * alphaShelf);
            b1 = -2 * A * ((A - 1) + (A + 1) * cosw0);
            b2 = A * ((A + 1) + (A - 1) * cosw0 - 2 * sqrtA * alphaShelf);
            a0 = (A + 1) - (A - 1) * cosw0 + 2 * sqrtA * alphaShelf;
            a1 = 2 * ((A - 1) - (A + 1) * cosw0);
            a2 = (A + 1) - (A - 1) * cosw0 - 2 * sqrtA * alphaShelf;
            break;
        case FilterType::HighPass:
            b0 = (1 + cosw0) / 2; b1 = -(1 + cosw0); b2 = (1 + cosw0) / 2;
            a0 = 1 + alpha; a1 = -2 * cosw0; a2 = 1 - alpha;
            break;
        case FilterType::LowPass:
            b0 = (1 - cosw0) / 2; b1 = 1 - cosw0; b2 = (1 - cosw0) / 2;
            a0 = 1 + alpha; a1 = -2 * cosw0; a2 = 1 - alpha;
            break;
        case FilterType::Bell:
        default:
            b0 = 1 + alpha * A; b1 = -2 * cosw0; b2 = 1 - alpha * A;
            a0 = 1 + alpha / A; a1 = -2 * cosw0; a2 = 1 - alpha / A;
            break;
    }
    b0 /= a0; b1 /= a0; b2 /= a0; a1 /= a0; a2 /= a0;

    const auto w = juce::MathConstants<double>::twoPi * (double) frequencyHz / sampleRate;
    const std::complex<double> z = std::polar (1.0, -w);
    const auto num = b0 + b1 * z + b2 * z * z;
    const auto den = 1.0 + a1 * z + a2 * z * z;
    const auto magnitude = std::abs (num / den);
    auto db = 20.0f * (float) std::log10 (juce::jmax (1.0e-6, magnitude));

    // The formula above is inherently a single 2-pole (12dB/oct) section; scaling its
    // dB deviation from 0 by an order-derived multiplier approximates the selected
    // slope (6/18/24 dB/oct) for the on-screen curve -- same approximation the source
    // EQ's SpectrumAnalyzer uses, not an exact match to SlopeFilter's real cascade.
    if (p.type == FilterType::HighPass || p.type == FilterType::LowPass)
    {
        const auto orderMultiplier = p.slope == FilterSlope::Slope6 ? 0.5f
                                    : p.slope == FilterSlope::Slope18 ? 1.5f
                                    : p.slope == FilterSlope::Slope24 ? 2.0f : 1.0f;
        db *= orderMultiplier;
    }
    return db;
}

float MasterSpectrumAnalyzer::summedMagnitudeResponseDb (float frequencyHz) const
{
    float totalDb = 0.0f;
    const auto bands = processor.getMasterBandParams();
    for (int b = 0; b < IRComposerConstants::numMasterBands; ++b)
        if (bands[(size_t) b].active)
            totalDb += bandMagnitudeResponseDb (bands[(size_t) b], frequencyHz);
    return totalDb;
}

void MasterSpectrumAnalyzer::updateResponseCurve()
{
    const auto bounds = graphBounds();
    responseCurve.clear();
    for (int pixel = 0; pixel <= (int) bounds.getWidth(); ++pixel)
    {
        const auto x = bounds.getX() + (float) pixel;
        const auto freq = xToFrequency (x);
        const auto db = summedMagnitudeResponseDb (freq);
        const auto y = gainToY (db);
        if (pixel == 0) responseCurve.startNewSubPath (x, y); else responseCurve.lineTo (x, y);
    }
}

void MasterSpectrumAnalyzer::paint (juce::Graphics& g)
{
    const auto bounds = graphBounds();
    g.setColour (juce::Colour (0xff0b0d10));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);

    static constexpr std::array<float, 10> frequencies { 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 };
    for (auto frequency : frequencies)
    {
        const auto x = frequencyToX (frequency);
        g.setColour (juce::Colour (0xff23272c));
        g.drawVerticalLine (juce::roundToInt (x), bounds.getY(), bounds.getBottom());
        g.setColour (juce::Colour (0xff777f87));
        g.setFont (10.0f);
        g.drawText (formatFrequency (frequency), juce::Rectangle<float> (x - 28.0f, bounds.getBottom() + 3.0f, 56.0f, 15.0f),
                    juce::Justification::centred);
    }
    for (int db = -60; db <= 12; db += 12)
    {
        const auto y = spectrumDbToY ((float) db);
        g.setColour (juce::Colour (0xff1c1f23));
        g.drawHorizontalLine (juce::roundToInt (y), bounds.getX(), bounds.getRight());
        g.setColour (juce::Colour (0xff777f87));
        g.setFont (10.0f);
        const auto label = db > 0 ? "+" + juce::String (db) : juce::String (db);
        g.drawText (label, juce::Rectangle<float> (0.0f, y - 7.0f, bounds.getX() - 5.0f, 14.0f),
                    juce::Justification::centredRight);
    }

    for (int i = -2; i <= 2; ++i)
    {
        const auto gainDb = gainRangeDb * (float) i * 0.5f;
        const auto y = gainToY (gainDb);
        g.setColour (i == 0 ? selectedNodeColour.withAlpha (0.8f) : juce::Colour (0xff3a3f45).withAlpha (0.75f));
        g.drawLine (bounds.getRight(), y, bounds.getRight() + 4.0f, y, 1.0f);
        g.setColour (i == 0 ? juce::Colour (0xffe4e8ec) : juce::Colour (0xff777f87));
        g.setFont (9.5f);
        const auto label = gainDb > 0.0f ? "+" + juce::String (gainDb, 1) : juce::String (gainDb, 1);
        g.drawText (label, juce::Rectangle<float> (bounds.getRight() + 5.0f, y - 7.0f, 32.0f, 14.0f),
                    juce::Justification::centredLeft);
    }

    g.setColour (inputColour.withAlpha (0.55f));
    g.strokePath (inputPath, juce::PathStrokeType (1.2f));
    g.setColour (outputColour.withAlpha (0.9f));
    g.strokePath (outputPath, juce::PathStrokeType (1.6f));
    g.setColour (responseColour.withAlpha (0.55f));
    g.strokePath (responseCurve, juce::PathStrokeType (1.2f));

    const auto selected = processor.getSelectedMasterBand();
    const auto bands = processor.getMasterBandParams();
    for (int b = 0; b < IRComposerConstants::numMasterBands; ++b)
    {
        if (! bands[(size_t) b].active)
            continue;
        const auto x = frequencyToX (bands[(size_t) b].frequencyHz);
        const auto y = gainToY (bands[(size_t) b].gainDb);
        const auto colour = b == selected ? selectedNodeColour : nodeColour;
        g.setColour (colour.withAlpha (0.35f));
        g.drawVerticalLine (juce::roundToInt (x), bounds.getY(), bounds.getBottom());
        g.setColour (colour);
        g.drawEllipse (juce::Rectangle<float> (11.0f, 11.0f).withCentre ({ x, y }), 1.6f);
        if (b == selected)
            g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre ({ x, y }));
    }

    g.setColour (juce::Colour (0xff2a2e33));
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 6.0f, 1.0f);
}
