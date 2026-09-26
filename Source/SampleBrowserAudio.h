#pragma once
#include <JuceHeader.h>
#include <functional>

// Decoded on a worker, published on the message thread. Never reads a file in render().
struct BrowserSample
{
    std::unique_ptr<juce::AudioBuffer<float>> audio;
    double sampleRate = 0.0;
    float trimStart = 0.0f, trimEnd = 1.0f;
    juce::String error;
};
BrowserSample decodeBrowserSample(const juce::File&, const std::function<bool()>& cancelled = {});
bool isBrowserAudioFile(const juce::File&);

// A separate audition voice: no PadData, MIDI, FX, variation cursor, or kit state.
class SampleBrowserPreview
{
public:
    void start(BrowserSample&&);
    void stop();
    void stopImmediately() noexcept { playing.store(false); }
    void setGain(float value);
    bool isPlaying() const noexcept { return playing.load(); }
    bool render(juce::AudioBuffer<float>& output, double outputRate);
private:
    juce::SpinLock lock;
    std::unique_ptr<juce::AudioBuffer<float>> audio;
    double sourceRate = 44100.0, position = 0.0;
    float smoothedGain = 0.0f;
    std::atomic<bool> stopping { false };
    std::atomic<float> gain { 0.5f };
    std::atomic<bool> playing { false };
};
