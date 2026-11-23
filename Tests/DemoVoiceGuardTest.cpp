#include "../Source/DemoMode.h"
#include "../Source/VoiceManager.h"

#include <chrono>
#include <iostream>
#include <thread>

namespace
{
bool require(bool condition, const char* message)
{
    if (! condition)
        std::cerr << "FAIL: " << message << '\n';
    return condition;
}

bool writeSample(const juce::File& file)
{
    auto stream = file.createOutputStream();
    if (stream == nullptr || ! stream->openedOk())
        return false;
    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatWriter> writer(
        format.createWriterFor(stream.get(), 48000.0, 1, 16, {}, 0));
    if (writer == nullptr)
        return false;
    stream.release();
    juce::AudioBuffer<float> source(1, 48000);
    for (int i = 0; i < source.getNumSamples(); ++i)
        source.setSample(0, i, 0.5f);
    return writer->writeFromAudioSampleBuffer(source, 0, source.getNumSamples());
}
}

int main(int argc, char**)
{
    juce::ScopedJuceInitialiser_GUI initialiser;
    const bool preview = argc > 1;
    bool ok = true;
    juce::TemporaryFile sample(".wav");
    if (! writeSample(sample.getFile()))
        return 1;

    AudioFileManager files;
    KitData kit;
    auto voices = std::make_unique<VoiceManager>();
    auto otherInstance = std::make_unique<VoiceManager>();
    voices->prepare(48000.0, 64);
    otherInstance->prepare(48000.0, 64);

    // Exercise the engine without PluginProcessor, so the usual final output
    // mute and DemoMode timer cannot mask a broken independent guard.
    const auto trigger = [&] (VoiceManager& manager)
    {
        if (preview)
            manager.previewNoteOn(0, 1.0f, kit, files, 48000.0);
        else
            manager.noteOn(0, 1.0f, kit, files, 48000.0);
    };
    trigger(*voices); // no sample
    ok &= require(! voices->hasActiveVoices(), "empty pads must not produce a voice");
    if (! files.loadFileForPad(0, sample.getFile()))
        return 1;
    kit.pads[0].mute = true;
    trigger(*voices);
    ok &= require(! voices->hasActiveVoices(), "muted pads must not produce a voice");
    kit.pads[0].mute = false;
    kit.pads[0].layers[0].mute = true;
    trigger(*voices);
    kit.pads[0].layers[0].mute = false;

    if constexpr (AsterDemoMode::isDemoBuild && AsterDemoMode::durationSeconds <= 5)
        std::this_thread::sleep_for(std::chrono::milliseconds(
            AsterDemoMode::durationSeconds * 1000 + 250));

    std::array<juce::AudioBuffer<float>, NUM_OUTPUTS> output;
    std::array<juce::AudioBuffer<float>*, NUM_OUTPUTS> buses;
    for (size_t i = 0; i < buses.size(); ++i)
    {
        output[i].setSize(2, 64);
        buses[i] = &output[i];
    }
    const auto render = [&] (VoiceManager& manager)
    {
        for (auto& buffer : output)
            buffer.clear();
        manager.beginProcessBlock();
        manager.process(buses.data(), NUM_OUTPUTS, OutputMode::Outs48,
                        kit, files, 0, 64, 48000.0);
    };

    // Main and all 47 individual outputs must remain audible before expiry.
    for (int bus = 0; bus < NUM_OUTPUTS; ++bus)
    {
        kit.pads[0].outputAssign = bus;
        voices->allNotesOff();
        trigger(*voices);
        render(*voices);
        ok &= require(output[static_cast<size_t>(bus)].getMagnitude(0, 64) > 0.01f,
                      "all output routes must be audible before expiry");
    }
    trigger(*otherInstance);
    render(*otherInstance);
    ok &= require(otherInstance->hasActiveVoices(), "another instance must work before expiry");
    ok &= require(! AsterDemoMode::hasStarted(), "the processor clock must remain unused in this test");

    if constexpr (AsterDemoMode::isDemoBuild && AsterDemoMode::durationSeconds <= 5)
    {
        // No audio callbacks during this wait: transport suspension must not
        // freeze/reset the wall-time deadline. Existing voices are still active.
        std::this_thread::sleep_for(std::chrono::milliseconds(
            AsterDemoMode::durationSeconds * 1000 + 250));
        render(*voices);
        render(*otherInstance);
        ok &= require(! voices->hasActiveVoices() && ! otherInstance->hasActiveVoices(),
                      "expired sustained voices must stop in every instance");
        for (const auto& buffer : output)
            ok &= require(buffer.getMagnitude(0, 64) == 0.0f, "expired outputs must be silent");

        voices->prepare(44100.0, 128);
        trigger(*voices);
        ok &= require(! voices->hasActiveVoices(), "prepare/sample-rate changes must not reset expiry");
        voices.reset();
        auto replacement = std::make_unique<VoiceManager>();
        replacement->prepare(48000.0, 64);
        replacement->noteOn(0, 1.0f, kit, files, 48000.0);
        replacement->previewNoteOn(0, 1.0f, kit, files, 48000.0);
        ok &= require(! replacement->hasActiveVoices(), "new instances must reject MIDI and preview after expiry");
    }
    if (ok)
        std::cout << "Voice guard tests passed (" << (preview ? "preview" : "MIDI") << ")\n";
    return ok ? 0 : 1;
}
