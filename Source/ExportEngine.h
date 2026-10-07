#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <functional>

class IRComposerAudioProcessor;

struct ExportSettings
{
    enum class Format { wav, aiff };

    Format format = Format::wav;
    int bitDepth = 24; // 16/24-bit PCM; 32-bit float is WAV-only
    double sampleRate = 0.0; // 0 = same as the live/project sample rate
    bool normalize = true;
    float fadeOutMs = 5.0f;
};

// Renders the current blended (crop+phase per slot -> sum -> MasterEQ -> normalize)
// master IR to a file, independent of BlendEngine's live-audition path so an export in
// progress never contends with the audio thread's convolution reloads (see the plan's
// threading table). Runs entirely on its own background thread.
class ExportEngine
{
public:
    ~ExportEngine() { pool.removeAllJobs (true, -1); }

    using CompletionCallback = std::function<void (bool success, juce::String errorMessage)>;

    void exportAsync (IRComposerAudioProcessor& processor, const ExportSettings& settings,
                       const juce::File& destination, CompletionCallback onComplete);

private:
    juce::ThreadPool pool { 1 };
};
