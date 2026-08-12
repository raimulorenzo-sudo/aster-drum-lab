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

    void beginOnFirstSound() noexcept;
    bool hasStarted() noexcept;
    double getRemainingSeconds() noexcept;
    bool hasExpired() noexcept;
    juce::var getStateAsVar(bool offlineRenderBlocked = false);
}
