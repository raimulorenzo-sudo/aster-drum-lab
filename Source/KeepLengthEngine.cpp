#include "KeepLengthEngine.h"

#include <algorithm>
#include <cmath>

void KeepLengthEngine::prepare(double hostSampleRate, int maximumBlockSize)
{
    const int safeRate = juce::jmax(8000, static_cast<int>(std::round(hostSampleRate)));
    processingSampleRate = static_cast<double>(safeRate);

    // 40/10 ms was the best transient/pitch compromise in the drum tests.
    // Split computation distributes FFT work across callbacks and avoids the
    // periodic CPU spikes of the unsplit configuration.
    normalStretch.configure(2,
                            static_cast<int>(safeRate * 0.040),
                            static_cast<int>(safeRate * 0.010),
                            true);

    // A smaller window is used only when the normal analysis window would
    // cover the whole hit.  This keeps the normal engine's pitch quality for
    // kicks, snares and longer material while reducing the non-streaming edge
    // case from roughly 40 ms to roughly 10 ms.  The small FFT is cheap enough
    // not to need split computation, which would add another interval of
    // output look-ahead.
    shortStretch.configure(2,
                           static_cast<int>(safeRate * 0.010),
                           static_cast<int>(safeRate * 0.002),
                           false);

    normalSeekLength = normalStretch.outputSeekLength(1.0f);
    shortSeekLength = shortStretch.outputSeekLength(1.0f);
    seekLength = normalSeekLength;
    activeStretch = &normalStretch;
    scratchCapacity = juce::jmax(juce::jmax(1, maximumBlockSize),
                                 juce::jmax(normalSeekLength, shortSeekLength));
    for (int channel = 0; channel < 2; ++channel)
    {
        inputScratch[static_cast<size_t>(channel)].assign(static_cast<size_t>(scratchCapacity), 0.0f);
        outputScratch[static_cast<size_t>(channel)].assign(static_cast<size_t>(scratchCapacity), 0.0f);
        dryScratch[static_cast<size_t>(channel)].assign(static_cast<size_t>(scratchCapacity), 0.0f);
        inputPointers[static_cast<size_t>(channel)] = inputScratch[static_cast<size_t>(channel)].data();
        outputPointers[static_cast<size_t>(channel)] = outputScratch[static_cast<size_t>(channel)].data();
    }

    prepared = true;
    reset();
}

void KeepLengthEngine::reset() noexcept
{
    needsInitialSeek = false;
    shortOutputCached = false;
    usingShortWindow = false;
    shortWindowCandidate = false;
    engineSelectionPending = false;
    activeStretch = &normalStretch;
    seekLength = normalSeekLength;
    inputPosition = 0;
    outputPosition = 0;
    outputLength = 0;
    processLength = 0;
    shortTransientPosition = 0;
    initialPitchSemitones = 0.0f;
}

void KeepLengthEngine::start(double startSample,
                             double endSample,
                             double sourceSamplesPerHostSample,
                             bool shouldReverse,
                             float initialSemitones) noexcept
{
    startPosition = startSample;
    endPosition = juce::jmax(startSample + 1.0, endSample);
    sourceStep = juce::jmax(1.0e-9, sourceSamplesPerHostSample);
    reversed = shouldReverse;

    const double hostTimelineLength = (endPosition - startPosition) / sourceStep;
    outputLength = juce::jmax(1, static_cast<int>(std::ceil(hostTimelineLength)));

    shortWindowCandidate = outputLength <= normalSeekLength;
    usingShortWindow = shortWindowCandidate;
    engineSelectionPending = shortWindowCandidate;
    activeStretch = shortWindowCandidate ? &shortStretch : &normalStretch;
    seekLength = shortWindowCandidate ? shortSeekLength : normalSeekLength;
    processLength = juce::jmax(0, outputLength - seekLength);
    inputPosition = 0;
    outputPosition = 0;
    shortTransientPosition = 0;
    initialPitchSemitones = initialSemitones;
    activeStretch->setTransposeSemitones(initialPitchSemitones);
    needsInitialSeek = prepared;
    shortOutputCached = false;
}

void KeepLengthEngine::fillTimeline(const juce::AudioBuffer<float>& source,
                                    int timelineStart,
                                    int count,
                                    std::array<std::vector<float>, 2>& destination) noexcept
{
    const int sourceLength = source.getNumSamples();
    const bool stereo = source.getNumChannels() >= 2;
    const float* sourceLeft = sourceLength > 0 ? source.getReadPointer(0) : nullptr;
    const float* sourceRight = stereo ? source.getReadPointer(1) : sourceLeft;

    for (int i = 0; i < count; ++i)
    {
        const double timeline = static_cast<double>(timelineStart + i);
        const double position = reversed
            ? endPosition - sourceStep * (timeline + 1.0)
            : startPosition + sourceStep * timeline;

        float left = 0.0f;
        float right = 0.0f;
        if (sourceLeft != nullptr && position >= startPosition && position < endPosition
            && position >= 0.0 && position < static_cast<double>(sourceLength))
        {
            const int index = juce::jlimit(0, sourceLength - 1, static_cast<int>(std::floor(position)));
            const int next = juce::jmin(index + 1, sourceLength - 1);
            const float fraction = static_cast<float>(position - std::floor(position));
            left = sourceLeft[index] + fraction * (sourceLeft[next] - sourceLeft[index]);
            right = sourceRight[index] + fraction * (sourceRight[next] - sourceRight[index]);
        }

        destination[0][static_cast<size_t>(i)] = left;
        destination[1][static_cast<size_t>(i)] = right;
    }
}

void KeepLengthEngine::initialiseFromSource(const juce::AudioBuffer<float>& source) noexcept
{
    if (! needsInitialSeek)
        return;

    if (engineSelectionPending)
    {
        findShortTransientPosition(source);

        // A very short low-frequency/tonal hit needs a longer FFT for correct
        // pitch.  High-frequency and noisy drum transients use the 10 ms engine
        // so automation remains responsive; low tonal hits retain the normal
        // engine's pitch quality.  The decision scans at most ~40 ms.
        const int scanLength = juce::jmin(outputLength, scratchCapacity);
        const float activityThreshold = 1.0e-4f;
        double leftEnergy = 0.0;
        double rightEnergy = 0.0;
        for (int i = shortTransientPosition; i < scanLength; ++i)
        {
            const float left = dryScratch[0][static_cast<size_t>(i)];
            const float right = dryScratch[1][static_cast<size_t>(i)];
            leftEnergy += static_cast<double>(left) * left;
            rightEnergy += static_cast<double>(right) * right;
        }
        const auto& analysisChannel = dryScratch[rightEnergy > leftEnergy ? 1u : 0u];
        int signChanges = 0;
        float previous = 0.0f;
        bool havePrevious = false;
        for (int i = shortTransientPosition; i < scanLength; ++i)
        {
            const float sample = analysisChannel[static_cast<size_t>(i)];
            if (std::abs(sample) < activityThreshold)
                continue;
            if (havePrevious && ((sample >= 0.0f) != (previous >= 0.0f)))
                ++signChanges;
            previous = sample;
            havePrevious = true;
        }
        const int activeLength = juce::jmax(1, scanLength - shortTransientPosition);
        const double crossingFrequency = 0.5 * static_cast<double>(signChanges)
                                       * processingSampleRate / activeLength;
        usingShortWindow = crossingFrequency >= 500.0;
        activeStretch = usingShortWindow ? &shortStretch : &normalStretch;
        seekLength = usingShortWindow ? shortSeekLength : normalSeekLength;
        processLength = juce::jmax(0, outputLength - seekLength);
        activeStretch->setTransposeSemitones(initialPitchSemitones);
        engineSelectionPending = false;
    }

    fillTimeline(source, 0, seekLength, inputScratch);
    activeStretch->outputSeek(inputPointers, seekLength);
    inputPosition = seekLength;

    // A source shorter than the analysis seek window cannot be streamed a
    // callback at a time: the first small flush would contain only silence.
    // Compute the padded minimum window once, cache it in the input scratch,
    // and expose only the original fixed-length portion over later callbacks.
    if (outputLength <= seekLength)
    {
        activeStretch->flush(inputPointers, seekLength, 1.0f);
        shortOutputCached = true;
    }
    needsInitialSeek = false;
}

void KeepLengthEngine::findShortTransientPosition(const juce::AudioBuffer<float>& source) noexcept
{
    // Short hits fit inside the normal analysis scratch, so this scan is
    // bounded to roughly 40 ms and is safe to perform at note-on.  It lets us
    // remove spectral pre-echo without delaying or replacing the actual peak.
    const int scanLength = juce::jmin(outputLength, scratchCapacity);
    fillTimeline(source, 0, scanLength, dryScratch);

    float peak = 0.0f;
    for (int i = 0; i < scanLength; ++i)
        peak = juce::jmax(peak, juce::jmax(std::abs(dryScratch[0][static_cast<size_t>(i)]),
                                          std::abs(dryScratch[1][static_cast<size_t>(i)])));

    if (peak <= 1.0e-9f)
        return;

    const float threshold = peak * 0.01f; // -40 dB relative to the hit peak
    for (int i = 0; i < scanLength; ++i)
    {
        if (std::abs(dryScratch[0][static_cast<size_t>(i)]) >= threshold
            || std::abs(dryScratch[1][static_cast<size_t>(i)]) >= threshold)
        {
            shortTransientPosition = i;
            return;
        }
    }
}

void KeepLengthEngine::applyShortTransientGuard(int timelineStart, int count) noexcept
{
    if (! shortWindowCandidate || shortTransientPosition <= timelineStart)
        return;

    const int protectedCount = juce::jlimit(0, count,
                                            shortTransientPosition - timelineStart);
    for (int channel = 0; channel < 2; ++channel)
    {
        const auto c = static_cast<size_t>(channel);
        std::copy_n(dryScratch[c].data(), protectedCount, outputScratch[c].data());
    }
}

int KeepLengthEngine::process(const juce::AudioBuffer<float>& source,
                              float targetSemitones,
                              int numSamples) noexcept
{
    if (! prepared || numSamples <= 0 || outputPosition >= outputLength)
        return 0;

    initialiseFromSource(source);
    activeStretch->setTransposeSemitones(targetSemitones);

    const int requested = juce::jmin(numSamples, scratchCapacity);
    const int count = juce::jmin(requested, outputLength - outputPosition);
    const int chunkStartPosition = outputPosition;
    fillTimeline(source, outputPosition, count, dryScratch);

    if (shortOutputCached)
    {
        for (int channel = 0; channel < 2; ++channel)
        {
            const auto c = static_cast<size_t>(channel);
            std::copy_n(inputScratch[c].data() + outputPosition,
                        count,
                        outputScratch[c].data());
        }
        applyShortTransientGuard(chunkStartPosition, count);
        outputPosition += count;
        return count;
    }

    int written = 0;

    while (written < count)
    {
        const int processRemaining = juce::jmax(0, processLength - outputPosition);
        if (processRemaining > 0)
        {
            const int block = juce::jmin(count - written, processRemaining);
            fillTimeline(source, inputPosition, block, inputScratch);

            std::array<float*, 2> blockOutput {
                outputPointers[0] + written,
                outputPointers[1] + written
            };
            activeStretch->process(inputPointers, block, blockOutput, block);
            inputPosition += block;
            outputPosition += block;
            written += block;
        }
        else
        {
            const int block = count - written;
            std::array<float*, 2> blockOutput {
                outputPointers[0] + written,
                outputPointers[1] + written
            };
            activeStretch->flush(blockOutput, block, 1.0f);
            outputPosition += block;
            written += block;
        }
    }

    applyShortTransientGuard(chunkStartPosition, written);
    return written;
}

double KeepLengthEngine::getSourcePosition() const noexcept
{
    const double timeline = static_cast<double>(outputPosition);
    return reversed
        ? endPosition - sourceStep * timeline
        : startPosition + sourceStep * timeline;
}
