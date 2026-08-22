#include "../Source/DemoMode.h"
#include "../Source/PluginProcessor.h"

#include <chrono>
#include <iostream>
#include <thread>

namespace
{
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

    AsterDemoMode::beginOnFirstSound();

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

        DrumSamplerAudioProcessor processor;
        processor.prepareToPlay(48000.0, 64);

        const auto saveProbe = juce::File::getSpecialLocation(juce::File::tempDirectory)
            .getNonexistentChildFile("aster-demo-save-probe", ".asterkit", false);
        ok &= require(! processor.saveKitToFile(saveProbe),
                      "Demo builds must reject ASTER kit saving");
        ok &= require(! saveProbe.existsAsFile(),
                      "a rejected Demo save must not create a file");

        processor.setNonRealtime(true);
        juce::AudioBuffer<float> offlineBuffer(
            juce::jmax(2, processor.getTotalNumOutputChannels()), 64);
        juce::MidiBuffer offlineMidi;
        processor.processBlock(offlineBuffer, offlineMidi);
        ok &= require(processor.wasDemoOfflineRenderBlocked(),
                      "Demo builds must block non-realtime rendering");

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
        ok &= require(restartedProcessor.getKit().pads[0].pitch == defaults.pads[0].pitch,
                      "content from a previous DAW process must reset to defaults");
        ok &= require(restartedProcessor.getAutomationSlotTargetID(0).isEmpty(),
                      "automation assignments from a previous DAW process must reset");

        if constexpr (AsterDemoMode::durationSeconds <= 5)
        {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(AsterDemoMode::durationSeconds * 1000 + 150));
            ok &= require(AsterDemoMode::hasExpired(),
                          "the shared demo timer must expire at the configured limit");
        }
    }
    else
    {
        ok &= require(! AsterDemoMode::hasStarted(),
                      "Full builds must not start a demo timer");
        ok &= require(! AsterDemoMode::hasExpired(),
                      "Full builds must never expire");
    }

    if (ok)
        std::cout << "Demo mode tests passed\n";
    return ok ? 0 : 1;
}
