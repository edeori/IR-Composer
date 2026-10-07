#include "../Source/PluginProcessor.h"
#include "../Source/IRLevel.h"
#include <iostream>

// Offline integration driver: its input is rendered by the real WITCHMVRK VST3.
// The live processor output is compared against an independent FFT convolution
// with the exported kernel by WitchmvrkChainTest.py.
int renderChain (int argc, char** argv)
{
    if ((argc != 8 && argc != 9) || juce::String (argv[1]) != "--chain-render") return 2;
    juce::ScopedJuceInitialiser_GUI gui;
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (juce::File (argv[2])));
    if (! reader || reader->lengthInSamples > 10000000) return 3;
    const auto rate = reader->sampleRate;
    const int blockSize = juce::jlimit (1, 8192, juce::String (argv[7]).getIntValue());
    IRComposerAudioProcessor processor;
    processor.setRateAndBufferSizeDetails (rate, blockSize);
    processor.prepareToPlay (rate, blockSize);
    // "auto" exercises the actual fresh-load length, including late capture
    // artefacts that the original fixed 250 ms test could silently exclude.
    if (argc == 8 || juce::String (argv[8]) != "auto")
        processor.setCombineLengthSamples ((juce::int64) (rate * 0.25));
    int loaded = 0;
    const int slots = juce::String (argv[6]) == "-" ? 1 : 2;
    processor.onSlotLoaded = [&] (int) { ++loaded; };
    // Load sequentially so slot 0 is the alignment reference in both runs.
    for (int slot = 0; slot < slots; ++slot)
    {
        processor.loadIRFile (slot, juce::File (argv[5 + slot]));
        const auto deadline = juce::Time::getMillisecondCounterHiRes() + 10000;
        while (loaded <= slot && juce::Time::getMillisecondCounterHiRes() < deadline)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
        if (loaded <= slot) return 4;
    }
    const auto length = (int) processor.getCombineLengthSamples();
    juce::AudioBuffer<float> kernel (2, length);
    kernel.clear();
    const auto gains = processor.getSlotMixGains();
    for (int slot = 0; slot < slots; ++slot)
    {
        auto params = processor.getSlotRenderParams (slot);
        params.gainDb = 0;
        params.polarityInverted = false;
        const auto& ir = processor.getIRSlot (slot);
        auto h = ir.renderProcessed (length, params);
        const auto level = IRLevel::computeNormalisationGain (ir.renderProcessed (length, {}), rate);
        for (int ch = 0; ch < 2; ++ch)
            kernel.addFrom (ch, 0, h, ch, 0, length, gains[(size_t) slot] * level);
        std::cout << "slot=" << slot << " crop=" << ir.getCropStartSample()
                  << " delay_ms=" << params.delayMs << " gain=" << gains[(size_t) slot] << '\n';
    }

    juce::MidiBuffer midi;
    juce::AudioBuffer<float> block (2, blockSize);
    // Allow both asynchronous kernel builders to settle, then clear their tails
    // by processing silence. Do not reset/re-prepare between warm-up and the take.
    for (int n = 0; n < juce::jmax (300, (int) rate / blockSize); ++n)
    {
        block.clear();
        processor.processBlock (block, midi);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (1);
    }
    const int samples = (int) reader->lengthInSamples;
    juce::AudioBuffer<float> input (2, samples), output (2, samples);
    if (! reader->read (&input, 0, samples, 0, true, true)) return 5;
    for (int offset = 0; offset < samples; offset += blockSize)
    {
        const auto count = juce::jmin (blockSize, samples - offset);
        for (int ch = 0; ch < 2; ++ch) block.copyFrom (ch, 0, input, ch, offset, count);
        juce::AudioBuffer<float> view (block.getArrayOfWritePointers(), 2, count);
        processor.processBlock (view, midi);
        for (int ch = 0; ch < 2; ++ch) output.copyFrom (ch, offset, block, ch, 0, count);
    }
    const auto write = [&] (const char* path, const juce::AudioBuffer<float>& audio)
    {
        juce::File file (path);
        if (file.exists()) return false; // keep previous measurement artifacts
        juce::WavAudioFormat wav;
        auto stream = file.createOutputStream();
        if (! stream) return false;
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), rate, 2, 32, {}, 0));
        if (! writer) return false;
        stream.release();
        return writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
    };
    return write (argv[3], output) && write (argv[4], kernel) ? 0 : 6;
}
