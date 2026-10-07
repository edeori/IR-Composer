#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>
#include <functional>
#include <atomic>
#include <memory>

// Loads an IR audio file off the message thread. registerBasicFormats() covers WAV,
// AIFF, FLAC and OGG. The callback always fires on the message thread via
// juce::MessageManager::callAsync, regardless of which thread pool job thread actually
// did the read, so callers (IRSlot::setRawBuffer et al.) never need their own thread
// safety for the handoff.
class IRFileLoader
{
public:
    IRFileLoader();
    ~IRFileLoader();
    void shutdown();

    using Callback = std::function<void (bool success, juce::AudioBuffer<float> buffer, double sampleRate)>;
    void loadAsync (const juce::File& file, Callback onComplete);

private:
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
    juce::AudioFormatManager formatManager;
    juce::ThreadPool threadPool { 2 };
};
