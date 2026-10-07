#include "IRFileLoader.h"

IRFileLoader::IRFileLoader()
{
    formatManager.registerBasicFormats();
}

IRFileLoader::~IRFileLoader()
{
    shutdown();
}

void IRFileLoader::shutdown()
{
    alive->store (false);
    threadPool.removeAllJobs (true, -1);
}

void IRFileLoader::loadAsync (const juce::File& file, Callback onComplete)
{
    // A queued message may outlive both the loader and its processor.
    auto guardedCallback = [lifetime = alive, callback = std::move (onComplete)]
        (bool success, juce::AudioBuffer<float> buffer, double rate)
    {
        if (lifetime->load())
            callback (success, std::move (buffer), rate);
    };
    threadPool.addJob ([this, file, guardedCallback]
    {
        std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));

        if (reader == nullptr)
        {
            juce::MessageManager::callAsync ([guardedCallback]
            {
                guardedCallback (false, juce::AudioBuffer<float>(), 0.0);
            });
            return;
        }

        const auto numChannels = (int) juce::jmin ((juce::int64) 2, (juce::int64) reader->numChannels);
        const auto numSamples = (int) juce::jmin (reader->lengthInSamples, (juce::int64) std::numeric_limits<int>::max());

        juce::AudioBuffer<float> readBuffer (juce::jmax (1, numChannels), numSamples);
        const auto success = reader->read (&readBuffer, 0, numSamples, 0, true, true);

        const auto sampleRate = reader->sampleRate;

        juce::MessageManager::callAsync ([guardedCallback, movedBuffer = std::move (readBuffer), sampleRate, success]() mutable
        {
            guardedCallback (success, std::move (movedBuffer), sampleRate);
        });
    });
}
