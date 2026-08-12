#include "DemoMode.h"

#include <atomic>
#include <chrono>
#include <cmath>

namespace
{
    using Clock = std::chrono::steady_clock;
    std::atomic<int64_t> demoStartNanoseconds { 0 };

    int64_t nowNanoseconds() noexcept
    {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            Clock::now().time_since_epoch()).count();
    }
}

namespace AsterDemoMode
{
    void beginOnFirstSound() noexcept
    {
        if constexpr (! isDemoBuild)
            return;

        int64_t expected = 0;
        const auto now = nowNanoseconds();
        demoStartNanoseconds.compare_exchange_strong(expected, now,
                                                     std::memory_order_acq_rel,
                                                     std::memory_order_acquire);
    }

    bool hasStarted() noexcept
    {
        if constexpr (! isDemoBuild)
            return false;
        return demoStartNanoseconds.load(std::memory_order_acquire) != 0;
    }

    double getRemainingSeconds() noexcept
    {
        if constexpr (! isDemoBuild)
            return static_cast<double>(durationSeconds);

        const auto startedAt = demoStartNanoseconds.load(std::memory_order_acquire);
        if (startedAt == 0)
            return static_cast<double>(durationSeconds);

        const auto elapsed = static_cast<double>(nowNanoseconds() - startedAt) / 1.0e9;
        return juce::jmax(0.0, static_cast<double>(durationSeconds) - elapsed);
    }

    bool hasExpired() noexcept
    {
        return isDemoBuild && hasStarted() && getRemainingSeconds() <= 0.0;
    }

    juce::var getStateAsVar(bool offlineRenderBlocked)
    {
        auto* state = new juce::DynamicObject();
        state->setProperty("isDemo", isDemoBuild);
        state->setProperty("durationSeconds", durationSeconds);
        state->setProperty("started", hasStarted());
        state->setProperty("remainingSeconds",
                           static_cast<int>(std::ceil(getRemainingSeconds())));
        state->setProperty("expired", hasExpired());
        state->setProperty("offlineRenderBlocked", isDemoBuild && offlineRenderBlocked);
        state->setProperty("kitSavingEnabled", ! isDemoBuild);
        return juce::var(state);
    }
}
