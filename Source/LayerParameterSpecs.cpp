#include "LayerParameterSpecs.h"

namespace
{
    static const std::array<LayerParameterSpecs::Spec, LayerParameterSpecs::numAutomatableParams> specs {{
        // Volume は fader position [0..1] (FaderCurve 経由)。0.75 = 0 dB unity.
        { LayerParameterSpecs::Param::Volume, "volume", "Volume", 0.75f,  0.0f,   1.0f,  false },
        { LayerParameterSpecs::Param::Pan,    "pan",    "Pan",    0.0f,  -1.0f,   1.0f,  false },
        { LayerParameterSpecs::Param::Pitch,  "pitch",  "Pitch",  0.0f,  -24.0f, 24.0f,  false },
        // Vel Min/Max は 0..127 を 0..1 に正規化して保持 (DAW 表示は 0..1)
        { LayerParameterSpecs::Param::VelMin, "velMin", "Vel Min", 0.0f,  0.0f,   1.0f,  false },
        { LayerParameterSpecs::Param::VelMax, "velMax", "Vel Max", 1.0f,  0.0f,   1.0f,  false },
        { LayerParameterSpecs::Param::EqBypass,      "eqBypass",      "EQ Bypass",       0.0f,    0.0f,     1.0f,  true },
        { LayerParameterSpecs::Param::EqLowMode,     "eqLowMode",     "EQ Low Cut",      0.0f,    0.0f,     1.0f,  true },
        { LayerParameterSpecs::Param::EqHighMode,    "eqHighMode",    "EQ High Cut",     0.0f,    0.0f,     1.0f,  true },
        { LayerParameterSpecs::Param::EqLowFreq,     "eqLowFreq",     "EQ Low Freq",     120.0f,  20.0f,    20000.0f, false },
        { LayerParameterSpecs::Param::EqLowGain,     "eqLowGain",     "EQ Low Gain",     0.0f,   -18.0f,    18.0f, false },
        { LayerParameterSpecs::Param::EqLowQ,        "eqLowQ",        "EQ Low Q",        0.7f,    0.2f,     8.0f, false },
        { LayerParameterSpecs::Param::EqLowMidFreq,  "eqLowMidFreq",  "EQ Low Mid Freq", 450.0f,  20.0f,    20000.0f, false },
        { LayerParameterSpecs::Param::EqLowMidGain,  "eqLowMidGain",  "EQ Low Mid Gain", 0.0f,   -18.0f,    18.0f, false },
        { LayerParameterSpecs::Param::EqLowMidQ,     "eqLowMidQ",     "EQ Low Mid Q",    1.0f,    0.2f,     8.0f, false },
        { LayerParameterSpecs::Param::EqHighMidFreq, "eqHighMidFreq", "EQ High Mid Freq", 2400.0f, 20.0f,   20000.0f, false },
        { LayerParameterSpecs::Param::EqHighMidGain, "eqHighMidGain", "EQ High Mid Gain", 0.0f,  -18.0f,    18.0f, false },
        { LayerParameterSpecs::Param::EqHighMidQ,    "eqHighMidQ",    "EQ High Mid Q",   1.0f,    0.2f,     8.0f, false },
        { LayerParameterSpecs::Param::EqHighFreq,    "eqHighFreq",    "EQ High Freq",    10000.0f, 20.0f,   20000.0f, false },
        { LayerParameterSpecs::Param::EqHighGain,    "eqHighGain",    "EQ High Gain",    0.0f,   -18.0f,    18.0f, false },
        { LayerParameterSpecs::Param::EqHighQ,       "eqHighQ",       "EQ High Q",       0.7f,    0.2f,     8.0f, false },
        { LayerParameterSpecs::Param::Fine,          "fine",          "Fine",            0.0f,   -100.0f,  100.0f, false },
    }};

    juce::String pagePrefix(int padIndex)
    {
        return KitData::pageLabel(KitData::pageForPad(padIndex))
             + juce::String(padIndex + 1).paddedLeft('0', 2);
    }
}

namespace LayerParameterSpecs
{
    const std::array<Spec, numAutomatableParams>& all() noexcept
    {
        return specs;
    }

    const Spec& specFor(Param param) noexcept
    {
        return specs[static_cast<size_t>(param)];
    }

    juce::String parameterID(int padIndex, int layerIndex, Param param)
    {
        return "pad" + juce::String(padIndex + 1).paddedLeft('0', 2)
             + ".layer" + juce::String(layerIndex + 1).paddedLeft('0', 2)
             + "." + specFor(param).idSuffix;
    }

    juce::String parameterName(int padIndex, int layerIndex, const PadData& pad, Param param)
    {
        return pagePrefix(padIndex) + " " + pad.padName
             + " L" + juce::String(layerIndex + 1).paddedLeft('0', 2)
             + " " + specFor(param).displayName;
    }
}
