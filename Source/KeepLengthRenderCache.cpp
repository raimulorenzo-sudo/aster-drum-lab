#include "KeepLengthRenderCache.h"
#include <algorithm>

namespace {
std::uint64_t bytes(const KeepLengthRender& render)
{ return (std::uint64_t) render.audio.getNumSamples() * 2 * sizeof(float); }
}

KeepLengthRenderCache::KeepLengthRenderCache(Capture fn)
    : juce::Thread("ASTER pitch preparation"), capture(std::move(fn))
{
    requests.reserve(slotCount);
    latest.reserve(slotCount);
    retired.reserve(slotCount + 64);
    stateOwners.reserve(slotCount + 64);
}
KeepLengthRenderCache::~KeepLengthRenderCache() { stop(); }
void KeepLengthRenderCache::stop() { signalThreadShouldExit(); notify(); stopThread(-1); }
void KeepLengthRenderCache::prepare()
{
    stop();
    synchronous = true;
    refresh();
    synchronous = false;
    startThread(juce::Thread::Priority::low);
}
std::shared_ptr<const KeepLengthRender> KeepLengthRenderCache::find(
    int pad, int layer, int variation, const juce::AudioBuffer<float>* source,
    double rate, double start, double end, double step, bool reverse, float pitch) const noexcept
{
    if (pad < 0 || pad >= NUM_PADS || layer < 0 || layer >= MAX_LAYERS_PER_PAD
        || variation < 0 || variation >= MAX_SAMPLE_STOCK_PER_LAYER) return {};
    recentPad.store(pad, std::memory_order_relaxed);
    const auto slot = index(pad, layer, variation);
    if (slot < 0 || slot >= slotCount) return {};
    const juce::SpinLock::ScopedTryLockType guard(publicationLock);
    if (!guard.isLocked()) return {};
    const auto& entry = entries[(size_t) slot];
    if (entry && entry->matches(source, rate, start, end, step, reverse, pitch)) return entry;
    return {};
}
std::shared_ptr<KeepLengthPreparedState> KeepLengthRenderCache::takePreparedState(
    int pad, int layer, int variation, const juce::AudioBuffer<float>* source,
    double rate, double start, double end, double step, bool reverse, float pitch) noexcept
{
    if (pad < 0 || pad >= NUM_PADS || layer < 0 || layer >= MAX_LAYERS_PER_PAD
        || variation < 0 || variation >= MAX_SAMPLE_STOCK_PER_LAYER) return {};
    recentPad.store(pad, std::memory_order_relaxed);
    std::shared_ptr<KeepLengthPreparedState> result;
    {
        const juce::SpinLock::ScopedTryLockType guard(publicationLock);
        if (!guard.isLocked()) return {};
        auto& entry = preparedStates[(size_t) index(pad, layer, variation)];
        if (entry && entry->matches(source, rate, start, end, step, reverse, pitch))
            result = std::move(entry); // Mutable DSP belongs to exactly one voice.
    }
    if (result) notify();
    return result;
}
void KeepLengthRenderCache::run()
{
    while (!threadShouldExit()) { refresh(); wait(20); }
}
void KeepLengthRenderCache::refresh()
{
    requests.clear();
    if (!capture(requests)) return;
    const auto refreshStarted = juce::Time::getMillisecondCounterHiRes();
    const int priorityPad = recentPad.load(std::memory_order_relaxed);
    std::stable_sort(requests.begin(), requests.end(), [priorityPad](const auto& a, const auto& b) {
        const int stride = MAX_LAYERS_PER_PAD * MAX_SAMPLE_STOCK_PER_LAYER;
        return (a.slot / stride == priorityPad) > (b.slot / stride == priorityPad);
    });
    std::array<const Request*, slotCount> wanted {}, stateWanted {};
    std::uint64_t stateReserve = 0;
    const auto stateBytes = [](const Request& r) {
        // Conservative bound for both configured FFT engines, their scratch
        // and bookkeeping. The source is shared with AudioFileManager.
        return preparedStateByteBound(r.rate, r.blockSize);
    };
    for (const auto& request : requests)
    {
        const double length = std::ceil((request.end - request.start) / request.step);
        if (!std::isfinite(length) || !std::isfinite(request.rate) || request.rate < 8000
            || request.rate > 384000 || length > std::numeric_limits<int>::max()
            || !request.source || !std::isfinite(request.pitch)
            || length <= request.rate * fullRenderSeconds) continue;
        stateWanted[(size_t) request.slot] = &request;
        stateReserve = juce::jmin(byteLimit / 2, stateReserve + stateBytes(request));
    }
    const auto renderBudget = byteLimit - stateReserve;
    std::uint64_t planned = 0;
    for (const auto& request : requests)
    {
        const double length = std::ceil((request.end - request.start) / request.step);
        if (!std::isfinite(length) || length <= 0 || length > request.rate * fullRenderSeconds) continue;
        const auto size = (std::uint64_t) length * 2 * sizeof(float);
        if (size > renderBudget - planned) continue;
        planned += size;
        wanted[(size_t) request.slot] = &request;
    }
    for (int i = 0; i < slotCount; ++i)
    {
        auto& entry = entries[(size_t) i];
        const auto* request = wanted[(size_t) i];
        if (entry && (!request || !request->matches(*entry)))
        {
            // Secure the non-realtime owner before unpublishing.
            retired.push_back(entry);
            const juce::SpinLock::ScopedLockType guard(publicationLock);
            entry.reset();
        }
    }
    retired.erase(std::remove_if(retired.begin(), retired.end(),
        [](const auto& r) { return r.use_count() == 1; }), retired.end());
    {
        const juce::SpinLock::ScopedLockType guard(publicationLock);
        for (int i = 0; i < slotCount; ++i)
            if (preparedStates[(size_t) i] && (!stateWanted[(size_t) i]
                || !stateWanted[(size_t) i]->matches(*preparedStates[(size_t) i])))
                preparedStates[(size_t) i].reset();
    }
    stateOwners.erase(std::remove_if(stateOwners.begin(), stateOwners.end(),
        [](const auto& state) { return state.use_count() == 1; }), stateOwners.end());
    std::uint64_t used = 0;
    for (const auto& entry : entries) if (entry) used += bytes(*entry);
    for (const auto& entry : retired) used += bytes(*entry);
    for (const auto& state : stateOwners) used += state->reservedBytes;
    for (const auto& request : requests)
    {
        if (!synchronous && threadShouldExit()) return;
        if (synchronous && juce::Time::getMillisecondCounterHiRes() - refreshStarted > 100.0) return;
        if (!stateWanted[(size_t) request.slot]) continue;
        {
            const juce::SpinLock::ScopedLockType guard(publicationLock);
            if (preparedStates[(size_t) request.slot]) continue;
        }
        const auto reserved = stateBytes(request);
        if (reserved > byteLimit - juce::jmin(used, byteLimit)) continue;
        auto state = std::make_shared<KeepLengthPreparedState>();
        state->source = request.source;
        state->sampleRate = request.rate; state->start = request.start;
        state->end = request.end; state->step = request.step;
        state->pitch = request.pitch; state->reverse = request.reverse;
        state->reservedBytes = reserved;
        state->preparedBlockSize = request.blockSize;
        state->engine.prepare(request.rate, request.blockSize);
        state->engine.start(request.start, request.end, request.step, request.reverse, request.pitch);
        state->engine.prime(*request.source);
        latest.clear();
        if (!capture(latest)) continue;
        const auto current = std::find_if(latest.begin(), latest.end(), [&](const auto& r) {
            return r.slot == request.slot && r.matches(*state);
        });
        if (current == latest.end()) continue;
        used += reserved;
        stateOwners.push_back(state);
        const juce::SpinLock::ScopedLockType guard(publicationLock);
        preparedStates[(size_t) request.slot] = std::move(state);
    }
    for (const auto& request : requests)
    {
        if (!synchronous && threadShouldExit()) return;
        if (synchronous && juce::Time::getMillisecondCounterHiRes() - refreshStarted > 100.0) return;
        if (!wanted[(size_t) request.slot]) continue;
        if (entries[(size_t) request.slot]) continue;
        const double length = std::ceil((request.end - request.start) / request.step);
        // A pathological long file must not monopolise CPU or cache memory.
        // It keeps the original streaming path instead.
        if (!std::isfinite(length) || length <= 0 || length > request.rate * fullRenderSeconds
            || length * 8 > byteLimit - juce::jmin(used, byteLimit)) continue;
        auto result = std::make_shared<KeepLengthRender>();
        result->source = request.source;
        result->sampleRate = request.rate; result->start = request.start;
        result->end = request.end; result->step = request.step;
        result->pitch = request.pitch; result->reverse = request.reverse;
        result->audio.setSize(2, (int) length);
        result->audio.clear();
        KeepLengthEngine engine;
        engine.prepare(request.rate, request.blockSize);
        engine.start(request.start, request.end, request.step, request.reverse, request.pitch);
        for (int position = 0; position < (int) length;)
        {
            if (!synchronous && threadShouldExit()) return;
            const int count = engine.process(*request.source, request.pitch,
                juce::jmin(request.blockSize, (int) length - position));
            if (count <= 0) break;
            result->audio.copyFrom(0, position, engine.getLeft(), count);
            result->audio.copyFrom(1, position, engine.getRight(), count);
            position += count;
        }
        latest.clear();
        if (!capture(latest)) continue;
        const auto current = std::find_if(latest.begin(), latest.end(), [&](const auto& r) {
            return r.slot == request.slot && r.matches(*result);
        });
        if (current == latest.end()) continue;
        used += bytes(*result);
        const juce::SpinLock::ScopedLockType guard(publicationLock);
        entries[(size_t) request.slot] = std::move(result);
    }
}
