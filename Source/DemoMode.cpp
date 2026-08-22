#include "DemoMode.h"

#include <atomic>
#include <chrono>

namespace
{
    using Clock = std::chrono::steady_clock;
    std::atomic<int64_t> demoStartNanoseconds { 0 };

    const juce::String& processSessionIdentifier()
    {
        static const juce::String identifier = juce::Uuid().toString();
        return identifier;
    }

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

    double getElapsedSeconds() noexcept
    {
        if constexpr (! isDemoBuild)
            return 0.0;

        const auto startedAt = demoStartNanoseconds.load(std::memory_order_acquire);
        if (startedAt == 0)
            return 0.0;

        return juce::jmax(0.0,
            static_cast<double>(nowNanoseconds() - startedAt) / 1.0e9);
    }

    double getRemainingSeconds() noexcept
    {
        if constexpr (! isDemoBuild)
            return static_cast<double>(durationSeconds);

        return juce::jmax(0.0,
            static_cast<double>(durationSeconds) - getElapsedSeconds());
    }

    bool hasExpired() noexcept
    {
        return isDemoBuild && hasStarted() && getRemainingSeconds() <= 0.0;
    }

    float getOutputGain(double elapsedSeconds) noexcept
    {
        if constexpr (! isDemoBuild)
            return 1.0f;

        if (elapsedSeconds >= static_cast<double>(durationSeconds))
            return 0.0f;

        // Fade only at the final expiry boundary to avoid a discontinuity.
        // There are no periodic Demo mutes during the 20-minute session.
        const auto remaining = static_cast<double>(durationSeconds) - elapsedSeconds;
        if (remaining < expiryFadeSeconds)
            return static_cast<float>(juce::jlimit(0.0, 1.0,
                                                   remaining / expiryFadeSeconds));

        return 1.0f;
    }

    const juce::String& getProcessSessionIdentifier()
    {
        return processSessionIdentifier();
    }

    bool stateBelongsToCurrentProcess(const juce::String& savedSessionIdentifier)
    {
        return ! isDemoBuild
            || (savedSessionIdentifier.isNotEmpty()
                && savedSessionIdentifier == processSessionIdentifier());
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
        state->setProperty("contentResetsAfterRestart", isDemoBuild);
        return juce::var(state);
    }
}
