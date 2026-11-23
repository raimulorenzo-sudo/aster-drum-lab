#include "../Source/DemoMode.h"
#include "../Source/PluginProcessor.h"

#include <chrono>
#include <iostream>
#include <thread>

namespace
{
    bool writeTestSample(const juce::File& file)
    {
        auto stream = file.createOutputStream();
        if (stream == nullptr || ! stream->openedOk()) return false;
        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatWriter> writer(
            format.createWriterFor(stream.get(), 48000.0, 1, 16, {}, 0));
        if (writer == nullptr) return false;
        stream.release();
        juce::AudioBuffer<float> source(1, 48000);
        for (int i = 0; i < source.getNumSamples(); ++i)
            source.setSample(0, i, 0.5f);
        return writer->writeFromAudioSampleBuffer(source, 0, source.getNumSamples());
    }

    bool require(bool condition, const char* message)
    {
        if (condition)
            return true;
        std::cerr << "FAIL: " << message << '\n';
        return false;
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    bool ok = true;
    const auto initial = AsterDemoMode::getStateAsVar();
    ok &= require((bool) initial.getProperty("isDemo", false) == AsterDemoMode::isDemoBuild,
                  "reported build mode must match the compile-time mode");
    ok &= require(! AsterDemoMode::hasStarted(),
                  "the timer must not start when the plug-in is merely loaded");

    juce::TemporaryFile sample(".wav");
    DrumSamplerAudioProcessor processor;
    processor.prepareToPlay(48000.0, 64);
    if (! writeTestSample(sample.getFile()) || ! processor.loadSampleForPad(0, sample.getFile()))
        return 1;
    juce::AudioBuffer<float> audio(juce::jmax(2, processor.getTotalNumOutputChannels()), 64);
    juce::MidiBuffer midi;
    const auto play = [&]
    {
        midi.clear();
        midi.addEvent(juce::MidiMessage::noteOn(1, 36, static_cast<juce::uint8>(127)), 0);
        processor.processBlock(audio, midi);
    };
    play(); // Start the real MIDI/audio path, not just the timer helper.
    ok &= require(audio.getMagnitude(0, 64) > 0.01f, "real-time audio must work before expiry");

    if constexpr (AsterDemoMode::isDemoBuild)
    {
        if constexpr (AsterDemoMode::durationSeconds > 1)
        {
            ok &= require(AsterDemoMode::getOutputGain(1.0) == 1.0f,
                          "the Demo must remain uninterrupted before expiry");
            ok &= require(AsterDemoMode::getOutputGain(
                              static_cast<double>(AsterDemoMode::durationSeconds)) == 0.0f,
                          "audio must be silent at the configured expiry time");
        }

        const auto saveProbe = juce::File::getSpecialLocation(juce::File::tempDirectory)
            .getNonexistentChildFile("aster-demo-save-probe", ".asterkit", false);
        ok &= require(! processor.saveKitToFile(saveProbe),
                      "Demo builds must reject ASTER kit saving");
        ok &= require(! saveProbe.existsAsFile(),
                      "a rejected Demo save must not create a file");

        processor.setNonRealtime(true);
        play();
        ok &= require(processor.wasDemoOfflineRenderBlocked(),
                      "Demo builds must block non-realtime rendering");
        ok &= require(audio.getMagnitude(0, 64) == 0.0f && midi.isEmpty(),
                      "offline rendering must emit neither audio nor MIDI");
        processor.setNonRealtime(false);
        play();
        ok &= require(audio.getMagnitude(0, 64) > 0.01f && ! processor.wasDemoOfflineRenderBlocked(),
                      "returning from offline rendering must restore normal trial audio");

        ok &= require(AsterDemoMode::hasStarted(),
                      "the demo timer must start on first sound");
        ok &= require(AsterDemoMode::getRemainingSeconds() > 0.0,
                      "audio time must remain immediately after the first sound");

        processor.setAutomatablePadParameter(0, PadParameterSpecs::Param::Pitch, -7.0f);
        processor.assignAutomationSlot(0, "masterVolume");
        juce::MemoryBlock state;
        processor.getStateInformation(state);
        auto stateTree = juce::ValueTree::readFromData(state.getData(), state.getSize());
        ok &= require(stateTree.getProperty("demoSessionId").toString().isNotEmpty(),
                      "Demo DAW state must contain the current process session ID");
        const auto elapsedBeforeRestore = AsterDemoMode::getElapsedSeconds();
        processor.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        ok &= require(std::abs(processor.getKit().pads[0].pitch + 7.0f) < 0.001f,
                      "same-process state restoration must retain pitch edits");
        ok &= require(processor.getAutomationSlotTargetID(0) == "masterVolume",
                      "same-process state restoration must retain automation");
        ok &= require(AsterDemoMode::getElapsedSeconds() >= elapsedBeforeRestore,
                      "state restoration must not reset the trial timer");

        stateTree.setProperty("demoSessionId", "different-daw-process", nullptr);
        juce::MemoryBlock previousSessionState;
        juce::MemoryOutputStream previousSessionStream(previousSessionState, false);
        stateTree.writeToStream(previousSessionStream);

        DrumSamplerAudioProcessor restartedProcessor;
        restartedProcessor.setAutomatablePadParameter(0, PadParameterSpecs::Param::Pitch, -11.0f);
        restartedProcessor.assignAutomationSlot(0, "masterVolume");
        restartedProcessor.setStateInformation(previousSessionState.getData(),
                                                static_cast<int>(previousSessionState.getSize()));
        const KitData defaults;
        ok &= require(std::abs(restartedProcessor.getKit().pads[0].pitch - defaults.pads[0].pitch) < 0.001f,
                      "content from a previous DAW process must reset to defaults");
        ok &= require(restartedProcessor.getAutomationSlotTargetID(0).isEmpty(),
                      "automation assignments from a previous DAW process must reset");

        if constexpr (AsterDemoMode::durationSeconds <= 5)
        {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(AsterDemoMode::durationSeconds * 1000 + 150));
            ok &= require(AsterDemoMode::hasExpired(),
                          "the shared demo timer must expire at the configured limit");
            play();
            ok &= require(audio.getMagnitude(0, 64) == 0.0f, "expired MIDI must be silent");
            processor.auditionPadOn(0, 1.0f);
            processor.auditionLayerOn(0, 0, 1.0f);
            processor.processBlock(audio, midi);
            ok &= require(audio.getMagnitude(0, 64) == 0.0f, "expired previews must be silent");
            processor.releaseResources();
            processor.prepareToPlay(44100.0, 64);
            processor.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
            play();
            ok &= require(audio.getMagnitude(0, 64) == 0.0f,
                          "prepare and same-process restore must not revive expired audio");
        }
    }
    else
    {
        ok &= require(! AsterDemoMode::hasStarted(),
                      "Full builds must not start a demo timer");
        ok &= require(! AsterDemoMode::hasExpired(),
                      "Full builds must never expire");
        processor.setNonRealtime(true);
        play();
        ok &= require(audio.getMagnitude(0, 64) > 0.01f,
                      "Full builds must still support offline rendering");
    }

    if (ok)
        std::cout << "Demo mode tests passed\n";
    return ok ? 0 : 1;
}
