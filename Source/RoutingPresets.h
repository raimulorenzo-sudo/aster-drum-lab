#pragma once
#include "KitData.h"

// ─────────────────────────────────────────────────────────────────────────────
// RoutingPresets  ─  Pad → OUT のルーティングをまとめて変更するヘルパー
//
//   All to Main          : すべての Pad を OUT 1 に送る
//   Individual Outs      : Pad i → OUT i（48個別アウト）
//   Standard Drum Groups : カテゴリごとに少数のアウトへまとめる
//   Custom               : ユーザー手動（メニューには出すが何もしないトリガ）
//
// 適用後は kit.pads[i].outputAssign が書き換わります。
// outputMode は変更しません（呼び出し側の責任で別途設定してください）。
// ─────────────────────────────────────────────────────────────────────────────
namespace RoutingPresets
{
    enum class Preset
    {
        AllToMain          = 0,
        IndividualOuts     = 1,
        StandardDrumGroups = 2,
        Custom             = 3
    };

    juce::String getPresetName(Preset p);

    // ── プリセット適用 ────────────────────────────────────────────────────
    void applyAllToMain         (KitData& kit);
    void applyIndividualOuts    (KitData& kit);
    void applyStandardDrumGroups(KitData& kit);

    // 名前で呼び出す（Custom は何もしない）
    void apply(Preset preset, KitData& kit);
}
