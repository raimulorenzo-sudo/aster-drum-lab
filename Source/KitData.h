#pragma once
#include "PadData.h"
#include <array>

// ─────────────────────────────────────────────────────────────────────────────
// 定数
// ─────────────────────────────────────────────────────────────────────────────
static constexpr int NUM_PADS       = 48;   // 総パッド数
static constexpr int PADS_PER_PAGE  = 16;   // 1ページあたりのパッド数
static constexpr int NUM_PAGES      = 3;    // ページ数（A / B / C）

// Phase 4: マルチアウト
static constexpr int NUM_OUTPUTS         = 48;  // 最大ステレオアウト数
static constexpr int OUTPUTS_PER_PAGE    = 16;  // Outputs Overview のページ単位
static constexpr int NUM_OUTPUT_PAGES    = 3;   // A / B / C
// kitVersion 6 ─ Layer 対応。Pad は <Layers> 子ノードを持ち、Layer 1+ の情報を含む。
// 旧バージョン（v <= 5）からの読み込みは PadData::fromValueTree が自動マイグレーション。
static constexpr int CURRENT_KIT_VERSION = 6;

// ─────────────────────────────────────────────────────────────────────────────
// OutputMode  ─  有効なアウトプット数（UI / 内部ルーティング用）
//
//   Stereo  : 全 Pad を OUT 1 にミックス（マルチアウトを使わない）
//   Outs16  : OUT 1〜16 を有効化
//   Outs32  : OUT 1〜32 を有効化
//   Outs48  : OUT 1〜48 を有効化（フル）
//
// 注: DAW のバスレイアウト自体は常に 48 バスを宣言しています。
//     OutputMode は UI と内部ルーティングのオーバーライドです。
// ─────────────────────────────────────────────────────────────────────────────
enum class OutputMode
{
    Stereo = 0,
    Outs16 = 1,
    Outs32 = 2,
    Outs48 = 3
};

inline int getActiveOutputCount(OutputMode m) noexcept
{
    switch (m)
    {
        case OutputMode::Stereo: return 1;
        case OutputMode::Outs16: return 16;
        case OutputMode::Outs32: return 32;
        case OutputMode::Outs48: return 48;
    }
    return 1;
}

inline juce::String getOutputModeName(OutputMode m)
{
    switch (m)
    {
        case OutputMode::Stereo: return "Stereo";
        case OutputMode::Outs16: return "16 Outs";
        case OutputMode::Outs32: return "32 Outs";
        case OutputMode::Outs48: return "48 Outs";
    }
    return "Stereo";
}

inline juce::String getMidiNoteName(int midiNote)
{
    static constexpr const char* noteNames[12] =
    {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };

    if (midiNote < 0 || midiNote > 127)
        return {};

    return juce::String(noteNames[midiNote % 12])
         + juce::String((midiNote / 12) - 2);
}

inline juce::String getPadDisplayNoteName(int padIndex)
{
    if (padIndex < 0 || padIndex >= NUM_PADS)
        return {};

    return getMidiNoteName(36 + padIndex);
}

// ─────────────────────────────────────────────────────────────────────────────
// OutputSlot  ─  各 OUT のメタデータ
//
//   customName    : ユーザーが設定したカスタム名（空 = Pad 名に追従）
//   followPadIndex: customName が空のとき、どの Pad の名前を使うか
//                   -1 = 自分のインデックスと同じ Pad（デフォルト）
//
//   表示名:
//     customName が非空                       → customName をそのまま表示
//     customName が空                         → pad.padName + " OUT" を表示
//                                              （followPadIndex で参照する Pad を変更可能）
// ─────────────────────────────────────────────────────────────────────────────
struct OutputSlot
{
    juce::String customName     {};   // 空ならパッド名に追従
    int          followPadIndex { -1 }; // -1 = 自分のインデックス

    juce::ValueTree toValueTree(int idx) const;
    void            fromValueTree(const juce::ValueTree& vt);
};

// ─────────────────────────────────────────────────────────────────────────────
// KitData  ─  48 パッド + 48 出力スロット + Output Mode をまとめて管理
// ─────────────────────────────────────────────────────────────────────────────
struct KitData
{
    std::array<PadData,    NUM_PADS>    pads;
    std::array<OutputSlot, NUM_OUTPUTS> outputs;
    juce::String kitName    { "Default" };
    OutputMode   outputMode { OutputMode::Outs48 };  // 48 Outsを公開しつつ、初期ルーティングはAll Main。
    // マスター出力ボリューム。pad.volume と同じくフェーダー位置 [0..1] で保持
    // (FaderCurve::positionToGain で線形ゲインへ変換)。0.75 = ユニティ(0 dB)。
    float        masterVolume { 0.75f };
    int          kitVersion { CURRENT_KIT_VERSION };
    juce::String pluginVersion { JucePlugin_VersionString };

    // ── Global Settings ─────────────────────────────────────────────────────
    bool smartTrimOnSampleLoad      { true };
    bool autoFadeOnTrim             { true };
    bool previewOnPadClick          { true };
    bool preservePadNameOnSampleLoad{ true };
    bool outputNameFollowsPadName   { true };
    OutputMode defaultOutputMode    { OutputMode::Outs48 };
    float defaultFadeOutMs          { 5.0f };

    KitData() { resetToDefaults(); }

    // ── ページ管理ヘルパー ─────────────────────────────────────────────────
    static int pageForPad(int padIndex) noexcept       { return padIndex / PADS_PER_PAGE; }
    static int firstPadOnPage(int page) noexcept       { return page * PADS_PER_PAGE; }
    static int lastPadOnPage(int page)  noexcept       { return firstPadOnPage(page) + PADS_PER_PAGE - 1; }

    static juce::String pageLabel(int page)
    {
        switch (page)
        {
            case 0:  return "A";
            case 1:  return "B";
            case 2:  return "C";
            default: return "?";
        }
    }

    static juce::uint32 defaultPadColourForIndex(int padIndex) noexcept;
    static int defaultMidiNoteForIndex(int padIndex) noexcept;

    // ── Output ヘルパー ────────────────────────────────────────────────────
    // 表示名（customName 優先、なければ Pad 名 + " OUT"）
    juce::String getOutputDisplayName(int outIdx) const;

    // 短い名前（例: "OUT 1" / "KICK OUT"）— ComboBox のメニュー用
    juce::String getOutputShortLabel(int outIdx) const;

    // ── デフォルト値にリセット ─────────────────────────────────────────────
    void resetToDefaults();

    // ── シリアライズ ───────────────────────────────────────────────────────
    juce::ValueTree toValueTree()                  const;
    void            fromValueTree(const juce::ValueTree& vt);
};
