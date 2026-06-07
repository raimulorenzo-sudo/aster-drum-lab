#pragma once
#include <JuceHeader.h>
#include "KitData.h"

namespace PadParameterSpecs
{
    enum class Param
    {
        Volume = 0,
        Pan,
        Pitch,
        Attack,
        Release,
        Start,
        End,
        FadeIn,
        FadeOut,
        Reverse,
        Mute,
        Solo,
        Humanize,   // v7+ DAW automation
        Velocity,   // v7+ DAW automation (velocitySens)
        PadVolume,  // Pad total volume, after all layer volumes
        PadPan,
        PadPitch,
    };

    struct Spec
    {
        Param        param;
        const char*  idSuffix;
        const char*  displayName;
        float        defaultValue;
        float        minValue;
        float        maxValue;
        bool         isBoolean;
    };

    constexpr int numAutomatableParams = 17;

    const std::array<Spec, numAutomatableParams>& all() noexcept;

    juce::String parameterID(int padIndex, Param param);
    juce::String parameterName(int padIndex, const PadData& pad, Param param);
    const Spec&  specFor(Param param) noexcept;
}
