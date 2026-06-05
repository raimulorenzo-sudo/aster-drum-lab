#pragma once
#include <JuceHeader.h>
#include "KitData.h"
#include "PadData.h"

// ─────────────────────────────────────────────────────────────────────────────
// LayerParameterSpecs  ─  Layer 単位 DAW automation 公開パラメータ定義
//
//   v7+: Layer ごとの主要パラメータを APVTS に公開する。
//   - Volume / Pan / Pitch: Layer-level mixing
//   - VelMin / VelMax: Velocity Range (どのベロシティ範囲で Layer が発音されるか)
//
//   命名規則:
//     ID:    "pad01.layer01.volume"  (1-indexed, zero-padded 2 桁)
//     Name:  "A01 KICK L01 Volume"   (Page + PadNum + PadName + Layer 表記 + 短縮表示名)
//
//   Layer 1 (MAIN) の Volume/Pan/Pitch は flat fields にミラーされるので、
//   既存の Pad-level パラメータと同じ値を指す (二重制御は parameterChanged で
//   キャンセル — Layer 0 を書いた直後に flat へ pull する)。
// ─────────────────────────────────────────────────────────────────────────────
namespace LayerParameterSpecs
{
    enum class Param
    {
        Volume = 0,
        Pan,
        Pitch,
        VelMin,
        VelMax,
        EqBypass,
        EqLowMode,
        EqHighMode,
        EqLowFreq,
        EqLowGain,
        EqLowQ,
        EqLowMidFreq,
        EqLowMidGain,
        EqLowMidQ,
        EqHighMidFreq,
        EqHighMidGain,
        EqHighMidQ,
        EqHighFreq,
        EqHighGain,
        EqHighQ,
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

    constexpr int numAutomatableParams = 20;

    const std::array<Spec, numAutomatableParams>& all() noexcept;
    const Spec&  specFor(Param param) noexcept;

    // ID: "pad01.layer01.volume" 形式
    juce::String parameterID(int padIndex, int layerIndex, Param param);
    // Display: "A01 KICK L01 Volume" 形式
    juce::String parameterName(int padIndex, int layerIndex, const PadData& pad, Param param);
}
