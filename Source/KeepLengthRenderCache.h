#pragma once
#include "KeepLengthEngine.h"
#include "PadData.h"
#include "KitData.h"
#include <functional>

// One bounded worker per processor. A miss always retains the live engine;
// preparation never delays a note or changes pitch, timing or sample length.
class KeepLengthRenderCache : private juce::Thread
{
public:
    static constexpr int slotCount = NUM_PADS * MAX_LAYERS_PER_PAD * MAX_SAMPLE_STOCK_PER_LAYER;
    static constexpr std::uint64_t byteLimit = 128ull * 1024 * 1024;
    static constexpr double fullRenderSeconds = 8.0;
    static std::uint64_t preparedStateByteBound(double rate, int blockSize) noexcept
    { return 128ull * 1024 + (std::uint64_t) std::ceil(rate) * 24
            + (std::uint64_t) juce::jmax(1, blockSize) * 24; }
    struct Request
    {
        int slot {};
        std::shared_ptr<const juce::AudioBuffer<float>> source;
        double rate {}, start {}, end {}, step {};
        float pitch {};
        bool reverse {};
        int blockSize {};
        bool matches(const KeepLengthRender& r) const noexcept
        { return r.matches(source.get(), rate, start, end, step, reverse, pitch); }
        bool matches(const KeepLengthPreparedState& r) const noexcept
        { return matches(static_cast<const KeepLengthRender&>(r)) && blockSize == r.preparedBlockSize; }
    };
    using Capture = std::function<bool(std::vector<Request>&)>;
    explicit KeepLengthRenderCache(Capture capture);
    ~KeepLengthRenderCache() override;
    void prepare(); // Host preparation, outside the realtime callback.
    void stop();
    std::shared_ptr<const KeepLengthRender> find(int pad, int layer, int variation,
        const juce::AudioBuffer<float>* source, double rate, double start,
        double end, double step, bool reverse, float pitch) const noexcept;
    std::shared_ptr<KeepLengthPreparedState> takePreparedState(int pad, int layer, int variation,
        const juce::AudioBuffer<float>* source, double rate, double start,
        double end, double step, bool reverse, float pitch) noexcept;
    static int index(int pad, int layer, int variation) noexcept
    { return (pad * MAX_LAYERS_PER_PAD + layer) * MAX_SAMPLE_STOCK_PER_LAYER + variation; }

private:
    void run() override;
    void refresh();
    Capture capture;
    mutable juce::SpinLock publicationLock;
    std::array<std::shared_ptr<const KeepLengthRender>, slotCount> entries;
    std::array<std::shared_ptr<KeepLengthPreparedState>, slotCount> preparedStates;
    // The worker retains ownership before publication and until a voice is
    // finished, so consuming/releasing a state never deletes an FFT on audio.
    std::vector<std::shared_ptr<KeepLengthPreparedState>> stateOwners;
    // Keep retired outputs alive until the last voice releases them. Large
    // buffers are consequently freed on this worker, never by a note-on/reset.
    std::vector<std::shared_ptr<const KeepLengthRender>> retired;
    std::vector<Request> requests;
    std::vector<Request> latest;
    bool synchronous { false };
    mutable std::atomic<int> recentPad { 0 };
};
