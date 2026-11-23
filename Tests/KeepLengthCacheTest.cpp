#include "../Source/KeepLengthRenderCache.h"
#include <chrono>
#include <iostream>
#include <mutex>

int main()
{
    int failures = 0;
    auto check = [&](bool value, const char* message) {
        if (!value) { ++failures; std::cerr << "FAIL " << message << '\n'; }
    };
    for (double rate : {48000.0, 96000.0}) for (int block : {64, 256})
    {
        std::mutex mutex;
        std::vector<KeepLengthRenderCache::Request> requests;
        for (int i = 0; i < 8; ++i)
        {
            auto source = std::make_shared<juce::AudioBuffer<float>>(i % 2 + 1, (int) (rate * (i < 2 ? .018 : .4)));
            for (int c = 0; c < source->getNumChannels(); ++c)
                for (int n = 0; n < source->getNumSamples(); ++n)
                    source->setSample(c, n, .2f * std::sin((float) (2 * juce::MathConstants<double>::pi * (137 + 237*c) * n / rate)));
            const double from = i % 3 ? source->getNumSamples() * .1 : 0;
            const double to = source->getNumSamples() * .93;
            requests.push_back({i, source, rate, from, to, i % 3 ? 44100.0 / rate : 1.0,
                                i % 2 ? 7.13f : -12.01f, (i % 2) != 0, block});
        }
        KeepLengthRenderCache cache([&](auto& output) {
            std::lock_guard<std::mutex> guard(mutex); output = requests; return true;
        });
        cache.prepare();
        std::array<std::shared_ptr<const KeepLengthRender>, 8> rendered;
        auto find = [&](int i) {
            const auto& r = requests[(size_t) i];
            return cache.find(0, i / MAX_SAMPLE_STOCK_PER_LAYER, i % MAX_SAMPLE_STOCK_PER_LAYER,
                r.source.get(), r.rate, r.start, r.end, r.step, r.reverse, r.pitch);
        };
        for (int attempt = 0; attempt < 500; ++attempt)
        {
            bool ready = true;
            for (int i = 0; i < 8; ++i) { rendered[(size_t) i] = find(i); ready &= rendered[(size_t) i] != nullptr; }
            if (ready) break;
            juce::Thread::sleep(10);
        }
        bool ready = true; for (auto& r : rendered) ready &= r != nullptr;
        check(ready, "all eight outputs prepared"); if (!ready) continue;
        std::array<KeepLengthEngine, 8> live, cached;
        for (int i = 0; i < 8; ++i)
        {
            auto& r = requests[(size_t) i];
            for (auto* engine : {&live[(size_t) i], &cached[(size_t) i]}) {
                engine->prepare(rate, block); engine->start(r.start, r.end, r.step, r.reverse, r.pitch);
            }
            cached[(size_t) i].usePreparedRender(rendered[(size_t) i]);
        }
        bool equal = true, finite = true; double firstLive = 0, firstCached = 0;
        for (int n = 0; n < 10000; ++n)
        {
            bool active = false;
            auto started = std::chrono::steady_clock::now();
            std::array<int,8> counts {};
            for (int i = 0; i < 8; ++i) counts[(size_t) i] = live[(size_t) i].process(*requests[(size_t) i].source, requests[(size_t) i].pitch, block);
            if (!n) firstLive = std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now() - started).count();
            started = std::chrono::steady_clock::now();
            for (int i = 0; i < 8; ++i)
            {
                auto& r = requests[(size_t) i];
                const auto count = cached[(size_t) i].process(*r.source, r.pitch, block);
                active |= count > 0; equal &= count == counts[(size_t) i];
            }
            if (!n) firstCached = std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now() - started).count();
            for (int i = 0; i < 8; ++i) for (int s = 0; s < counts[(size_t) i]; ++s)
            {
                const auto a = cached[(size_t) i].getLeft()[s], b = cached[(size_t) i].getRight()[s];
                finite &= std::isfinite(a) && std::isfinite(b);
                equal &= std::abs(a - live[(size_t) i].getLeft()[s]) < 1e-7f
                      && std::abs(b - live[(size_t) i].getRight()[s]) < 1e-7f;
            }
            if (!active) break;
        }
        check(equal && finite, "cached stereo output, trim, reverse, fine pitch and resampling match live output");
        for (const auto& engine : cached)
            check(engine.getOutputPosition() == engine.getOutputLength(), "cached render reaches the complete fixed-length tail");
        std::cout << "8 layers rate=" << rate << " block=" << block << " first live=" << firstLive
                  << "ms cached=" << firstCached << "ms deadline=" << block / rate * 1000 << "ms\n";
        // Never use stale data after rate, pitch, reverse, trim or source changes.
        const auto& r = requests[0];
        check(!cache.find(0,0,0,r.source.get(),rate,r.start,r.end,r.step,!r.reverse,r.pitch)
            && !cache.find(0,0,0,r.source.get(),rate,r.start+1,r.end,r.step,r.reverse,r.pitch)
            && !cache.find(0,0,0,r.source.get(),rate,r.start,r.end,r.step,r.reverse,r.pitch+1), "stale cache key rejected");
        KeepLengthEngine automated; automated.prepare(rate, block);
        automated.start(r.start, r.end, r.step, r.reverse, r.pitch); automated.usePreparedRender(rendered[0]);
        automated.process(*r.source,r.pitch,block); automated.process(*r.source,r.pitch+3,block);
        check(automated.getOutputPosition() == block*2, "automation retains current timeline");
        for (int s=0;s<block;++s) check(std::isfinite(automated.getLeft()[s]), "automation fallback finite");
        { std::lock_guard<std::mutex> guard(mutex); requests.clear(); }
        cache.stop();
    }
    // Long sources keep only a ready-to-run initial DSP state. Two hits must
    // never share its mutable FFT/envelope position.
    for (double rate : {48000.,96000.}) for (int block : {64,256})
    {
        std::mutex mutex;
        std::vector<KeepLengthRenderCache::Request> requests;
        for (int i=0;i<8;++i) {
            auto source=std::make_shared<juce::AudioBuffer<float>>(2,(int)(rate*9));
            for(int c=0;c<2;++c)for(int n=0;n<source->getNumSamples();++n)
                source->setSample(c,n,.15f*std::sin((float)(n*2*juce::MathConstants<double>::pi*(83+137*c+i*3)/rate)));
            requests.push_back({i,source,rate,11.25,source->getNumSamples()-.75,1.,7.13f+i*.017f,i%2!=0,block});
        }
        KeepLengthRenderCache cache([&](auto& output){std::lock_guard<std::mutex> guard(mutex);output=requests;return true;});
        cache.prepare();
        std::array<std::shared_ptr<KeepLengthPreparedState>,8> states;
        auto take=[&](int i,float offset=0.f){const auto& r=requests[(size_t)i];return cache.takePreparedState(0,i/5,i%5,
            r.source.get(),r.rate,r.start,r.end,r.step,r.reverse,r.pitch+offset);};
        check(!take(0,1.f),"long prepared cache rejects stale Pitch without consuming its ready state");
        for(int attempt=0;attempt<500;++attempt){bool ready=true;for(int i=0;i<8;++i){if(!states[(size_t)i])states[(size_t)i]=take(i);ready &= states[(size_t)i]!=nullptr;}if(ready)break;juce::Thread::sleep(2);}
        cache.stop();
        bool ready=true;for(const auto& state:states)ready &= state!=nullptr;
        check(ready,"all eight long-source states prepared");if(!ready)continue;
        auto second=take(0);
        check(!second||second!=states[0],"replenished long hits own separate mutable processing states");
        check(!take(0),"a prepared state is consumed once while the worker is stopped");
        std::array<KeepLengthEngine,8> live,cached;
        for(int i=0;i<8;++i){const auto& r=requests[(size_t)i];for(auto* e:{&live[(size_t)i],&cached[(size_t)i]}){e->prepare(rate,block);e->start(r.start,r.end,r.step,r.reverse,r.pitch);}cached[(size_t)i].usePreparedState(std::move(states[(size_t)i]));}
        bool equal=true;double firstLive=0,firstCached=0;
        for(int n=0;n<100;++n){auto start=std::chrono::steady_clock::now();for(int i=0;i<8;++i)live[(size_t)i].process(*requests[(size_t)i].source,requests[(size_t)i].pitch+(n>50?2.3f:0),block);if(!n)firstLive=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            start=std::chrono::steady_clock::now();for(int i=0;i<8;++i)cached[(size_t)i].process(*requests[(size_t)i].source,requests[(size_t)i].pitch+(n>50?2.3f:0),block);if(!n)firstCached=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            for(int i=0;i<8;++i)for(int j=0;j<block;++j)equal &= live[(size_t)i].getLeft()[j]==cached[(size_t)i].getLeft()[j]&&live[(size_t)i].getRight()[j]==cached[(size_t)i].getRight()[j]&&std::isfinite(cached[(size_t)i].getLeft()[j]);
        }
        check(equal,"long prepared stereo output and mid-hit Pitch automation match live audio exactly");
        check(cached[0].getOutputPosition()==100*block,"long prepared voice has no inserted leading frames");
        {std::lock_guard<std::mutex> guard(mutex);for(auto& r:requests)r.blockSize=8192;}
        cache.prepare();
        auto resized=take(0);
        check(resized&&resized->preparedBlockSize==8192&&resized->engine.getScratchCapacity()>=8192,"buffer size changes replace ready FFT states before the next hit");
        live[0].process(*requests[0].source,requests[0].pitch+2.3f,block);
        cached[0].process(*requests[0].source,requests[0].pitch+2.3f,block);
        bool retained=true;for(int j=0;j<block;++j)retained &= live[0].getLeft()[j]==cached[0].getLeft()[j];
        check(retained&&cached[0].getOutputPosition()==101*block,"restocking while a hit is active cannot reset or share that hit's DSP state");
        cache.stop();
        std::cout<<"long 8 layers rate="<<rate<<" block="<<block<<" live="<<firstLive<<"ms prepared="<<firstCached<<"ms deadline="<<block/rate*1000<<"ms\n";
    }
    std::cout << "Keep Length cache failures: " << failures << '\n';
    return failures ? 1 : 0;
}
