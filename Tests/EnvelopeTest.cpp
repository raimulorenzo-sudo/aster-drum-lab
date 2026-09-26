#include "../Source/SamplerVoice.h"

#include <cmath>
#include <iostream>

namespace
{
bool check(bool condition, const char* message)
{
    if (! condition)
        std::cerr << "FAIL: " << message << '\n';
    return condition;
}

bool approximately(float actual, float expected, float tolerance = 0.025f)
{
    return std::abs(actual - expected) <= tolerance;
}

juce::AudioBuffer<float> makeConstantSource(int samples)
{
    juce::AudioBuffer<float> source(2, samples);
    source.clear();
    for (int channel = 0; channel < source.getNumChannels(); ++channel)
        for (int sample = 0; sample < samples; ++sample)
            source.setSample(channel, sample, 1.0f);
    return source;
}

void startVoice(DrumVoice& voice,
                int sourceSamples,
                double sampleRate,
                bool oneShot,
                float attack,
                float release,
                float hold,
                float decay,
                bool keepLength = false)
{
    voice.prepare(sampleRate, 256);
    voice.start(0,
                0.0,
                static_cast<double>(sourceSamples),
                1.0f,
                1.0f,
                oneShot,
                attack,
                release,
                sampleRate,
                1.0,
                false,
                0,
                0,
                static_cast<double>(sourceSamples),
                1,
                0,
                false,
                false,
                keepLength,
                1.0,
                0.0f,
                0.0f,
                hold,
                decay);
}
}

int main()
{
    bool ok = true;

    // Missing v7 fields must load as FULL so old kits retain their exact
    // sample-length One Shot behaviour.
    LayerData defaults;
    ok &= check(defaults.hold < 0.0f, "new layers do not default to HOLD FULL");
    juce::ValueTree legacyLayer { "Layer" };
    legacyLayer.setProperty("attack", 0.25f, nullptr);
    legacyLayer.setProperty("release", 0.4f, nullptr);
    LayerData migrated;
    migrated.fromValueTree(legacyLayer);
    ok &= check(migrated.hold < 0.0f, "legacy layer did not migrate to HOLD FULL");
    ok &= check(approximately(migrated.decay, 0.05f, 0.0001f),
                "legacy layer did not receive the neutral decay default");

    LayerData saved;
    saved.hold = 0.125f;
    saved.decay = 0.375f;
    LayerData restored;
    restored.fromValueTree(saved.toValueTree());
    ok &= check(approximately(restored.hold, saved.hold, 0.0001f)
                && approximately(restored.decay, saved.decay, 0.0001f),
                "Hold/Decay ValueTree round-trip changed values");

    constexpr double simpleRate = 1000.0;
    constexpr int simpleLength = 1000;
    const auto simpleSource = makeConstantSource(simpleLength);
    LayerData neutralLayer;

    // FULL uses the legacy unity path and must remain at unity until sample end.
    DrumVoice legacyVoice;
    startVoice(legacyVoice, simpleLength, simpleRate, true, 0.0f, 0.1f, -1.0f, 0.1f);
    legacyVoice.triggerRelease(); // Note Off remains ignored in One Shot.
    juce::AudioBuffer<float> legacyOutput(2, simpleLength);
    legacyOutput.clear();
    legacyVoice.render(simpleSource, legacyOutput, 0, simpleLength,
                       neutralLayer, simpleRate, 0.0f);
    ok &= check(approximately(legacyOutput.getSample(0, 0), 1.0f, 0.0001f)
                && approximately(legacyOutput.getSample(0, simpleLength - 1), 1.0f, 0.0001f),
                "HOLD FULL changed legacy One Shot gain");
    ok &= check(! legacyVoice.isActive, "HOLD FULL did not end at the sample boundary");

    // Finite Hold opts into AHD: 100 samples full, then 100 samples linear decay.
    DrumVoice ahdVoice;
    startVoice(ahdVoice, simpleLength, simpleRate, true, 0.0f, 0.1f, 0.1f, 0.1f);
    juce::AudioBuffer<float> ahdOutput(2, simpleLength);
    ahdOutput.clear();
    ahdVoice.render(simpleSource, ahdOutput, 0, simpleLength,
                    neutralLayer, simpleRate, 0.0f);
    ok &= check(approximately(ahdOutput.getSample(0, 50), 1.0f),
                "finite Hold did not preserve the full-level section");
    ok &= check(approximately(ahdOutput.getSample(0, 149), 0.50f, 0.04f),
                "One Shot decay did not reach its expected midpoint");
    ok &= check(approximately(ahdOutput.getSample(0, 250), 0.0f, 0.0001f)
                && ! ahdVoice.isActive,
                "finite One Shot did not stop after Decay");

    // Gate ignores Hold/Decay and only enters Release after Note Off.
    DrumVoice gateVoice;
    startVoice(gateVoice, simpleLength, simpleRate, false, 0.0f, 0.1f, 0.0f, 0.01f);
    juce::AudioBuffer<float> gateBeforeNoteOff(2, 200);
    gateBeforeNoteOff.clear();
    gateVoice.render(simpleSource, gateBeforeNoteOff, 0, 200,
                     neutralLayer, simpleRate, 0.0f);
    ok &= check(gateVoice.isActive
                && approximately(gateBeforeNoteOff.getSample(0, 199), 1.0f, 0.0001f),
                "Gate incorrectly applied One Shot Hold/Decay");
    gateVoice.triggerRelease();
    juce::AudioBuffer<float> gateRelease(2, 150);
    gateRelease.clear();
    gateVoice.render(simpleSource, gateRelease, 0, 150,
                     neutralLayer, simpleRate, 0.0f);
    ok &= check(approximately(gateRelease.getSample(0, 49), 0.50f, 0.04f),
                "Gate release curve changed");
    ok &= check(! gateVoice.isActive
                && approximately(gateRelease.getSample(0, 120), 0.0f, 0.0001f),
                "Gate did not stop after Release");

    // The separate KEEP LENGTH render branch must advance the same AHD state.
    constexpr double keepLengthRate = 48000.0;
    constexpr int keepLengthSamples = 4800;
    const auto keepLengthSource = makeConstantSource(keepLengthSamples);
    DrumVoice keepLengthVoice;
    startVoice(keepLengthVoice, keepLengthSamples, keepLengthRate,
               true, 0.0f, 0.1f, 0.01f, 0.01f, true);
    juce::AudioBuffer<float> keepLengthOutput(2, keepLengthSamples);
    keepLengthOutput.clear();
    int rendered = 0;
    while (rendered < keepLengthSamples && keepLengthVoice.isActive)
    {
        const int count = juce::jmin(256, keepLengthSamples - rendered);
        keepLengthVoice.render(keepLengthSource, keepLengthOutput, rendered, count,
                               neutralLayer, keepLengthRate, 0.0f);
        rendered += count;
    }
    ok &= check(! keepLengthVoice.isActive,
                "KEEP LENGTH path did not stop after finite Hold/Decay");
    ok &= check(keepLengthOutput.getMagnitude(0, 1600, 256) < 0.0001f,
                "KEEP LENGTH path remained audible after finite Decay");

    if (ok)
        std::cout << "Envelope tests passed\n";
    return ok ? 0 : 1;
}
