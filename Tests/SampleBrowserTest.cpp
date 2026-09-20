#include "../Source/PluginProcessor.h"
#include "../Source/PadDataJson.h"
#include <iostream>
#include <cmath>

namespace {
int failures = 0;
void expect(bool result, const char* name) { if (!result) { std::cerr << "FAIL: " << name << '\n'; ++failures; } }
bool writeSample(const juce::File& file, int channels, double rate, int frames, float amplitude)
{
    auto stream = std::unique_ptr<juce::FileOutputStream>(file.createOutputStream());
    if (! stream || ! stream->openedOk()) return false;
    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(stream.get(), rate, (unsigned int) channels, 16, {}, 0));
    if (!writer) return false;
    stream.release();
    juce::AudioBuffer<float> buffer(channels, frames);
    for (int ch = 0; ch < channels; ++ch)
        for (int i = 0; i < frames; ++i) buffer.setSample(ch, i, ch == 0 ? amplitude : -amplitude);
    return writer->writeFromAudioSampleBuffer(buffer, 0, frames);
}
juce::String snapshot(DrumSamplerAudioProcessor& p) { return PadDataJson::varToJson(PadDataJson::kitToVar(p.getKit(), 0, 0, "")); }
bool near(float a, float b) { return std::abs(a - b) < 0.001f; }
}
int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("aster-browser-test-" + juce::Uuid().toString());
    folder.createDirectory();
    const auto first = folder.getChildFile("first.wav"), second = folder.getChildFile("second.wav");
    expect(writeSample(first, 1, 24000, 2400, .4f), "write mono fixture");
    expect(writeSample(second, 2, 48000, 4800, .7f), "write stereo fixture");
    auto invalid = folder.getChildFile("invalid.wav"); invalid.replaceWithText("not audio");
    expect(!decodeBrowserSample(invalid).audio, "corrupt audio rejected");
    expect(!decodeBrowserSample(first, [] { return true; }).audio, "cancelled load publishes no audio");
    expect(!decodeBrowserSample(folder.getChildFile("absent.wav")).audio, "missing file rejected");
    {
        DrumSamplerAudioProcessor p;
        p.prepareToPlay(48000, 256);
        p.clearKitDirty();
        const auto before = snapshot(p);
        p.browserPreview.setGain(.5f);
        p.browserPreview.start(decodeBrowserSample(first));
        juce::AudioBuffer<float> out(p.getTotalNumOutputChannels(), 256);
        juce::MidiBuffer midi;
        p.processBlock(out, midi);
        expect(out.getMagnitude(0, 150, 106) > .15f, "audition renders without MIDI or pad samples");
        expect(near(out.getSample(0, 200), out.getSample(1, 200)), "mono audition duplicated to stereo");
        for (int ch = 2; ch < out.getNumChannels(); ++ch)
            expect(out.getMagnitude(ch, 0, 256) == 0, "audition stays on main bus");
        expect(snapshot(p) == before && !p.isKitDirty(), "audition leaves kit and dirty flag unchanged");
        p.browserPreview.stop(); p.processBlock(out, midi);
        expect(!p.browserPreview.isPlaying() && out.getMagnitude(0, 200, 56) == 0, "stop fades to silence");
        p.browserPreview.start(decodeBrowserSample(first));
        int blocks = 0;
        while (p.browserPreview.isPlaying() && blocks < 30) { p.processBlock(out, midi); ++blocks; }
        expect(blocks == 19, "24k source has correct duration at 48k host");
        p.browserPreview.start(decodeBrowserSample(second)); p.processBlock(out, midi);
        expect(out.getSample(0, 200) > 0 && out.getSample(1, 200) < 0, "stereo channels preserved");
        p.setNonRealtime(true); p.processBlock(out, midi);
        expect(out.getMagnitude(0, 0, 256) == 0 && !p.browserPreview.isPlaying(), "audition excluded from offline export");
        p.setNonRealtime(false); p.processBlock(out, midi);
        expect(out.getMagnitude(0, 0, 256) == 0, "audition does not resume after offline export");
        p.browserPreview.start(decodeBrowserSample(second));
        p.setMasterVolumeParameter(0, false); p.processBlock(out, midi); p.processBlock(out, midi);
        expect(out.getMagnitude(0, 0, 256) == 0, "master volume controls audition output");
        p.releaseResources(); expect(!p.browserPreview.isPlaying(), "release stops preview");
    }
    {
        DrumSamplerAudioProcessor p;
        p.prepareToPlay(48000, 256);
        // Exercise the full 48-pad, 8-layer destination range, including Layer 1.
        for (auto& pad : p.getKit().pads)
            while (pad.layerCount() < MAX_LAYERS_PER_PAD) pad.layers.emplace_back();
        p.syncParametersFromKit();
        for (int pad = 0; pad < NUM_PADS; ++pad)
            for (int layer = 0; layer < MAX_LAYERS_PER_PAD; ++layer)
            {
                if (layer == 0) // MAIN uses legacy pad IDs, matching sendPadPatchToJuce.
                {
                    p.setAutomatablePadParameter(pad, PadParameterSpecs::Param::Volume, .32f, false);
                    p.setAutomatablePadParameter(pad, PadParameterSpecs::Param::Pan, -.24f, false);
                    p.setAutomatablePadParameter(pad, PadParameterSpecs::Param::Pitch, 7.0f, false);
                }
                else
                {
                    p.setAutomatableLayerParameter(pad, layer, LayerParameterSpecs::Param::Volume, .32f, false);
                    p.setAutomatableLayerParameter(pad, layer, LayerParameterSpecs::Param::Pan, -.24f, false);
                    p.setAutomatableLayerParameter(pad, layer, LayerParameterSpecs::Param::Pitch, 7.0f, false);
                }
                expect(p.commitBrowserSample(pad, layer, first, false, 0, 0, decodeBrowserSample(first)), "add to valid destination");
                const auto& actual = p.getKit().pads[(size_t)pad].layers[(size_t)layer];
                expect(near(actual.volume, .32f) && near(actual.pan, -.24f) && near(actual.pitch, 7.0f), "add preserves layer sound controls");
                expect(actual.sampleStock.size() == 1 && actual.sampleFilePath == first.getFullPathName(), "sample stored on exact destination");
            }
        for (int i = 1; i < 5; ++i)
            expect(p.commitBrowserSample(4, 1, first, false, i, i - 1, decodeBrowserSample(first)), "append bank through slot five");
        const auto full = snapshot(p);
        expect(!p.commitBrowserSample(4, 1, second, false, 5, 4, decodeBrowserSample(second)), "full ADD cannot replace");
        expect(snapshot(p) == full, "rejected ADD leaves entire kit unchanged");
        p.selectLayerSampleStock(4, 1, 1);
        auto& target = p.getKit().pads[4].layers[1];
        target.roundRobin = true; target.mute = true; target.solo = true; target.reverse = true;
        p.syncParametersFromKit();
        expect(p.commitBrowserSample(4, 1, second, true, 5, 1, decodeBrowserSample(second)), "replace explicitly selected slot");
        expect(target.sampleStock.size() == 5 && target.sampleStock[1].sampleFilePath == second.getFullPathName()
               && target.sampleStock[0].sampleFilePath == first.getFullPathName(), "replace leaves siblings intact");
        expect(target.roundRobin && target.mute && target.solo && target.reverse && near(target.volume,.32f) && near(target.pitch,7), "replace preserves playback and sound settings");
        const auto identity = p.browserTargetToken(4, 1);
        p.setAutomatableLayerParameter(4, 1, LayerParameterSpecs::Param::Pan, .5f, false);
        expect(p.browserTargetToken(4, 1) == identity, "automation does not invalidate import identity");
        const auto replaced = snapshot(p);
        expect(!p.commitBrowserSample(4, 1, first, true, 5, 4, decodeBrowserSample(first)), "stale active slot rejected");
        expect(!p.commitBrowserSample(48, 0, first, false, 0, 0, decodeBrowserSample(first)), "invalid pad rejected");
        expect(!p.commitBrowserSample(4, 8, first, false, 0, 0, decodeBrowserSample(first)), "invalid layer rejected");
        expect(snapshot(p) == replaced, "invalid requests leave kit unchanged");
        juce::MemoryBlock state; p.getStateInformation(state);
        DrumSamplerAudioProcessor restored; restored.setStateInformation(state.getData(), (int)state.getSize());
        const auto& reloaded = restored.getKit().pads[4].layers[1];
        expect(reloaded.sampleStock.size() == 5 && reloaded.activeSampleStockIndex == 1 && reloaded.roundRobin
               && reloaded.sampleFilePath == second.getFullPathName() && near(reloaded.pitch, 7), "browser imports survive session restore");
        expect(restored.getFileManager().hasSample(47, 7, 0), "last pad/layer audio restored");
        p.setStateInformation(state.getData(), (int)state.getSize());
        expect(p.browserTargetToken(4, 1) != identity, "even identical session restore invalidates pending imports");
    }
    folder.deleteRecursively();
    if (failures) return 1;
    std::cout << "Sample browser: preview isolation, routing, resampling, stop/export, all 384 destinations, five-slot add/replace and persistence passed\n";
    return 0;
}
