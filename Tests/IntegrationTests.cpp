#include "../Source/PluginProcessor.h"
#include "../Source/ExportEngine.h"
#include "../Source/IRLevel.h"
#include <iostream>

namespace
{
int failures = 0;
void check (bool ok, const char* name)
{
    std::cout << (ok ? "  ok: " : "FAILED: ") << name << std::endl;
    if (! ok) ++failures;
}

bool waitFor (const std::function<bool()>& done)
{
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 5000.0;
    while (! done() && juce::Time::getMillisecondCounterHiRes() < deadline)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
    return done();
}

void testLongKernelAcrossVariableBlocks()
{
    constexpr double rate = 48000.0;
    constexpr int length = 16000;
    juce::AudioBuffer<float> ir (2, length);
    juce::Random random (12345);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < length; ++i)
            ir.setSample (ch, i, i == 0 ? 1.0f
                : 0.02f * (2.0f * random.nextFloat() - 1.0f) * std::exp (-i / 3000.0f));

    juce::TemporaryFile file (".wav");
    {
        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatWriter> writer (format.createWriterFor (
            file.getFile().createOutputStream().release(), rate, 2, 32, {}, 0));
        if (! writer || ! writer->writeFromAudioSampleBuffer (ir, 0, length))
        {
            check (false, "create long stereo IR fixture");
            return;
        }
    }
    for (int blockSize : { 64, 128, 512 })
    {
        IRComposerAudioProcessor processor;
        processor.setRateAndBufferSizeDetails (rate, blockSize);
        processor.prepareToPlay (rate, blockSize);
        processor.setCombineLengthSamples (length);
        bool loaded = false;
        processor.onSlotLoaded = [&] (int) { loaded = true; };
        processor.loadIRFile (0, file.getFile(), 0, length, false);
        check (waitFor ([&] { return loaded; }), "long stereo IR loads");
        auto expected = processor.getIRSlot (0).renderProcessed (length, {});
        expected.applyGain (0.25f * IRLevel::computeNormalisationGain (expected, rate));
        juce::MidiBuffer midi;
        juce::AudioBuffer<float> buffer (2, blockSize);
        for (int n = 0; n < 300; ++n)
        {
            buffer.clear();
            processor.processBlock (buffer, midi);
            juce::Thread::sleep (1);
        }
        const std::array<int, 5> sizes { 1, 17, blockSize / 2, blockSize - 1, blockSize };
        float peakError = 0.0f;
        int offset = 0;
        for (int n = 0; offset < 2 * length; ++n)
        {
            const auto count = juce::jmin (sizes[(size_t) n % sizes.size()], 2 * length - offset);
            buffer.clear();
            if (offset == 0)
                for (int ch = 0; ch < 2; ++ch) buffer.setSample (ch, 0, 0.25f);
            juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), 2, count);
            processor.processBlock (view, midi);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < count; ++i)
                    peakError = juce::jmax (peakError, std::abs (view.getSample (ch, i)
                        - (offset + i < length ? expected.getSample (ch, offset + i) : 0.0f)));
            offset += count;
        }
        std::cout << "    long IR, block=" << blockSize << ", error=" << peakError << std::endl;
        check (peakError < 1.0e-6f, "long stereo convolution matches the IR without an extra repeat");
    }
}

void testTwoIRsWithDifferentLeadingSilence (bool autoAlign)
{
    constexpr double rate = 48000.0;
    constexpr int onset = 7200; // 150 ms: beyond the correlation search window
    juce::TemporaryFile early (".wav"), late (".wav");
    for (int index = 0; index < 2; ++index)
    {
        juce::AudioBuffer<float> ir (2, 12000);
        ir.clear();
        for (int channel = 0; channel < 2; ++channel)
            ir.setSample (channel, index == 0 ? 0 : onset, 1.0f);
        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatWriter> writer (format.createWriterFor (
            (index == 0 ? early : late).getFile().createOutputStream().release(), rate, 2, 24, {}, 0));
        if (! writer || ! writer->writeFromAudioSampleBuffer (ir, 0, ir.getNumSamples()))
        {
            check (false, "create two-IR echo fixture");
            return;
        }
    }

    IRComposerAudioProcessor processor;
    processor.setRateAndBufferSizeDetails (rate, 128);
    processor.prepareToPlay (rate, 128);
    processor.setAutoAlignOnLoad (autoAlign);
    int loaded = 0;
    processor.onSlotLoaded = [&] (int) { ++loaded; };
    processor.loadIRFile (0, early.getFile());
    check (waitFor ([&] { return loaded == 1; }), "first echo-test IR loads");
    processor.loadIRFile (1, late.getFile());
    check (waitFor ([&] { return loaded == 2; }), "second echo-test IR loads");

    juce::MidiBuffer midi;
    juce::AudioBuffer<float> block (2, 128);
    for (int index = 0; index < 300; ++index)
    {
        block.clear();
        processor.processBlock (block, midi);
        juce::Thread::sleep (1);
    }
    float direct = 0.0f, echo = 0.0f;
    for (int offset = 0; offset < 24000; offset += 128)
    {
        block.clear();
        if (offset == 0)
            for (int channel = 0; channel < 2; ++channel)
                block.setSample (channel, 0, 0.25f);
        processor.processBlock (block, midi);
        if (offset == 0) direct = block.getSample (0, 0);
        for (int sample = (offset == 0 ? 1 : 0); sample < 128; ++sample)
            echo = juce::jmax (echo, std::abs (block.getSample (0, sample)));
    }
    std::cout << "    two IRs, auto-align " << autoAlign << ": direct=" << direct << ", echo=" << echo << std::endl;
    check (std::abs (direct - 0.25f) < 1.0e-4f && echo < 1.0e-6f,
           "two freshly loaded IRs sum without an echo from file-leading silence");

    processor.setSlotCropStartSample (1, 0); // explicitly retain the file's delay
    juce::MemoryBlock saved;
    processor.getStateInformation (saved);
    loaded = 0;
    processor.setStateInformation (saved.getData(), (int) saved.getSize());
    check (waitFor ([&] { return loaded == 2; }) && processor.getIRSlot (1).getCropStartSample() == 0,
           "session restore preserves an explicit zero crop instead of auto-trimming again");
}

void testCaptureEchoIsExcludedOnLoad (int slots)
{
    constexpr double rate = 48000.0;
    juce::TemporaryFile file (".wav");
    juce::AudioBuffer<float> ir (2, 3 * (int) rate);
    ir.clear();
    for (int ch = 0; ch < 2; ++ch)
    {
        ir.setSample (ch, 0, 1.0f);
        ir.setSample (ch, 120000, 0.2f); // disconnected capture artefact at 2.5 s
    }
    {
        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatWriter> writer (format.createWriterFor (
            file.getFile().createOutputStream().release(), rate, 2, 24, {}, 0));
        if (! writer || ! writer->writeFromAudioSampleBuffer (ir, 0, ir.getNumSamples()))
        {
            check (false, "create capture echo fixture");
            return;
        }
    }
    IRComposerAudioProcessor processor;
    processor.setRateAndBufferSizeDetails (rate, 128);
    processor.prepareToPlay (rate, 128);
    int loaded = 0;
    processor.onSlotLoaded = [&] (int) { ++loaded; };
    for (int slot = 0; slot < slots; ++slot)
    {
        processor.loadIRFile (slot, file.getFile());
        check (waitFor ([&] { return loaded == slot + 1; }), "capture echo fixture loads");
    }
    juce::AudioBuffer<float> block (2, 128);
    juce::MidiBuffer midi;
    for (int n = 0; n < 300; ++n)
    {
        block.clear();
        processor.processBlock (block, midi);
        juce::Thread::sleep (1);
    }
    float error = 0.0f;
    for (int offset = 0; offset < 3 * (int) rate; offset += 128)
    {
        block.clear();
        if (offset == 0)
            for (int ch = 0; ch < 2; ++ch) block.setSample (ch, 0, 0.25f);
        processor.processBlock (block, midi);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 128; ++i)
                error = juce::jmax (error, std::abs (block.getSample (ch, i)
                    - (offset + i == 0 ? 0.25f : 0.0f)));
    }
    check (error < 1.0e-6f, "fresh capture produces no late echo with one or four loaded slots");
}

} // namespace

int renderChain (int argc, char** argv);

int main (int argc, char** argv)
{
    if (argc > 1) return renderChain (argc, argv);
    juce::ScopedJuceInitialiser_GUI init;
    testLongKernelAcrossVariableBlocks();
    testCaptureEchoIsExcludedOnLoad (1);
    testCaptureEchoIsExcludedOnLoad (4);
    testTwoIRsWithDifferentLeadingSilence (true);
    testTwoIRsWithDifferentLeadingSilence (false);
    juce::TemporaryFile source (".wav");
    juce::AudioBuffer<float> audio (2, 10000);
    audio.clear();
    audio.setSample (0, 0, 1.0f);
    audio.setSample (1, 0, 1.0f);
    {
        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatWriter> writer (format.createWriterFor (
            source.getFile().createOutputStream().release(), 48000.0, 2, 24, {}, 0));
        if (! writer || ! writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples()))
            return 1;
    }

    juce::MemoryBlock emptyState;
    {
        IRComposerAudioProcessor empty;
        empty.getStateInformation (emptyState);
    }

    auto processor = std::make_unique<IRComposerAudioProcessor>();
    processor->setRateAndBufferSizeDetails (48000.0, 128);
    processor->prepareToPlay (48000.0, 128);
    bool loaded = false;
    processor->onSlotLoaded = [&] (int) { loaded = true; };
    processor->loadIRFile (0, source.getFile(), 5000, 9000, false);
    check (waitFor ([&] { return loaded; }), "IR load completes");
    check (processor->getIRSlot (0).getCropStartSample() == 5000
        && processor->getIRSlot (0).getCropEndSample() == 9000,
        "saved crop beyond auto-trimmed tail restores exactly");

    auto existingAppStateHandle = processor->getAppState();
    processor->setStateInformation (emptyState.getData(), (int) emptyState.getSize());
    check (existingAppStateHandle == processor->getAppState(), "state restore preserves UI settings bindings");
    check (! processor->getIRSlot (0).hasAudio(), "restoring an empty state clears old IR audio");
    check (processor->getSlotRequestedFile (0) == juce::File(), "empty state clears pending file path");

    loaded = false;
    processor->loadIRFile (0, source.getFile(), 0, -1, false);
    processor->setStateInformation (emptyState.getData(), (int) emptyState.getSize());
    juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
    check (! loaded && ! processor->getIRSlot (0).hasAudio(), "state restore cancels previous pending load");

    processor->loadIRFile (0, source.getFile(), 0, -1, false);
    check (waitFor ([&] { return loaded; }), "reload completes");
    processor->setCombineLengthSamples (processor->getIRSlot (0).getCropEndSample());
    for (bool aiff : { false, true })
    for (int depth : { 16, 24, 32 })
    for (int rate : { 44100, 88200, 96000 })
    {
        ExportEngine exporter;
        ExportSettings settings;
        settings.sampleRate = rate;
        settings.format = aiff ? ExportSettings::Format::aiff : ExportSettings::Format::wav;
        settings.bitDepth = depth;
        juce::TemporaryFile output (aiff ? ".aiff" : ".wav");
        const auto unsupported = aiff && depth == 32;
        if (unsupported)
            output.getFile().replaceWithText ("preserve existing file");
        bool completed = false, success = false;
        exporter.exportAsync (*processor, settings, output.getFile(),
            [&] (bool ok, juce::String) { success = ok; completed = true; });
        const auto finished = waitFor ([&] { return completed; });
        if (unsupported)
        {
            check (finished && ! success, "unsupported AIFF bit depth is rejected");
            check (output.getFile().loadFileAsString() == "preserve existing file", "invalid export preserves existing destination");
            continue;
        }
        check (finished && success, "export at another sample rate succeeds");
        std::unique_ptr<juce::AudioFormat> format;
        if (aiff) format = std::make_unique<juce::AiffAudioFormat>();
        else      format = std::make_unique<juce::WavAudioFormat>();
        std::unique_ptr<juce::AudioFormatReader> reader (format->createReaderFor (output.getFile().createInputStream().release(), true));
        check (reader && juce::approximatelyEqual (reader->sampleRate, (double) rate) && reader->lengthInSamples
            == (juce::int64) std::ceil (processor->getCombineLengthSamples() * rate / 48000.0),
            "export file has requested rate and duration");
        check (reader && reader->bitsPerSample == (unsigned int) depth
            && reader->usesFloatingPointData == (! aiff && depth == 32), "export encoding matches format and bit-depth label");
    }

    // Exercise the actual processor, not only the offline IR renderer: a unit IR
    // must never create a delayed second copy, including across short host blocks.
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> silence (2, 128);
    for (int block = 0; block < 300; ++block)
    {
        silence.clear();
        processor->processBlock (silence, midi);
        juce::Thread::sleep (1); // allow the asynchronous convolution kernel to arrive
    }
    float firstSample = 0.0f, repeatedPeak = 0.0f;
    int processed = 0;
    const std::array<int, 5> blockSizes { 1, 17, 64, 127, 128 };
    for (int block = 0; processed < 9600; ++block)
    {
        const auto count = juce::jmin (blockSizes[(size_t) block % blockSizes.size()], 9600 - processed);
        juce::AudioBuffer<float> signal (2, count);
        signal.clear();
        if (processed == 0)
            for (int channel = 0; channel < 2; ++channel)
                signal.setSample (channel, 0, 0.25f);
        processor->processBlock (signal, midi);
        for (int channel = 0; channel < 2; ++channel)
            for (int sample = 0; sample < count; ++sample)
                if (processed + sample == 0)
                    firstSample = signal.getSample (channel, sample);
                else
                    repeatedPeak = juce::jmax (repeatedPeak, std::abs (signal.getSample (channel, sample)));
        processed += count;
    }
    check (std::abs (firstSample - 0.25f) < 1.0e-4f, "live unit IR passes the original impulse at unity gain");
    check (repeatedPeak < 1.0e-6f, "live convolution creates no delayed repeat across variable host blocks");

    // Queue completion without pumping messages, then destroy its owner.
    bool staleCallback = false;
    {
        IRFileLoader loader;
        loader.loadAsync (source.getFile(), [&] (bool, juce::AudioBuffer<float>, double) { staleCallback = true; });
        juce::Thread::sleep (100);
    }
    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    check (! staleCallback, "queued file completion is cancelled after loader destruction");

    processor->loadIRFile (0, source.getFile());
    processor.reset();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    check (true, "processor destruction safely joins workers and cancels callbacks");
    return failures == 0 ? 0 : 1;
}
