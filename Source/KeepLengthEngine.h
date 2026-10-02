#pragma once

#include <JuceHeader.h>
#include <signalsmith-stretch/signalsmith-stretch.h>

#include <array>
#include <vector>

// One realtime pitch engine per DrumVoice. The source timeline always advances
// at its natural sample-rate ratio; only spectral pitch changes, so the trimmed
// duration is independent of Pitch automation.
class KeepLengthEngine
{
public:
    void prepare(double hostSampleRate, int maximumBlockSize);
    void reset() noexcept;

    void start(double startSample,
               double endSample,
               double sourceSamplesPerHostSample,
               bool reverse,
               float initialSemitones) noexcept;

    // Produces at most numSamples into the internal stereo scratch buffer.
    // The returned count can be smaller only when the fixed-length timeline ends.
    int process(const juce::AudioBuffer<float>& source,
                float targetSemitones,
                int numSamples) noexcept;

    const float* getLeft() const noexcept  { return outputScratch[0].data(); }
    const float* getRight() const noexcept { return outputScratch[1].data(); }
    const float* getDryLeft() const noexcept  { return dryScratch[0].data(); }
    const float* getDryRight() const noexcept { return dryScratch[1].data(); }
    int getScratchCapacity() const noexcept { return scratchCapacity; }
    int getOutputLength() const noexcept { return outputLength; }
    int getOutputPosition() const noexcept { return outputPosition; }
    int getAnalysisSeekLength() const noexcept { return seekLength; }
    bool isUsingShortWindow() const noexcept { return usingShortWindow; }
    double getSourcePosition() const noexcept;
    bool isPrepared() const noexcept { return prepared; }

private:
    using Stretch = signalsmith::stretch::SignalsmithStretch<float>;

    void initialiseFromSource(const juce::AudioBuffer<float>& source) noexcept;
    void findShortTransientPosition(const juce::AudioBuffer<float>& source) noexcept;
    void applyShortTransientGuard(int timelineStart, int count) noexcept;
    void fillTimeline(const juce::AudioBuffer<float>& source,
                      int timelineStart,
                      int count,
                      std::array<std::vector<float>, 2>& destination) noexcept;

    // Both instances are configured during prepare(), never on the audio
    // thread.  Short one-shots can therefore select the low-latency window at
    // note-on without allocating or reconfiguring a live stretcher.
    Stretch normalStretch { 0x41535445L }; // deterministic "ASTE" seed
    Stretch shortStretch  { 0x41535446L };
    Stretch* activeStretch { &normalStretch };
    std::array<std::vector<float>, 2> inputScratch;
    std::array<std::vector<float>, 2> outputScratch;
    std::array<std::vector<float>, 2> dryScratch;
    std::array<float*, 2> inputPointers {};
    std::array<float*, 2> outputPointers {};

    double startPosition { 0.0 };
    double endPosition { 1.0 };
    double sourceStep { 1.0 };
    double processingSampleRate { 48000.0 };
    bool reversed { false };
    bool prepared { false };
    bool needsInitialSeek { false };
    bool shortOutputCached { false };
    bool usingShortWindow { false };
    bool shortWindowCandidate { false };
    bool engineSelectionPending { false };
    int scratchCapacity { 0 };
    int normalSeekLength { 0 };
    int shortSeekLength { 0 };
    int seekLength { 0 };
    int inputPosition { 0 };
    int outputPosition { 0 };
    int outputLength { 0 };
    int processLength { 0 };
    int shortTransientPosition { 0 };
    float initialPitchSemitones { 0.0f };
};
