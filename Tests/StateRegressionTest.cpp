#include "../Source/PluginProcessor.h"
#include "../Source/PadDataJson.h"
#include "RecentKitGuard.h"
#include <cmath>
#include <limits>
#include <iostream>
#include <thread>

namespace {
int failures = 0;
void expect(bool ok, const char* message) { if (!ok) { ++failures; std::cerr << "FAIL: " << message << '\n'; } }
bool near(float a, float b) { return std::abs(a-b) < 0.0001f; }
bool writeSample(const juce::File& file, float value) {
    auto stream = file.createOutputStream(); juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(stream.get(),48000,1,16,{},0));
    if(!writer) return false; stream.release(); juce::AudioBuffer<float> source(1,4800);
    for(int i=0;i<4800;++i) source.setSample(0,i,value);
    return writer->writeFromAudioSampleBuffer(source,0,4800);
}
bool add(DrumSamplerAudioProcessor& p,int layer,const juce::File& file,int count=0) {
    return p.commitBrowserSample(0,layer,file,false,count,count?count-1:0,decodeBrowserSample(file));
}
float render(DrumSamplerAudioProcessor& p,bool note=false) {
    juce::AudioBuffer<float> out(p.getTotalNumOutputChannels(),256); juce::MidiBuffer midi;
    if(note) midi.addEvent(juce::MidiMessage::noteOn(1,36,(juce::uint8)127),0);
    p.processBlock(out,midi); float peak=0;
    for(int c=0;c<out.getNumChannels();++c) for(int i=0;i<256;++i) {
        auto v=out.getSample(c,i); if(!std::isfinite(v)) return -1; peak=std::max(peak,std::abs(v));
    }
    return peak;
}
juce::var stateItem(int index,const juce::String& state) {
    auto* o=new juce::DynamicObject();o->setProperty("index",index);o->setProperty("state",state);return juce::var(o);
}
}
int main() {
    juce::ScopedJuceInitialiser_GUI init; RecentKitGuard recent;
    auto folder=juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("aster-state-regression-"+juce::Uuid().toString());folder.createDirectory();
    auto first=folder.getChildFile("a.wav"),second=folder.getChildFile("b.wav");
    expect(writeSample(first,.2f)&&writeSample(second,.5f),"fixture WAVs created");
    {
        KitData source;auto& pad=source.pads[0];pad.polyphony=3;pad.voiceSteal=PadData::VoiceStealMode::Quietest;
        auto& l=pad.layers[0];l.hold=.12f;l.decay=.3f;l.polarityInvert=true;
        LayerFxSlot fx;fx.type=LayerFxType::Drive;fx.drive.amount=.8f;l.fxChain={fx};pad.syncFlatFromLayer0();
        auto file=folder.getChildFile("one-layer.tree");{juce::FileOutputStream stream(file);source.toValueTree().writeToStream(stream);}
        for(int category=0;category<3;++category) {
            DrumSamplerAudioProcessor p;p.getKit().pads[0].layers.resize(3);p.syncParametersFromKit();expect(add(p,1,first)&&add(p,2,second),"partial load samples created");
            DrumSamplerAudioProcessor::KitLoadOptions o;o.samples=false;o.padNamesAndColours=category==0;o.padParameters=category==1;o.mixerSettings=category==2;o.routing=false;
            expect(p.loadKitFromFile(file,o),"partial load succeeds");const auto& current=p.getKit().pads[0];
            expect(current.layerCount()==3&&current.layers[2].sampleFilePath==second.getFullPathName()&&p.getFileManager().hasSample(0,2),"non-sample category never removes sample layers");
            if(category==1) expect(current.layers[0].fxChain.size()==1&&current.layers[0].polarityInvert&&current.polyphony==3&&current.voiceSteal==PadData::VoiceStealMode::Quietest,"partial playback includes FX, polarity and polyphony");
            expect(p.applyDefaultKit(o)&&p.getKit().pads[0].layerCount()==3&&p.getFileManager().hasSample(0,2),"partial Default preserves additional samples");
        }
    }
    {
        DrumSamplerAudioProcessor p;p.prepareToPlay(48000,256);p.getKit().pads[0].layers.resize(2);p.syncParametersFromKit();
        expect(add(p,0,first)&&add(p,0,second,1)&&add(p,1,second),"undo bank fixture loaded");
        auto& pad=p.getKit().pads[0];pad.layers[1].hold=.1f;pad.layers[1].decay=.2f;pad.layers[1].roundRobin=true;
        pad.layers[0].sampleStock[0].startPosition=.2f;pad.layers[0].sampleStock[0].endPosition=.8f;
        const auto original=PadDataJson::padToVar(pad)["nativeUndoState"].toString();
        p.clearLayerSample(0,0);pad.layers.resize(1);p.syncParametersFromKit();
        expect(p.restorePadUndoStates({stateItem(0,original)}),"native Undo restores complete snapshot");
        expect(pad.layerCount()==2&&pad.layers[0].sampleStock.size()==2&&pad.layers[0].activeSampleStockIndex==1&&p.getFileManager().hasSample(0,0,1)&&near(pad.layers[1].hold,.1f),"Undo restores bank, active slot, layer structure and envelope");
        expect(near(pad.layers[0].sampleStock[0].startPosition,.2f)&&near(pad.layers[0].sampleStock[0].endPosition,.8f),"Undo preserves inactive variation trim");
        const auto before=pad.toValueTree();
        expect(!p.restorePadUndoStates({stateItem(0,original),stateItem(1,"invalid")})&&before.isEquivalentTo(pad.toValueTree()),"invalid Undo is rejected before any changes");
        expect(!p.restorePadUndoStates({stateItem(0,original),stateItem(0,original)}),"duplicate Undo destinations rejected");
        auto projected = stateItem(0,original);
        // Keep the JSON owner alive: its per-layer snapshots are opaque.
        auto json = PadDataJson::padToVar(pad);
        auto* layerStates = json["layers"].getArray();
        auto project = [&](int source, bool empty) {
            auto* item = new juce::DynamicObject();
            item->setProperty("state",(*layerStates)[source]["nativeUndoState"]);
            item->setProperty("layerName","Restored layer");
            item->setProperty("activeSampleStockIndex",empty?0:1);
            item->setProperty("sampleStock",empty?juce::var(juce::Array<juce::var>{}):(*layerStates)[source]["sampleStock"]);
            return juce::var(item);
        };
        projected.getDynamicObject()->setProperty("layers",juce::Array<juce::var>{project(1,false),project(0,true)});
        expect(p.restorePadUndoStates({projected})&&pad.layerCount()==2&&pad.layers[0].sampleFilePath==second.getFullPathName()&&pad.layers[1].sampleStock.empty()&&!p.getFileManager().hasSample(0,1),"immediate Redo restores moved layers and a new empty layer without stale audio");
        expect(p.restorePadUndoStates({stateItem(0,original)}),"original bank restored after structural Redo test");
        p.resetPadSettings(0);
        expect(pad.layerCount()==2&&pad.layers[0].sampleStock.size()==2&&pad.layers[0].activeSampleStockIndex==1&&pad.layers[0].sampleFilePath==second.getFullPathName(),"native settings reset preserves every sample and active slot");
        p.getVoiceManager().allNotesOff();expect(render(p,true)>.6f,"reset renders the selected samples rather than stale slot zero");
    }
    {
        DrumSamplerAudioProcessor p;p.prepareToPlay(48000,256);
        expect(add(p,0,first)&&add(p,0,second,1),"prepared Undo fixture loaded");
        const float expected=render(p,true);p.getVoiceManager().allNotesOff();
        const auto original=PadDataJson::padToVar(p.getKit().pads[0])["nativeUndoState"].toString();
        expect(p.removeLayerSampleStock(0,0,1),"prepared Undo starts with a smaller bank");
        auto existing=p.getFileManager().getSharedBufferNoLock(0,0,0);
        auto hiddenFirst=folder.getChildFile("a-hidden.wav"),hiddenSecond=folder.getChildFile("b-hidden.wav");
        expect(first.moveFileTo(hiddenFirst),"temporarily hide an already loaded sample");
        auto prepared=p.preparePadUndoStates({stateItem(0,original)});
        expect(prepared&&p.getKit().pads[0].layers[0].sampleStock.size()==1,"Undo preparation leaves the playing kit unchanged");
        expect(prepared&&prepared->entries[0].samples[0][0].audio==existing,"Undo reuses loaded audio even when its source is offline");
        expect(second.moveFileTo(hiddenSecond),"hide new sample after preparation");
        if(prepared) p.applyPreparedPadUndoStates(*prepared);
        expect(p.getKit().pads[0].layers[0].sampleStock.size()==2&&p.getFileManager().hasSample(0,0,1)&&expected>0&&near(render(p,true),expected),"atomic Undo commit plays prepared audio without reading files");
        expect(hiddenFirst.moveFileTo(first)&&hiddenSecond.moveFileTo(second),"restore prepared Undo fixture paths");
    }
    {
        DrumSamplerAudioProcessor p;p.prepareToPlay(48000,256);
        const auto id=PadParameterSpecs::parameterID(47,PadParameterSpecs::Param::Pitch);
        for(auto* parameter:p.getParameters())
            if(auto* ranged=dynamic_cast<juce::RangedAudioParameter*>(parameter);ranged&&ranged->getParameterID()==id)
                ranged->setValueNotifyingHost(ranged->convertTo0to1(9.0f));
        expect(add(p,0,first),"Browser commits while another pad has pending host automation");
        render(p);
        expect(near(p.getKit().pads[47].pitch,9.0f),"local parameter sync preserves pending host automation on another pad");
    }
    {
        DrumSamplerAudioProcessor p;p.prepareToPlay(48000,256);expect(add(p,0,first),"mute fixture loaded");
        for(bool solo:{false,true}) {
            p.getVoiceManager().allNotesOff();render(p,true);
            p.setAutomatablePadParameter(solo?1:0,solo?PadParameterSpecs::Param::Solo:PadParameterSpecs::Param::Mute,1,false);
            for(int i=0;i<24;++i)render(p);
            p.setAutomatablePadParameter(solo?1:0,solo?PadParameterSpecs::Param::Solo:PadParameterSpecs::Param::Mute,0,false);
            expect(render(p)==0,"Mute/Solo exclusion cannot resume an expired hit");
        }
        render(p,true);p.getKit().pads[0].layers[0].mute=true;render(p);p.getKit().pads[0].layers[0].mute=false;expect(render(p)==0,"layer mute stops its active voice");
    }
    {
        DrumSamplerAudioProcessor p;p.prepareToPlay(48000,256);
        expect(add(p,0,first)&&add(p,0,second,1)&&p.selectLayerSampleStock(0,0,0),"playing variation fixture loaded");
        expect(render(p,true)>0,"variation starts playing");
        expect(p.removeLayerSampleStock(0,0,0)&&render(p)==0,"removing a playing variation cannot switch its tail to the next file");
        render(p,true);
        expect(p.commitBrowserSample(0,0,first,true,1,0,decodeBrowserSample(first))&&render(p)==0,"Browser replacement cannot reuse an old voice position");
    }
    {
        auto invalid=folder.getChildFile("non-finite.wav");{
            juce::FileOutputStream s(invalid);s.write("RIFF",4);s.writeInt(52);s.write("WAVEfmt ",8);s.writeInt(16);s.writeShort(3);s.writeShort(1);s.writeInt(48000);s.writeInt(192000);s.writeShort(4);s.writeShort(32);s.write("data",4);s.writeInt(16);
            s.writeFloat(std::numeric_limits<float>::quiet_NaN());s.writeFloat(std::numeric_limits<float>::infinity());s.writeFloat(-std::numeric_limits<float>::infinity());s.writeFloat(.25f);
        }
        DrumSamplerAudioProcessor p;p.prepareToPlay(48000,256);p.getKit().pads[0].layers[0].smartTrim=false;expect(add(p,0,invalid),"browser accepts sanitized WAV");expect(render(p,true)>=0,"initial WAV playback finite");
        juce::MemoryBlock state;p.getStateInformation(state);DrumSamplerAudioProcessor restored;restored.setStateInformation(state.getData(),(int)state.getSize());restored.prepareToPlay(48000,256);expect(render(restored,true)>=0,"WAV playback finite after recall");
        render(p,true);p.setStateInformation(state.getData(),(int)state.getSize());expect(render(p)==0,"recall never continues an old voice with a new sample");
        auto bad=folder.getChildFile("corrupt.wav");bad.replaceWithText("not audio");expect(!p.getFileManager().loadFileForPadVariation(0,0,0,bad)&&p.getFileManager().hasSample(0,0),"failed file load preserves previous audio buffer");
    }
    {
        DrumSamplerAudioProcessor p;p.prepareToPlay(48000,256);expect(add(p,0,first),"concurrent mutation fixture loaded");p.copyPad(0);
        std::atomic<bool> stop {false}, finite {true};std::atomic<int> blocks {0};
        std::thread audio([&] {
            // JUCE's AU/VST3 wrappers hold this lock around processBlock.
            while(!stop.load()) {
                const juce::ScopedLock callback(p.getCallbackLock());
                if(render(p,(blocks.load()%8)==0)<0) finite.store(false);
                ++blocks;
            }
        });
        for(int i=0;i<30;++i) {
            p.clearPadFull(0);p.pastePad(0);p.resetPadSettings(0);
            expect(p.addOrReplaceLayerSampleStock(0,0,second),"concurrent sample addition succeeds");
            expect(p.selectLayerSampleStock(0,0,0)&&p.removeLayerSampleStock(0,0,1),"concurrent variation selection/removal succeeds");
        }
        stop.store(true);audio.join();expect(blocks.load()>0&&finite.load(),"audio remains finite during settings, structure and bank changes");
    }
    {
        DrumSamplerAudioProcessor p;
        const auto volumeId = PadParameterSpecs::parameterID(0, PadParameterSpecs::Param::Volume);
        const auto panId = PadParameterSpecs::parameterID(0, PadParameterSpecs::Param::Pan);
        auto pair = [&](bool high) {
            const juce::ScopedLock callback(p.getCallbackLock());
            p.setAutomatablePadParameter(0, PadParameterSpecs::Param::Volume, high ? .8f : .2f, false);
            p.setAutomatablePadParameter(0, PadParameterSpecs::Param::Pan, high ? .5f : -.5f, false);
        };
        pair(false);
        std::atomic<bool> stop {false};
        std::thread writer([&] { bool high = false; while (!stop.load()) { pair(high); high = !high; } });
        bool coherent = true;
        juce::MemoryBlock last;
        for (int i = 0; i < 100; ++i)
        {
            juce::MemoryBlock state; p.getStateInformation(state);
            auto root = juce::ValueTree::readFromData(state.getData(), state.getSize());
            const auto layer = root.getChildWithName("Kit").getChild(0).getChildWithName("Layers").getChild(0);
            const float volume = layer["volume"], pan = layer["pan"];
            coherent &= (near(volume,.8f) && near(pan,.5f)) || (near(volume,.2f) && near(pan,-.5f));
            const auto parameters = root.getChildWithName("PARAMETERS");
            for (int j=0; j<parameters.getNumChildren(); ++j) {
                const auto item = parameters.getChild(j);
                if (item["id"].toString() == volumeId) coherent &= near((float) item["value"],volume);
                if (item["id"].toString() == panId) coherent &= near((float) item["value"],pan);
            }
            last = state;
        }
        stop.store(true); writer.join();
        expect(coherent,"concurrent DAW saves freeze complete Layer and APVTS transactions");
        DrumSamplerAudioProcessor restored; restored.setStateInformation(last.getData(),(int)last.getSize());
        const auto& layer = restored.getKit().pads[0].layers[0];
        expect((near(layer.volume,.8f)&&near(layer.pan,.5f))||(near(layer.volume,.2f)&&near(layer.pan,-.5f)),"DAW recall preserves frozen control pair");
    }
    folder.deleteRecursively();std::cout<<"State regression failures: "<<failures<<'\n';return failures?1:0;
}
