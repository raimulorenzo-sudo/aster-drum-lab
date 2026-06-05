#pragma once
#include <cmath>
#include <algorithm>
#include <limits>

/*  Unified fader curve — must stay byte-identical with
    ui-prototype/src/utils/fader.ts (FADER_UNITY_POS, FADER_DB_TOP,
    FADER_DB_FLOOR, FADER_CURVE_EXP).

    pad.volume (and the master output knob) are stored as a FADER
    POSITION in [0..1] — NOT as a linear gain. The single source of truth
    for converting a position to a multiplicative gain is
    `FaderCurve::positionToGain()`. Apply it once at the point where the
    gain is multiplied into the signal (see VoiceManager::trigger).

    Curve:
        pos = 0                     → -Infinity dB  (gain = 0, hard mute)
        pos → 0⁺                    → -60 dB        (approached asymptotically)
        pos = kUnityPos  (0.75)     →   0 dB        (unity gain, linear gain 1.0)
        pos = 1                     →  +12 dB

    Below unity:  db = -(kDbUnity - kDbFloor) * (1 - pos/kUnityPos)^kCurveExp
    Above unity:  db linear in position over (0 .. +12 dB)
*/
namespace FaderCurve
{
    inline constexpr float kUnityPos = 0.75f;
    inline constexpr float kDbTop    =  12.0f;
    inline constexpr float kDbUnity  =   0.0f;
    inline constexpr float kDbFloor  = -60.0f;
    inline constexpr float kCurveExp =   2.5f;

    inline constexpr float kDbRangeBelow = kDbUnity - kDbFloor; // 60
    inline constexpr float kDbRangeAbove = kDbTop   - kDbUnity; // 12

    /** Fader position [0..1] → dB. Returns -infinity for pos ≤ 0. */
    inline float positionToDb (float pos) noexcept
    {
        if (pos <= 0.0f) return -std::numeric_limits<float>::infinity();
        const float p = std::min (pos, 1.0f);
        if (p >= kUnityPos)
        {
            const float t = (p - kUnityPos) / (1.0f - kUnityPos);
            return kDbUnity + t * kDbRangeAbove;
        }
        const float t = p / kUnityPos;
        return kDbUnity - kDbRangeBelow * std::pow (1.0f - t, kCurveExp);
    }

    /** dB → fader position [0..1]. -inf / very low → 0. */
    inline float dbToPosition (float db) noexcept
    {
        if (! std::isfinite (db) || db <= kDbFloor) return 0.0f;
        if (db >= kDbTop)   return 1.0f;
        if (db >= kDbUnity)
            return kUnityPos + (db - kDbUnity) / kDbRangeAbove * (1.0f - kUnityPos);
        // Inverse: (1 - t)^exp = -db / kDbRangeBelow
        const float ratio = -db / kDbRangeBelow;
        const float oneMinusT = std::pow (ratio, 1.0f / kCurveExp);
        return std::max (0.0f, (1.0f - oneMinusT) * kUnityPos);
    }

    /** Fader position [0..1] → linear gain (amplitude). 0 → 0 (silence). */
    inline float positionToGain (float pos) noexcept
    {
        if (pos <= 0.0f) return 0.0f;
        return std::pow (10.0f, positionToDb (pos) / 20.0f);
    }

    /** Linear gain → fader position. Used by the legacy-state
     *  migrator in setStateInformation. */
    inline float gainToPosition (float gain) noexcept
    {
        if (gain <= 0.0f) return 0.0f;
        const float topGain = std::pow (10.0f, kDbTop / 20.0f);
        if (gain >= topGain) return 1.0f;
        return dbToPosition (20.0f * std::log10 (gain));
    }
}
