#include "../Source/PluginProcessor.h"

#include <iostream>

namespace
{
bool writeConstantTestSample(const juce::File& file)
{
    file.deleteFile();
    auto stream = std::unique_ptr<juce::FileOutputStream>(file.createOutputStream());
    if (stream == nullptr || ! stream->openedOk())
        return false;

    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatWriter> writer(
        format.createWriterFor(stream.get(), 48000.0, 1, 16, {}, 0));
    if (writer == nullptr)
        return false;

    stream.release();

    juce::AudioBuffer<float> source(1, 512);
    for (int i = 0; i < source.getNumSamples(); ++i)
        source.setSample(0, i, 0.75f);

    return writer->writeFromAudioSampleBuffer(source, 0, source.getNumSamples());
}

int firstAudibleSample(const juce::AudioBuffer<float>& buffer)
{
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            if (std::abs(buffer.getSample(channel, sample)) > 1.0e-5f)
                return sample;

    return -1;
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI initialiseJuce;
    const auto sampleFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("aster-midi-timing", ".wav", false);

    if (! writeConstantTestSample(sampleFile))
    {
        std::cerr << "Failed to create timing-test sample\n";
        return 1;
    }

    DrumSamplerAudioProcessor processor;
    processor.prepareToPlay(48000.0, 256);
    if (! processor.loadSampleForPad(0, sampleFile))
    {
        std::cerr << "Failed to load timing-test sample\n";
        sampleFile.deleteFile();
        return 1;
    }

    constexpr int eventSample = 73;
    juce::AudioBuffer<float> output(processor.getTotalNumOutputChannels(), 256);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 36, static_cast<juce::uint8>(127)), eventSample);
    processor.processBlock(output, midi);

    const int onset = firstAudibleSample(output);
    sampleFile.deleteFile();

    if (onset != eventSample)
    {
        std::cerr << "Expected onset at sample " << eventSample
                  << ", got " << onset << '\n';
        return 1;
    }

    std::cout << "MIDI onset rendered at exact sample " << onset << '\n';
    return 0;
}
