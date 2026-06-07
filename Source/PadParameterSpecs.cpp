#include "PadParameterSpecs.h"

namespace
{
    static const std::array<PadParameterSpecs::Spec, PadParameterSpecs::numAutomatableParams> specs {{
        // Volume is a FADER POSITION 0..1 (see FaderCurve.h). 0.75 == 0 dB unity.
        { PadParameterSpecs::Param::Volume,  "volume",  "Volume",   0.75f,  0.0f, 1.0f,  false },
        { PadParameterSpecs::Param::Pan,     "pan",     "Pan",      0.0f,  -1.0f, 1.0f,  false },
        { PadParameterSpecs::Param::Pitch,   "pitch",   "Pitch",    0.0f, -24.0f, 24.0f, false },
        { PadParameterSpecs::Param::Attack,  "attack",  "Attack",   0.002f, 0.0f, 2.0f,  false },
        { PadParameterSpecs::Param::Release, "release", "Release",  0.05f,  0.0f, 4.0f,  false },
        { PadParameterSpecs::Param::Start,   "start",   "Start",    0.0f,   0.0f, 1.0f,  false },
        { PadParameterSpecs::Param::End,     "end",     "End",      1.0f,   0.0f, 1.0f,  false },
        { PadParameterSpecs::Param::FadeIn,  "fadeIn",  "Fade In",  0.0f,   0.0f, 1.0f,  false },
        { PadParameterSpecs::Param::FadeOut, "fadeOut", "Fade Out", 0.0f,   0.0f, 1.0f,  false },
        { PadParameterSpecs::Param::Reverse, "reverse", "Reverse",  0.0f,   0.0f, 1.0f,  true  },
        { PadParameterSpecs::Param::Mute,    "mute",    "Mute",     0.0f,   0.0f, 1.0f,  true  },
        { PadParameterSpecs::Param::Solo,    "solo",    "Solo",     0.0f,   0.0f, 1.0f,  true  },
        // v7+: Humanize / Velocity (UI 上は DYNAMICS タブ)
        { PadParameterSpecs::Param::Humanize, "humanize", "Humanize", 0.0f, 0.0f, 1.0f, false },
        { PadParameterSpecs::Param::Velocity, "velocity", "Velocity", 1.0f, 0.0f, 1.0f, false },
        // createParameterLayout appends these after the complete legacy layout.
        { PadParameterSpecs::Param::PadVolume, "padVolume", "Pad Volume", 0.75f, 0.0f, 1.0f, false },
    }};

    juce::String pagePrefix(int padIndex)
    {
        return KitData::pageLabel(KitData::pageForPad(padIndex))
             + juce::String(padIndex + 1).paddedLeft('0', 2);
    }
}

namespace PadParameterSpecs
{
    const std::array<Spec, numAutomatableParams>& all() noexcept
    {
        return specs;
    }

    const Spec& specFor(Param param) noexcept
    {
        return specs[static_cast<size_t>(param)];
    }

    juce::String parameterID(int padIndex, Param param)
    {
        return "pad" + juce::String(padIndex + 1).paddedLeft('0', 2)
             + "." + specFor(param).idSuffix;
    }

    juce::String parameterName(int padIndex, const PadData& pad, Param param)
    {
        return pagePrefix(padIndex) + " " + pad.padName + " " + specFor(param).displayName;
    }
}
