#include "../Source/KeepLengthRenderCache.h"
#include <cstdlib>
#include <new>
#include <iostream>
#include <cmath>

namespace {
thread_local bool track = false;
thread_local size_t allocationCount = 0, allocationBytes = 0, deletionCount = 0;
int failures = 0;
void check(bool ok, const char* text) { if (!ok) { ++failures; std::cerr << "FAIL " << text << '\n'; } }
}
void* operator new(std::size_t bytes) {
    if (track) { ++allocationCount; allocationBytes += bytes; }
    if (auto* memory = std::malloc(bytes ? bytes : 1)) return memory;
    throw std::bad_alloc();
}
void operator delete(void* memory) noexcept { if (track && memory) ++deletionCount; std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { if (track && memory) ++deletionCount; std::free(memory); }

int main() {
    int cases = 0;
    for (double rate : {44100., 48000., 96000., 192000., 384000.}) for (int block : {64,256}) for (int kind=0; kind<4; ++kind) {
        const double sourceRate = kind==2 ? 44100. : rate;
        const int length = (int)(sourceRate*(kind==3 ? 9. : kind<2 ? .009 : .23));
        auto source = std::make_shared<juce::AudioBuffer<float>>(kind%2+1,length);
        for(int c=0;c<source->getNumChannels();++c)for(int i=0;i<length;++i)
            source->setSample(c,i,.25f*std::sin((float)(i*2*juce::MathConstants<double>::pi*(kind==1?3000:80+193*c)/sourceRate))*std::exp((float)(-i/sourceRate)));
        const double from=kind==2 ? 11.25 : 0., to=length-.75, step=sourceRate/rate;
        const bool reverse=kind%2!=0; const float pitch=kind%2 ? -12.13f : 7.17f;
        allocationBytes=0;track=true;
        auto state=std::make_shared<KeepLengthPreparedState>();
        state->sampleRate=rate;state->source=source;state->start=from;state->end=to;state->step=step;state->reverse=reverse;state->pitch=pitch;
        state->engine.prepare(rate,block);state->engine.start(from,to,step,reverse,pitch);state->engine.prime(*source);
        track=false;
        check(allocationBytes>0&&allocationBytes<=KeepLengthRenderCache::preparedStateByteBound(rate,block),"reserved memory bounds actual preparation allocations");
        KeepLengthEngine reference,prepared;
        for(auto* e:{&reference,&prepared}){e->prepare(rate,block);e->start(from,to,step,reverse,pitch);}
        allocationCount=deletionCount=0;track=true;prepared.usePreparedState(state);track=false;
        check(allocationCount==0&&deletionCount==0,"adopting a prepared state allocates and deletes nothing");
        bool equal=true,finite=true,noHeap=true;int frames=0;
        const int limit=reference.getOutputLength();
        while(frames<limit){const int request=juce::jmin(block,limit-frames);const float target=frames>limit/2 ? pitch+2.37f : pitch;
            const int a=reference.process(*source,target,request);
            allocationCount=deletionCount=0;track=true;const int b=prepared.process(*source,target,request);track=false;
            noHeap &= allocationCount==0&&deletionCount==0;
            equal &= a==b&&reference.getOutputPosition()==prepared.getOutputPosition()&&reference.getSourcePosition()==prepared.getSourcePosition();
            for(int i=0;i<a;++i){finite &= std::isfinite(prepared.getLeft()[i])&&std::isfinite(prepared.getRight()[i]);
                equal &= reference.getLeft()[i]==prepared.getLeft()[i]&&reference.getRight()[i]==prepared.getRight()[i]
                    &&reference.getDryLeft()[i]==prepared.getDryLeft()[i]&&reference.getDryRight()[i]==prepared.getDryRight()[i];}
            frames+=a;if(a<=0)break;
        }
        check(equal&&finite,"prepared output, dry, timing, reverse, resampling, tail and mid-hit Pitch match original exactly");
        check(noHeap,"prepared realtime processing allocates and deletes nothing");
        check(frames==limit,"prepared voice produces every requested frame without added delay");
        ++cases;
    }
    // Reject a consumed/mismatched state rather than replaying the wrong phase.
    KeepLengthEngine empty;empty.prepare(48000,64);empty.start(0,500,1,false,7);
    auto mismatch=std::make_shared<KeepLengthPreparedState>();mismatch->sampleRate=96000;
    empty.usePreparedState(mismatch);
    auto source=juce::AudioBuffer<float>(1,500);source.clear();source.setSample(0,100,.5f);
    KeepLengthEngine fallback;fallback.prepare(48000,64);fallback.start(0,500,1,false,7);
    const int a=empty.process(source,7,64),b=fallback.process(source,7,64);
    bool rejected=a==b;for(int i=0;i<a;++i)rejected &= empty.getLeft()[i]==fallback.getLeft()[i];
    check(rejected,"mismatched prepared state leaves the original live path intact");
    std::cout<<"Prepared state cases: "<<cases<<", failures: "<<failures<<'\n';
    return failures?1:0;
}
