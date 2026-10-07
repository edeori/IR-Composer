#include "ExportEngine.h"
#include "IRLevel.h"
#include "MasterEQ.h"
#include "Parameters.h"
#include "PluginProcessor.h"

namespace
{
    void applyFadeOut (juce::AudioBuffer<float>& buffer, int fadeSamples)
    {
        const auto numSamples = buffer.getNumSamples();
        fadeSamples = juce::jmin (fadeSamples, numSamples);
        if (fadeSamples <= 0)
            return;

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            auto* d = buffer.getWritePointer (ch);
            for (int i = 0; i < fadeSamples; ++i)
            {
                const auto gain = (float) (fadeSamples - i) / (float) fadeSamples;
                d[numSamples - fadeSamples + i] *= gain;
            }
        }
    }

    juce::AudioBuffer<float> resampleTo (const juce::AudioBuffer<float>& source, double sourceRate, double targetRate)
    {
        if (juce::approximatelyEqual (sourceRate, targetRate) || targetRate <= 0.0)
            return source;

        const auto ratio = sourceRate / targetRate;
        const auto numOutSamples = (int) std::ceil ((double) source.getNumSamples() / ratio);
        juce::AudioBuffer<float> out (source.getNumChannels(), juce::jmax (0, numOutSamples));

        for (int ch = 0; ch < source.getNumChannels(); ++ch)
        {
            juce::LagrangeInterpolator interpolator;
            interpolator.reset();
            interpolator.process (ratio, source.getReadPointer (ch), out.getWritePointer (ch), numOutSamples, source.getNumSamples(), 0);
        }
        return out;
    }
}

void ExportEngine::exportAsync (IRComposerAudioProcessor& processor, const ExportSettings& settings,
                                 const juce::File& destination, CompletionCallback onComplete)
{
    pool.addJob ([&processor, settings, destination, onComplete]
    {
        const auto targetLength = processor.getCombineLengthSamples();
        if (targetLength <= 0)
        {
            juce::MessageManager::callAsync ([onComplete] { onComplete (false, "Nothing to export -- load at least one IR first."); });
            return;
        }

        const auto liveSampleRate = processor.getSampleRate() > 0.0 ? processor.getSampleRate() : 44100.0;

        // Same mix law as the live audition (see PluginProcessor::processBlock and
        // BlendEngine): each slot level-matched on its own, then scaled by its blend-pad
        // weight x Gain x polarity -- so the exported IR sounds like what was heard.
        const auto mixGains = processor.getSlotMixGains();
        juce::AudioBuffer<float> sum (2, (int) targetLength);
        sum.clear();
        bool anySlotIncluded = false;
        for (int s = 0; s < IRComposerConstants::numSlots; ++s)
        {
            if (mixGains[(size_t) s] == 0.0f)
                continue;
            const auto& slot = processor.getIRSlot (s);
            const auto params = processor.getSlotRenderParams (s);
            IRSlot::RenderParams kernelParams;
            kernelParams.delayMs = params.delayMs;
            kernelParams.rotateDegrees = params.rotateDegrees;
            const auto rendered = slot.renderProcessed (targetLength, kernelParams);
            const auto levelMatch = IRLevel::computeNormalisationGain (slot.renderProcessed (targetLength, {}), liveSampleRate);
            for (int ch = 0; ch < 2; ++ch)
                sum.addFrom (ch, 0, rendered, ch, 0, rendered.getNumSamples(), mixGains[(size_t) s] * levelMatch);
            anySlotIncluded = true;
        }

        if (! anySlotIncluded)
        {
            juce::MessageManager::callAsync ([onComplete] { onComplete (false, "Nothing to export -- no active, loaded IR slots."); });
            return;
        }

        MasterEQ eq;
        eq.processBuffer (sum, liveSampleRate, processor.getMasterBandParams());

        if (settings.normalize)
        {
            const auto peak = juce::jmax (sum.getMagnitude (0, 0, sum.getNumSamples()),
                                           sum.getMagnitude (1, 0, sum.getNumSamples()));
            if (peak > 1.0e-6f)
                sum.applyGain (1.0f / peak);
        }

        applyFadeOut (sum, (int) (settings.fadeOutMs * 0.001f * (float) liveSampleRate));

        const auto exportSampleRate = settings.sampleRate > 0.0 ? settings.sampleRate : liveSampleRate;
        auto resampled = resampleTo (sum, liveSampleRate, exportSampleRate);

        std::unique_ptr<juce::AudioFormat> format;
        if (settings.format == ExportSettings::Format::wav)
            format = std::make_unique<juce::WavAudioFormat>();
        else
            format = std::make_unique<juce::AiffAudioFormat>();

        // Reject unsupported settings before touching an existing destination.
        if (! format->getPossibleBitDepths().contains (settings.bitDepth))
        {
            juce::MessageManager::callAsync ([onComplete] { onComplete (false, "This format/bit-depth combination isn't supported."); });
            return;
        }

        destination.deleteFile();
        std::unique_ptr<juce::FileOutputStream> outputStream (destination.createOutputStream());
        if (outputStream == nullptr)
        {
            juce::MessageManager::callAsync ([onComplete] { onComplete (false, "Could not create the destination file."); });
            return;
        }

        // JUCE supports 32-bit float WAV; AIFF exports support 16/24-bit PCM.
        std::unique_ptr<juce::AudioFormatWriter> writer (
            format->createWriterFor (outputStream.get(), exportSampleRate, (unsigned int) resampled.getNumChannels(),
                                      settings.bitDepth, {}, 0));

        if (writer == nullptr)
        {
            juce::MessageManager::callAsync ([onComplete] { onComplete (false, "This format/bit-depth combination isn't supported."); });
            return;
        }

        outputStream.release(); // writer now owns the stream

        const auto success = writer->writeFromAudioSampleBuffer (resampled, 0, resampled.getNumSamples());
        writer.reset();

        juce::MessageManager::callAsync ([onComplete, success]
        {
            onComplete (success, success ? juce::String() : "Failed to write the audio file.");
        });
    });
}
