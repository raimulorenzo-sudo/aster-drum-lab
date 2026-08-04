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

bool renderTransientLevel(const juce::File& sampleFile,
                          float outputDb,
                          bool bypassed,
                          float& level)
{
    DrumSamplerAudioProcessor processor;
    processor.prepareToPlay(48000.0, 256);
    if (! processor.loadSampleForPad(0, sampleFile))
        return false;

    LayerFxSlot transient;
    transient.type = LayerFxType::Transient;
    transient.bypassed = bypassed;
    transient.transient.attack = 0.0f;
    transient.transient.sustain = 0.0f;
    transient.transient.outputDb = outputDb;
    processor.getKit().pads[0].layers[0].fxChain = { transient };

    juce::AudioBuffer<float> output(processor.getTotalNumOutputChannels(), 256);
    output.clear();
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 36, static_cast<juce::uint8>(127)), 0);
    processor.processBlock(output, midi);
    level = std::abs(output.getSample(0, 64));
    return true;
}

bool transientOutputPersistenceIsCompatible()
{
    LayerData source;
    LayerFxSlot transient;
    transient.type = LayerFxType::Transient;
    transient.transient.outputDb = -4.5f;
    source.fxChain = { transient };

    auto currentTree = source.toValueTree();
    LayerData restored;
    restored.fromValueTree(currentTree);
    if (restored.fxChain.size() != 1
        || ! approximately(restored.fxChain[0].transient.outputDb, -4.5f))
        return false;

    auto legacyTree = currentTree.createCopy();
    auto legacyFx = legacyTree.getChildWithName("FxChain").getChild(0);
    legacyFx.removeProperty("outputDb", nullptr);

    LayerData legacyRestored;
    legacyRestored.fromValueTree(legacyTree);
    return legacyRestored.fxChain.size() == 1
        && approximately(legacyRestored.fxChain[0].transient.outputDb, 0.0f);
}

bool automationSlotsAreStableAndPersistent()
{
    DrumSamplerAudioProcessor processor;
    int automatableCount = 0;
    int expectedSlot = 0;
    juce::RangedAudioParameter* firstSlot = nullptr;

    for (auto* parameter : processor.getParameters())
    {
        if (! parameter->isAutomatable())
            continue;

        ++automatableCount;
        const auto* parameterWithID = dynamic_cast<juce::AudioProcessorParameterWithID*>(parameter);
        if (parameterWithID == nullptr)
        {
            std::cerr << "Automatable parameter has no stable ID\n";
            return false;
        }

        const auto expectedID = "asterAutomationSlot"
                              + juce::String(expectedSlot + 1).paddedLeft('0', 2);
        const auto expectedName = "ASTER AUTO "
                                + juce::String(expectedSlot + 1).paddedLeft('0', 2);
        if (parameterWithID->paramID != expectedID
            || parameter->getName(128) != expectedName
            || ! parameter->isMetaParameter())
        {
            std::cerr << "Unexpected slot " << expectedSlot << ": id="
                      << parameterWithID->paramID << ", name=" << parameter->getName(128)
                      << ", meta=" << parameter->isMetaParameter() << '\n';
            return false;
        }

        if (expectedSlot == 0)
            firstSlot = dynamic_cast<juce::RangedAudioParameter*>(parameter);
        ++expectedSlot;
    }

    if (automatableCount != DrumSamplerAudioProcessor::automationSlotCount
        || expectedSlot != DrumSamplerAudioProcessor::automationSlotCount
        || firstSlot == nullptr)
    {
        std::cerr << "Automatable count=" << automatableCount << ", expectedSlot="
                  << expectedSlot << ", firstSlot=" << (firstSlot != nullptr) << '\n';
        return false;
    }

    processor.beginAutomationLearn(0);
    processor.setMasterVolumeParameter(0.37f, true);
    if (processor.getAutomationSlotTargetID(0) != "masterVolume"
        || processor.getAutomationSlotTargetName(0) != "Master Volume")
    {
        std::cerr << "Learn mismatch: id=" << processor.getAutomationSlotTargetID(0)
                  << ", name=" << processor.getAutomationSlotTargetName(0) << '\n';
        return false;
    }

    firstSlot->setValueNotifyingHost(0.64f);
    processor.syncKitFromParameters();
    if (! approximately(processor.getKit().masterVolume, 0.64f, 0.001f))
    {
        std::cerr << "Playback mismatch: " << processor.getKit().masterVolume << '\n';
        return false;
    }

    juce::MemoryBlock savedState;
    processor.getStateInformation(savedState);
    DrumSamplerAudioProcessor restored;
    restored.setStateInformation(savedState.getData(), static_cast<int>(savedState.getSize()));
    if (restored.getAutomationSlotTargetID(0) != "masterVolume")
    {
        std::cerr << "Restore mismatch: " << restored.getAutomationSlotTargetID(0) << '\n';
        return false;
    }

    restored.assignAutomationSlot(1, "masterVolume");
    if (! restored.getAutomationSlotTargetID(0).isEmpty()
        || restored.getAutomationSlotTargetID(1) != "masterVolume")
    {
        std::cerr << "Direct assignment mismatch: slot0="
                  << restored.getAutomationSlotTargetID(0) << ", slot1="
                  << restored.getAutomationSlotTargetID(1) << '\n';
        return false;
    }

    restored.clearAutomationSlot(1);
    if (! restored.getAutomationSlotTargetID(1).isEmpty())
        return false;

    LayerFxSlot compressor;
    compressor.type = LayerFxType::Compressor;
    restored.getKit().pads[0].layers[0].fxChain.push_back(compressor);
    const juce::String fxTarget { "pad01.layer01.fx.compressor.threshold" };
    restored.assignAutomationSlot(2, fxTarget);
    if (restored.getAutomationSlotTargetID(2) != fxTarget
        || restored.getAutomationSlotTargetName(2) != "Pad 01 L01 COMPRESSOR Threshold")
    {
        std::cerr << "FX assignment mismatch: " << restored.getAutomationSlotTargetName(2) << '\n';
        return false;
    }

    restored.setFxAutomationTargetValue(fxTarget, 0.25f, true);
    if (! approximately(restored.getKit().pads[0].layers[0].fxChain[0].compressor.threshold, -36.0f, 0.001f))
    {
        std::cerr << "FX UI routing mismatch\n";
        return false;
    }

    juce::RangedAudioParameter* thirdSlot = nullptr;
    for (auto* parameter : restored.getParameters())
        if (const auto* withID = dynamic_cast<juce::AudioProcessorParameterWithID*>(parameter);
            withID != nullptr && withID->paramID == "asterAutomationSlot03")
            thirdSlot = dynamic_cast<juce::RangedAudioParameter*>(parameter);
    if (thirdSlot == nullptr) return false;
    thirdSlot->setValueNotifyingHost(0.75f);
    restored.prepareToPlay(48000.0, 64);
    if (! approximately(restored.getKit().pads[0].layers[0].fxChain[0].compressor.threshold, -12.0f, 0.001f))
    {
        std::cerr << "FX host routing mismatch\n";
        return false;
    }

    juce::MemoryBlock fxState;
    restored.getStateInformation(fxState);
    DrumSamplerAudioProcessor fxRestored;
    fxRestored.setStateInformation(fxState.getData(), static_cast<int>(fxState.getSize()));
    return fxRestored.getAutomationSlotTargetID(2) == fxTarget
        && fxRestored.getAutomationSlotTargetName(2) == "Pad 01 L01 COMPRESSOR Threshold";
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

    float transientBaseline = 0.0f;
    float transientOutput = 0.0f;
    float transientBypass = 0.0f;
    if (! renderTransientLevel(sampleFile, 0.0f, false, transientBaseline)
        || ! renderTransientLevel(sampleFile, -6.0f, false, transientOutput)
        || ! renderTransientLevel(sampleFile, -6.0f, true, transientBypass))
    {
        std::cerr << "Failed to render transient output test\n";
        sampleFile.deleteFile();
        return 1;
    }

    if (transientBaseline <= 1.0e-5f
        || ! approximately(transientOutput / transientBaseline, 1.0f / sixDbGain)
        || ! approximately(transientBypass / transientBaseline, 1.0f)
        || ! transientOutputPersistenceIsCompatible())
    {
        std::cerr << "Transient output mismatch: baseline=" << transientBaseline
                  << ", output ratio=" << transientOutput / transientBaseline
                  << ", bypass ratio=" << transientBypass / transientBaseline << '\n';
        sampleFile.deleteFile();
        return 1;
    }

    if (! automationSlotsAreStableAndPersistent())
    {
        std::cerr << "Automation slot exposure, routing, or persistence mismatch\n";
        sampleFile.deleteFile();
        return 1;
    }

    sampleFile.deleteFile();

    std::cout << "MIDI onset rendered at exact sample " << onset << '\n';
    std::cout << "Compressor gain flow verified: Make Up -> Mix -> Output\n";
    std::cout << "Transient output volume and legacy persistence verified\n";
    std::cout << "24 fixed automation slots and assignment persistence verified\n";
    return 0;
}
