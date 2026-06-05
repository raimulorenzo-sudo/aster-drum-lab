#pragma once
#include <JuceHeader.h>
#include "PadData.h"
#include "KitData.h"

// ─────────────────────────────────────────────────────────────────────────────
// PadDataJson  ─  PadData / KitData ↔ JSON 変換 (WebView UI 用)
//
// オーディオ処理・MIDI 処理には一切触らない。
// UI 同期目的の純粋な変換ユーティリティ。
// ─────────────────────────────────────────────────────────────────────────────
namespace PadDataJson
{
    /** 1つのパッドを JUCE var (JSON 互換) に変換 */
    juce::var padToVar(const PadData& pad);

    /** KitData 全体を JUCE var に変換 ({"pads":[...], "page":0, ...}) */
    juce::var kitToVar(const KitData& kit,
                       int selectedIndex,
                       int currentPage,
                       const juce::String& kitName);

    /** var → 文字列 (UTF-8 JSON) */
    juce::String varToJson(const juce::var& v);
}
