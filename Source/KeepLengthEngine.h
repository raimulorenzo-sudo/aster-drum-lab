#pragma once

#include <JuceHeader.h>
#include <signalsmith-stretch/signalsmith-stretch.h>

#include <array>
#include <vector>
#include <memory>

// Immutable, pre-rendered spectral output. Envelopes, gain, fades and FX are
// deliberately applied by DrumVoice afterwards, exactly as on the live path.
struct KeepLengthRender
{
    std::shared_ptr<const juce::AudioBuffer<float>> source;
    double sampleRate {}, start {}, end {}, step {};
    float pitch {};
    bool reverse {};
    juce::AudioBuffer<float> audio;

    bool matches(const juce::AudioBuffer<float>* input, double rate,
                 double from, double to, double sourceStep, bool reversed,
                 float semitones) const noexcept
    {
        return source.get() == input && sampleRate == rate && start == from
            && end == to && step == sourceStep && reverse == reversed
            && pitch == semitones;
    }
};

struct KeepLengthPreparedState;

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

    const float* getLeft() const noexcept;
    const float* getRight() const noexcept;
    const float* getDryLeft() const noexcept;
    const float* getDryRight() const noexcept;
    int getScratchCapacity() const noexcept { return scratchCapacity; }
    int getOutputLength() const noexcept { return outputLength; }
    int getOutputPosition() const noexcept { return outputPosition; }
    int getAnalysisSeekLength() const noexcept { return seekLength; }
    bool isUsingShortWindow() const noexcept { return usingShortWindow; }
    double getSourcePosition() const noexcept;
    bool isPrepared() const noexcept { return prepared; }
    void usePreparedRender(std::shared_ptr<const KeepLengthRender> render) noexcept
    { preparedRender = std::move(render); }
    // Worker-only preparation. A prepared state is consumed by one voice;
    // neither FFT plans nor mutable processing state are shared between voices.
    void prime(const juce::AudioBuffer<float>& source) noexcept;
    void usePreparedState(std::shared_ptr<KeepLengthPreparedState> state) noexcept;
    void releasePreparedState() noexcept { preparedState.reset(); }

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
    int tailOutputOffset { 0 };
    int tailOutputLength { 0 };
    int shortTransientPosition { 0 };
    float initialPitchSemitones { 0.0f };
    std::shared_ptr<const KeepLengthRender> preparedRender;
    std::shared_ptr<KeepLengthPreparedState> preparedState;
};

struct KeepLengthPreparedState : KeepLengthRender
{
    KeepLengthEngine engine;
    std::uint64_t reservedBytes {};
    int preparedBlockSize {};
};
