#include "SampleBrowserAudio.h"
#include "SmartTrim.h"
#include <cmath>

bool isBrowserAudioFile(const juce::File& file)
{
    return file.hasFileExtension("wav;aif;aiff");
}

BrowserSample decodeBrowserSample(const juce::File& file, const std::function<bool()>& cancelled)
{
    BrowserSample result;
    const auto aborted = [&] { return cancelled && cancelled(); };
    if (aborted()) return result;
    if (! file.existsAsFile() || ! isBrowserAudioFile(file))
    {
        result.error = "File unavailable. Choose a WAV or AIFF file.";
        return result;
    }
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (! reader || reader->lengthInSamples <= 0 || reader->numChannels == 0
        || ! std::isfinite(reader->sampleRate) || reader->sampleRate <= 0.0)
    {
        result.error = "This audio file could not be read.";
        return result;
    }
    const auto channels = juce::jmin(2, static_cast<int>(reader->numChannels));
    // Bound allocations, integer conversion, and decode time for corrupt/huge headers.
    constexpr juce::int64 maxDecodedBytes = 128 * 1024 * 1024;
    if (reader->lengthInSamples > maxDecodedBytes / (channels * static_cast<int>(sizeof(float))))
    {
        result.error = "File too large for the browser (128 MB decoded audio limit).";
        return result;
    }
    try
    {
        auto buffer = std::make_unique<juce::AudioBuffer<float>>(channels, static_cast<int>(reader->lengthInSamples));
        for (int offset = 0; offset < buffer->getNumSamples(); offset += 65536)
        {
            if (aborted()) return result;
            const int length = juce::jmin(65536, buffer->getNumSamples() - offset);
            if (! reader->read(buffer.get(), offset, length, offset, true, channels > 1))
            {
                result.error = "Audio read failed. The kit has not been changed.";
                return result;
            }
            for (int ch = 0; ch < channels; ++ch)
                for (int i = offset; i < offset + length; ++i)
                    if (! std::isfinite(buffer->getSample(ch, i))) buffer->setSample(ch, i, 0.0f);
        }
        if (aborted()) return result;
        const auto trim = SmartTrim::analyze(*buffer, reader->sampleRate);
        result.trimStart = trim.startPosition;
        result.trimEnd = trim.endPosition;
        result.sampleRate = reader->sampleRate;
        result.audio = std::move(buffer);
    }
    catch (const std::bad_alloc&)
    {
        result.error = "Not enough memory to load this sample.";
    }
    return result;
}

void SampleBrowserPreview::start(BrowserSample&& sample)
{
    if (! sample.audio || sample.audio->getNumSamples() == 0) return;
    // Swap ownership under the lock, destroy the old buffer on this non-audio thread.
    {
        const juce::SpinLock::ScopedLockType guard(lock);
        audio.swap(sample.audio);
        sourceRate = sample.sampleRate;
        position = 0.0;
        smoothedGain = 0.0f;
        stopping = false;
        playing.store(true);
    }
}
void SampleBrowserPreview::stop()
{
    stopping.store(true);
}
void SampleBrowserPreview::setGain(float value)
{
    if (std::isfinite(value)) gain.store(juce::jlimit(0.0f, 1.0f, value));
}
bool SampleBrowserPreview::render(juce::AudioBuffer<float>& output, double outputRate)
{
    const juce::SpinLock::ScopedTryLockType guard(lock);
    if (! guard.isLocked() || ! playing.load() || ! audio || outputRate <= 0.0) return false;
    const double step = sourceRate / outputRate;
    const float gainStep = static_cast<float>(1.0 / (outputRate * 0.003));
    const int length = audio->getNumSamples();
    const int channels = juce::jmin(2, output.getNumChannels());
    for (int i = 0; i < output.getNumSamples(); ++i)
    {
        if (position >= length || (stopping && smoothedGain <= 0.0f))
        {
            playing.store(false);
            break;
        }
        const float target = stopping ? 0.0f : gain.load();
        smoothedGain += juce::jlimit(-gainStep, gainStep, target - smoothedGain);
        const int a = static_cast<int>(position), b = juce::jmin(a + 1, length - 1);
        const float fraction = static_cast<float>(position - a);
        const float endFade = juce::jlimit(0.0f, 1.0f, static_cast<float>((length - position) / (sourceRate * 0.002)));
        for (int ch = 0; ch < channels; ++ch)
        {
            const auto* data = audio->getReadPointer(juce::jmin(ch, audio->getNumChannels() - 1));
            output.addSample(ch, i, (data[a] + (data[b] - data[a]) * fraction) * smoothedGain * endFade);
        }
        position += step;
    }
    return true;
}
