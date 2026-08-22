#pragma once

#include <JuceHeader.h>

#ifndef ASTER_DEMO_BUILD
 #define ASTER_DEMO_BUILD 0
#endif

#ifndef ASTER_DEMO_DURATION_SECONDS
 #define ASTER_DEMO_DURATION_SECONDS 1200
#endif

namespace AsterDemoMode
{
    inline constexpr bool isDemoBuild = ASTER_DEMO_BUILD != 0;
    inline constexpr int durationSeconds = ASTER_DEMO_DURATION_SECONDS;
    inline constexpr double unrestrictedSeconds = 5.0 * 60.0;
    inline constexpr double muteIntervalSeconds = 60.0;
    inline constexpr double muteDurationSeconds = 2.0;
    inline constexpr double muteFadeSeconds = 0.1;

    void beginOnFirstSound() noexcept;
    bool hasStarted() noexcept;
    double getElapsedSeconds() noexcept;
    double getRemainingSeconds() noexcept;
    bool hasExpired() noexcept;
    float getScheduledOutputGain(double elapsedSeconds) noexcept;
    bool isScheduledMuteActive(double elapsedSeconds) noexcept;
    int getSecondsUntilNextMute(double elapsedSeconds) noexcept;
    juce::var getStateAsVar(bool offlineRenderBlocked = false);
}
