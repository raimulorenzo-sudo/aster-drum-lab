#include "RoutingPresets.h"

namespace RoutingPresets
{

juce::String getPresetName(Preset p)
{
    switch (p)
    {
        case Preset::AllToMain:          return "All to Main";
        case Preset::IndividualOuts:     return "Individual Outs";
        case Preset::StandardDrumGroups: return "Standard Drum Groups";
        case Preset::Custom:             return "Custom";
    }
    return "Custom";
}

// ─────────────────────────────────────────────────────────────────────────────
// All to Main : すべて OUT 1 へ
// ─────────────────────────────────────────────────────────────────────────────
void applyAllToMain(KitData& kit)
{
    for (auto& pad : kit.pads)
        pad.outputAssign = 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Individual Outs : Pad i → OUT i
// ─────────────────────────────────────────────────────────────────────────────
void applyIndividualOuts(KitData& kit)
{
    for (int i = 0; i < NUM_PADS; ++i)
        kit.pads[static_cast<size_t>(i)].outputAssign = i;
}

// ─────────────────────────────────────────────────────────────────────────────
// Standard Drum Groups : カテゴリで少数のアウトへまとめる
//
//   OUT 1  : KICK / 808 / BASS / SUB     （低音）
//   OUT 2  : SNARE / CLAP / RIM           （スネア系）
//   OUT 3  : HAT / CLOSED HAT / OPEN HAT  （ハット）
//   OUT 4  : RIDE / CRASH / CYMBAL        （シンバル）
//   OUT 5  : TOM                          （タム）
//   OUT 6  : PERC / SHAKER / SNAP / STOMP （パーカッション）
//   OUT 7  : VOX / VOCAL                  （ボーカル）
//   OUT 8  : FX / RISER / DOWNER / SWEEP / IMPACT / TEXTURE …  （エフェクト・その他）
// ─────────────────────────────────────────────────────────────────────────────
void applyStandardDrumGroups(KitData& kit)
{
    auto categorize = [](const juce::String& name) -> int
    {
        const auto n = name.toUpperCase();

        if (n.contains("KICK") || n.contains("808") || n.contains("BASS") || n.contains("SUB"))
            return 0;  // OUT 1 (低音)

        if (n.contains("SNARE") || n.contains("CLAP") || n.contains("RIM"))
            return 1;  // OUT 2 (スネア系)

        if (n.contains("HAT"))
            return 2;  // OUT 3 (ハット)

        if (n.contains("RIDE") || n.contains("CRASH") || n.contains("CYMBAL"))
            return 3;  // OUT 4 (シンバル)

        if (n.contains("TOM"))
            return 4;  // OUT 5 (タム)

        if (n.contains("PERC") || n.contains("SHAKER") || n.contains("SNAP") || n.contains("STOMP"))
            return 5;  // OUT 6 (パーカッション)

        if (n.contains("VOX") || n.contains("VOCAL"))
            return 6;  // OUT 7 (ボーカル)

        // FX / RISER / DOWNER / SWEEP / IMPACT / TEXTURE / NOISE / LOOP / その他
        return 7;       // OUT 8 (FX・その他)
    };

    for (int i = 0; i < NUM_PADS; ++i)
    {
        auto& pad = kit.pads[static_cast<size_t>(i)];
        pad.outputAssign = categorize(pad.padName);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// プリセット適用
// ─────────────────────────────────────────────────────────────────────────────
void apply(Preset preset, KitData& kit)
{
    switch (preset)
    {
        case Preset::AllToMain:          applyAllToMain         (kit); break;
        case Preset::IndividualOuts:     applyIndividualOuts    (kit); break;
        case Preset::StandardDrumGroups: applyStandardDrumGroups(kit); break;
        case Preset::Custom:             /* 何もしない */              break;
    }
}

} // namespace RoutingPresets
