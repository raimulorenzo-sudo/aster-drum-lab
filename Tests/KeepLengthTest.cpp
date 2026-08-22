#include "../Source/SamplerVoice.h"

#include <cmath>
#include <iostream>
#include <vector>

namespace
{
constexpr int sampleRate = 48000;
constexpr int blockSize = 64;

juce::AudioBuffer<float> makeTone(int samples, double frequency = 220.0)
{
    juce::AudioBuffer<float> source(2, samples);
    for (int i = 0; i < samples; ++i)
    {
        const float value = 0.25f * std::sin(static_cast<float>(
            2.0 * juce::MathConstants<double>::pi * frequency * i / sampleRate));
        source.setSample(0, i, value);
        source.setSample(1, i, value);
    }
    return source;
}

juce::AudioBuffer<float> makeDelayedTransient(int samples, int onset)
{
    juce::AudioBuffer<float> source(2, samples);
    source.clear();
    for (int i = onset; i < samples; ++i)
    {
        const float elapsed = static_cast<float>(i - onset);
        const float value = (i == onset ? 0.9f : 0.0f)
                          + 0.28f * std::exp(-elapsed / 90.0f)
                                  * std::sin(0.73f * elapsed);
        source.setSample(0, i, value);
        source.setSample(1, i, value);
    }
    return source;
}

bool check(bool condition, const char* message)
{
    if (! condition)
        std::cerr << "FAIL: " << message << '\n';
    return condition;
}

int renderEngine(KeepLengthEngine& engine,
                 const juce::AudioBuffer<float>& source,
                 bool automate,
                 std::vector<float>& rendered,
                 float fixedPitch = 12.0f)
{
    int total = 0;
    while (true)
    {
        const float time = static_cast<float>(total) / static_cast<float>(sampleRate);
        const float pitch = automate
            ? 12.0f * std::sin(2.0f * juce::MathConstants<float>::pi * 2.0f * time)
            : fixedPitch;
        const int count = engine.process(source, pitch, blockSize);
        if (count <= 0)
            break;
        rendered.insert(rendered.end(), engine.getLeft(), engine.getLeft() + count);
        total += count;
    }
    return total;
}

double frequencyFromPositiveCrossings(const std::vector<float>& audio,
                                      int start,
                                      int end)
{
    int crossings = 0;
    for (int i = juce::jmax(1, start); i < juce::jmin(end, static_cast<int>(audio.size())); ++i)
        if (audio[static_cast<size_t>(i - 1)] <= 0.0f && audio[static_cast<size_t>(i)] > 0.0f)
            ++crossings;
    return static_cast<double>(crossings) * sampleRate / static_cast<double>(end - start);
}


int peakPosition(const std::vector<float>& audio)
{
    int position = 0;
    float peak = 0.0f;
    for (int i = 0; i < static_cast<int>(audio.size()); ++i)
    {
        const float magnitude = std::abs(audio[static_cast<size_t>(i)]);
        if (magnitude > peak)
        {
            peak = magnitude;
            position = i;
        }
    }
    return position;
}

int audibleOnsetPosition(const std::vector<float>& audio)
{
    float peak = 0.0f;
    for (float sample : audio)
        peak = juce::jmax(peak, std::abs(sample));
    const float threshold = peak * 0.01f; // -40 dB relative to the processed peak
    for (int i = 0; i < static_cast<int>(audio.size()); ++i)
        if (std::abs(audio[static_cast<size_t>(i)]) >= threshold)
            return i;
    return static_cast<int>(audio.size());
}
}

int main()
{
    bool ok = true;
    const auto source = makeTone(sampleRate);

    KeepLengthEngine fixed;
    fixed.prepare(sampleRate, blockSize);
    fixed.start(0.0, source.getNumSamples(), 1.0, false, 12.0f);
    std::vector<float> fixedAudio;
    const int fixedLength = renderEngine(fixed, source, false, fixedAudio);
    ok &= check(fixedLength == sampleRate, "fixed +12 pitch changed output length");
    const double fixedFrequency = frequencyFromPositiveCrossings(fixedAudio,
                                                                 sampleRate / 2,
                                                                 sampleRate * 9 / 10);
    ok &= check(std::abs(fixedFrequency - 440.0) < 12.0,
                "fixed +12 pitch did not reach approximately one octave");

    KeepLengthEngine automated;
    automated.prepare(sampleRate, blockSize);
    automated.start(0.0, source.getNumSamples(), 1.0, false, 0.0f);
    std::vector<float> automatedAudio;
    const int automatedLength = renderEngine(automated, source, true, automatedAudio);
    ok &= check(automatedLength == sampleRate, "automation changed output length");
    bool finite = true;
    float peak = 0.0f;
    for (float sample : automatedAudio)
    {
        finite = finite && std::isfinite(sample);
        peak = juce::jmax(peak, std::abs(sample));
    }
    ok &= check(finite && peak > 0.01f, "automation produced invalid or silent audio");

    // Very short drum hits are shorter than the stretcher's analysis window.
    // They use the padded one-shot path and must still remain audible and keep
    // their exact trimmed duration.
    const auto shortSource = makeTone(1024, 880.0);
    KeepLengthEngine shortHit;
    shortHit.prepare(sampleRate, blockSize);
    shortHit.start(0.0, shortSource.getNumSamples(), 1.0, false, 12.0f);
    std::vector<float> shortAudio;
    const int shortLength = renderEngine(shortHit, shortSource, false, shortAudio);
    float shortPeak = 0.0f;
    for (float sample : shortAudio)
        shortPeak = juce::jmax(shortPeak, std::abs(sample));
    ok &= check(shortLength == shortSource.getNumSamples(),
                "short hit changed duration");
    ok &= check(std::isfinite(shortPeak) && shortPeak > 0.01f,
                "short hit became invalid or silent");

    // Host automation can jump rather than glide. Exercise a worst-case block
    // pattern and verify that it neither changes duration nor creates NaNs.
    KeepLengthEngine jumping;
    jumping.prepare(sampleRate, blockSize);
    jumping.start(0.0, source.getNumSamples(), 1.0, false, -24.0f);
    int jumpingLength = 0;
    bool jumpingFinite = true;
    while (true)
    {
        const float pitch = ((jumpingLength / blockSize) & 1) == 0 ? -24.0f : 24.0f;
        const int count = jumping.process(source, pitch, blockSize);
        if (count <= 0)
            break;
        for (int i = 0; i < count; ++i)
            jumpingFinite = jumpingFinite && std::isfinite(jumping.getLeft()[i]);
        jumpingLength += count;
    }
    ok &= check(jumpingLength == source.getNumSamples(),
                "rapid automation changed duration");
    ok &= check(jumpingFinite, "rapid automation produced invalid audio");

    // A 30 ms hit should use the dedicated low-latency window instead of the
    // normal 40 ms drum window.  Its audible transient must stay on the MIDI
    // timeline; spectral energy below -40 dB is deliberately ignored here.
    constexpr int transientLength = sampleRate * 30 / 1000;
    constexpr int transientPosition = sampleRate * 5 / 1000;
    const auto transientSource = makeDelayedTransient(transientLength, transientPosition);
    for (float pitch : { -12.0f, 12.0f })
    {
        KeepLengthEngine transientEngine;
        transientEngine.prepare(sampleRate, blockSize);
        transientEngine.start(0.0, transientSource.getNumSamples(), 1.0, false, pitch);
        std::vector<float> transientAudio;
        renderEngine(transientEngine, transientSource, false, transientAudio, pitch);
        ok &= check(transientEngine.isUsingShortWindow(),
                    "high-frequency 30 ms hit did not select the short analysis window");
        ok &= check(transientEngine.getAnalysisSeekLength() <= sampleRate * 10 / 1000,
                    "short analysis window exceeds 10 ms");
        const int onsetError = audibleOnsetPosition(transientAudio) - transientPosition;
        const int peakError = peakPosition(transientAudio) - transientPosition;
        std::cout << "short transient pitch=" << pitch
                  << " onsetErrorSamples=" << onsetError
                  << " peakErrorSamples=" << peakError << '\n';
        ok &= check(std::abs(onsetError) <= sampleRate / 1000,
                    "short-window audible onset moved by more than 1 ms");
    }

    // The short engine must not merely render the whole hit at note-on.  A
    // block-rate pitch change during a 30 ms tone needs to alter its tail while
    // preserving the same output length.
    const auto shortTone = makeTone(transientLength, 900.0);
    KeepLengthEngine shortConstant;
    shortConstant.prepare(sampleRate, blockSize);
    shortConstant.start(0.0, shortTone.getNumSamples(), 1.0, false, -12.0f);
    std::vector<float> shortConstantAudio;
    renderEngine(shortConstant, shortTone, false, shortConstantAudio, -12.0f);

    KeepLengthEngine shortAutomated;
    shortAutomated.prepare(sampleRate, blockSize);
    shortAutomated.start(0.0, shortTone.getNumSamples(), 1.0, false, -12.0f);
    std::vector<float> shortAutomatedAudio;
    int shortAutomationPosition = 0;
    while (true)
    {
        const float pitch = shortAutomationPosition < sampleRate * 5 / 1000
            ? -12.0f : 12.0f;
        const int count = shortAutomated.process(shortTone, pitch, blockSize);
        if (count <= 0)
            break;
        shortAutomatedAudio.insert(shortAutomatedAudio.end(),
                                   shortAutomated.getLeft(),
                                   shortAutomated.getLeft() + count);
        shortAutomationPosition += count;
    }

    float shortAutomationDifference = 0.0f;
    for (int i = sampleRate * 20 / 1000; i < transientLength; ++i)
        shortAutomationDifference += std::abs(
            shortConstantAudio[static_cast<size_t>(i)]
            - shortAutomatedAudio[static_cast<size_t>(i)]);
    ok &= check(shortAutomatedAudio.size() == shortConstantAudio.size(),
                "short automation changed output length");
    ok &= check(shortAutomationDifference > 0.01f,
                "30 ms hit did not respond to pitch automation during playback");

    // Low tonal hits stay on the normal window: a 10 ms FFT does not have
    // enough frequency resolution for bass material.  This automatic fallback
    // prevents the short automation path from detuning a clipped kick tail.
    const auto shortLowTone = makeTone(transientLength, 220.0);
    KeepLengthEngine shortLowEngine;
    shortLowEngine.prepare(sampleRate, blockSize);
    shortLowEngine.start(0.0, shortLowTone.getNumSamples(), 1.0, false, 12.0f);
    std::vector<float> shortLowAudio;
    renderEngine(shortLowEngine, shortLowTone, false, shortLowAudio, 12.0f);
    ok &= check(! shortLowEngine.isUsingShortWindow(),
                "low tonal 30 ms hit incorrectly selected the short FFT");

    KeepLengthEngine resampled;
    const int source441Length = 44100;
    const auto source441 = makeTone(source441Length);
    resampled.prepare(sampleRate, blockSize);
    resampled.start(0.0, source441Length, 44100.0 / sampleRate, false, -12.0f);
    std::vector<float> resampledAudio;
    ok &= check(renderEngine(resampled, source441, false, resampledAudio) == sampleRate,
                "44.1 kHz source did not retain one-second duration at 48 kHz host rate");

    LayerData freshLayer;
    ok &= check(freshLayer.keepLength, "new layers must default KEEP LENGTH to ON");
    juce::ValueTree legacyLayer("Layer");
    LayerData migratedLayer;
    migratedLayer.fromValueTree(legacyLayer);
    ok &= check(migratedLayer.keepLength,
                "legacy layer without property must adopt KEEP LENGTH ON");
    juce::ValueTree legacyPad("Pad");
    PadData migratedPad;
    migratedPad.fromValueTree(legacyPad);
    ok &= check(migratedPad.keepLength,
                "legacy pad without property must adopt KEEP LENGTH ON");
    LayerData roundTripLayer;
    roundTripLayer.keepLength = true;
    LayerData restoredLayer;
    restoredLayer.fromValueTree(roundTripLayer.toValueTree());
    ok &= check(restoredLayer.keepLength, "KEEP LENGTH was not persisted");

    DrumVoice fixedVoice;
    fixedVoice.prepare(sampleRate, blockSize);
    fixedVoice.start(0, 0.0, source.getNumSamples(), 1.0f, 1.0f, true,
                     0.0f, 0.05f, sampleRate, 2.0, false, 0, 0,
                     source.getNumSamples(), 1, 0, false, false,
                     true, 1.0, 12.0f, 0.0f);
    LayerData layer;
    layer.keepLength = true;
    juce::AudioBuffer<float> output(2, blockSize);
    while (fixedVoice.isActive)
    {
        output.clear();
        fixedVoice.render(source, output, 0, blockSize, layer, sampleRate, 12.0f);
    }
    ok &= check(fixedVoice.keepLengthEngine.getOutputPosition() == sampleRate,
                "DrumVoice KEEP LENGTH integration ended at the wrong duration");

    DrumVoice zeroPitchVoice;
    zeroPitchVoice.prepare(sampleRate, blockSize);
    zeroPitchVoice.start(0, 0.0, source.getNumSamples(), 1.0f, 1.0f, true,
                         0.0f, 0.05f, sampleRate, 1.0, false, 0, 0,
                         source.getNumSamples(), 3, 0, false, false,
                         true, 1.0, 0.0f, 0.0f);
    output.clear();
    zeroPitchVoice.render(source, output, 0, blockSize, layer, sampleRate, 0.0f);
    float zeroPitchError = 0.0f;
    for (int i = 0; i < blockSize; ++i)
        zeroPitchError = juce::jmax(zeroPitchError,
                                    std::abs(output.getSample(0, i) - source.getSample(0, i)));
    ok &= check(zeroPitchError < 1.0e-6f,
                "zero Pitch no longer uses the transparent dry path");

    DrumVoice legacyVoice;
    legacyVoice.prepare(sampleRate, blockSize);
    legacyVoice.start(0, 0.0, source.getNumSamples(), 1.0f, 1.0f, true,
                      0.0f, 0.05f, sampleRate, 2.0, false, 0, 0,
                      source.getNumSamples(), 2, 0, false, false,
                      false, 1.0, 12.0f, 0.0f);
    layer.keepLength = false;
    while (legacyVoice.isActive)
    {
        output.clear();
        legacyVoice.render(source, output, 0, blockSize, layer, sampleRate, 12.0f);
    }
    ok &= check(std::abs(legacyVoice.samplesRendered - sampleRate / 2) <= blockSize,
                "KEEP LENGTH OFF no longer follows legacy playback-rate duration");

    if (! ok)
        return 1;

    std::cout << "KEEP LENGTH tests passed: fixed length, smooth and jumping automation, short hits, "
                 "sample-rate conversion, legacy compatibility, and DrumVoice integration.\n";
    return 0;
}
