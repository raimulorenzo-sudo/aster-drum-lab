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

bool renderCompressorLevel(const juce::File& sampleFile,
                           float makeupDb,
                           float mix,
                           float outputDb,
                           bool bypassed,
                           float& level)
{
    DrumSamplerAudioProcessor processor;
    processor.prepareToPlay(48000.0, 256);
    if (! processor.loadSampleForPad(0, sampleFile))
        return false;

    LayerFxSlot compressor;
    compressor.type = LayerFxType::Compressor;
    compressor.bypassed = bypassed;
    compressor.compressor.threshold = 0.0f;
    compressor.compressor.ratio = 1.0f;
    compressor.compressor.makeupDb = makeupDb;
    compressor.compressor.mix = mix;
    compressor.compressor.outputDb = outputDb;
    processor.getKit().pads[0].layers[0].fxChain = { compressor };

    juce::AudioBuffer<float> output(processor.getTotalNumOutputChannels(), 256);
    output.clear();
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 36, static_cast<juce::uint8>(127)), 0);
    processor.processBlock(output, midi);
    level = std::abs(output.getSample(0, 64));
    return true;
}

bool approximately(float actual, float expected, float tolerance = 0.015f)
{
    return std::abs(actual - expected) <= tolerance;
}

bool compressorGainPersistenceIsCompatible()
{
    LayerData source;
    LayerFxSlot compressor;
    compressor.type = LayerFxType::Compressor;
    compressor.compressor.makeupDb = 7.5f;
    compressor.compressor.outputDb = -3.0f;
    source.fxChain = { compressor };

    auto currentTree = source.toValueTree();
    LayerData restored;
    restored.fromValueTree(currentTree);
    if (restored.fxChain.size() != 1
        || ! approximately(restored.fxChain[0].compressor.makeupDb, 7.5f)
        || ! approximately(restored.fxChain[0].compressor.outputDb, -3.0f))
        return false;

    auto legacyTree = currentTree.createCopy();
    auto legacyFx = legacyTree.getChildWithName("FxChain").getChild(0);
    legacyFx.removeProperty("makeupDb", nullptr);
    legacyFx.removeProperty("outputDb", nullptr);

    LayerData legacyRestored;
    legacyRestored.fromValueTree(legacyTree);
    return legacyRestored.fxChain.size() == 1
        && approximately(legacyRestored.fxChain[0].compressor.makeupDb, 0.0f)
        && approximately(legacyRestored.fxChain[0].compressor.outputDb, 0.0f);
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
    if (onset != eventSample)
    {
        std::cerr << "Expected onset at sample " << eventSample
                  << ", got " << onset << '\n';
        sampleFile.deleteFile();
        return 1;
    }

    float baseline = 0.0f;
    float makeup = 0.0f;
    float dryOutput = 0.0f;
    float bypass = 0.0f;
    if (! renderCompressorLevel(sampleFile, 0.0f, 1.0f, 0.0f, false, baseline)
        || ! renderCompressorLevel(sampleFile, 6.0f, 1.0f, 0.0f, false, makeup)
        || ! renderCompressorLevel(sampleFile, 6.0f, 0.0f, -6.0f, false, dryOutput)
        || ! renderCompressorLevel(sampleFile, 6.0f, 1.0f, -6.0f, true, bypass))
    {
        std::cerr << "Failed to render compressor gain test\n";
        sampleFile.deleteFile();
        return 1;
    }

    const float sixDbGain = juce::Decibels::decibelsToGain(6.0f);
    if (baseline <= 1.0e-5f
        || ! approximately(makeup / baseline, sixDbGain)
        || ! approximately(dryOutput / baseline, 1.0f / sixDbGain)
        || ! approximately(bypass / baseline, 1.0f)
        || ! compressorGainPersistenceIsCompatible())
    {
        std::cerr << "Compressor gain flow mismatch: baseline=" << baseline
                  << ", makeup ratio=" << makeup / baseline
                  << ", dry/output ratio=" << dryOutput / baseline
                  << ", bypass ratio=" << bypass / baseline << '\n';
        sampleFile.deleteFile();
        return 1;
    }

    sampleFile.deleteFile();

    std::cout << "MIDI onset rendered at exact sample " << onset << '\n';
    std::cout << "Compressor gain flow verified: Make Up -> Mix -> Output\n";
    return 0;
}
