#pragma once
#include <JuceHeader.h>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// PlaybackMode
//   OneShot : Note Off を無視して最後まで再生する
//   Gate    : Note Off でリリースに入る（ノートの長さが再生時間に影響）
// ─────────────────────────────────────────────────────────────────────────────
enum class PlaybackMode
{
    OneShot = 0,
    Gate    = 1
};

// PadColourMode
//   Auto   : padColourARGB is the category/default color and the JS UI shows
//            its own derived "auto" color (categoryColor in INITIAL_PADS).
//   Custom : padColourARGB is a user-chosen override; JS shows that color.
enum class PadColourMode
{
    Auto   = 0,
    Custom = 1
};

struct LayerEqBand
{
    float freq { 1000.0f };
    float gain { 0.0f };
    float q    { 0.9f };
};

enum class LayerEqEdgeMode
{
    Shelf = 0,
    Cut   = 1
};

struct LayerEq
{
    bool bypassed { false };
    LayerEqEdgeMode lowMode  { LayerEqEdgeMode::Shelf };
    LayerEqEdgeMode highMode { LayerEqEdgeMode::Shelf };
    LayerEqBand low      { 120.0f,   0.0f, 0.7f };
    LayerEqBand lowMid   { 450.0f,   0.0f, 1.0f };
    LayerEqBand highMid  { 2400.0f,  0.0f, 1.0f };
    LayerEqBand high     { 10000.0f, 0.0f, 0.7f };
};

enum class LayerFxType
{
    Eq = 0,
    Filter,
    Drive,
    Transient,
    Compressor
};

struct LayerFilterFx
{
    // HP / LP は完全に独立。slope (dB/oct: 12/24/48) も Q もセクションごとに持つ。
    // 両方 ON で band-pass 的に動く。
    bool  hpEnabled   { true };
    float hpCutoff    { 80.0f };
    int   hpSlope     { 12 };
    float hpResonance { 0.7f };
    bool  lpEnabled   { false };
    float lpCutoff    { 18000.0f };
    int   lpSlope     { 12 };
    float lpResonance { 0.7f };
};

struct LayerDriveFx
{
    int type { 0 }; // 0=Soft, 1=Hard, 2=Tube, 3=Tape, 4=Fold, 5=Bit Crush, 6=Rate
    float amount { 0.25f };
    float tone { 0.5f };
    float mix { 1.0f };
    float outputDb { 0.0f };
};

struct LayerTransientFx
{
    float attack { 0.0f };
    float sustain { 0.0f };
    float outputDb { 0.0f };
};

struct LayerCompressorFx
{
    float threshold { -12.0f };
    float ratio { 4.0f };
    float attack { 8.0f };
    float release { 80.0f };
    float makeupDb { 0.0f };
    float mix { 1.0f };
    float outputDb { 0.0f };
};

struct LayerFxSlot
{
    LayerFxType type { LayerFxType::Eq };
    bool bypassed { false };
    LayerEq eq {};
    LayerFilterFx filter {};
    LayerDriveFx drive {};
    LayerTransientFx transient {};
    LayerCompressorFx compressor {};
};

// ─────────────────────────────────────────────────────────────────────────────
// LayerData  ─  1 つのレイヤーが持つ情報（Pad 内で複数 Layer を重ねる仕組み）
//
// Layer は「サンプル + そのサンプルに付随する再生パラメータ」を表す。
// 1 Pad は常に最低 1 つの Layer を持つ（layers[0]）。
//
// Layer に置く設定:
//   - サンプル参照
//   - Volume / Pan / Pitch（Layer 単位の音量・パン・ピッチ）
//   - Attack / Release（エンベロープ）
//   - Start/End/FadeIn/FadeOut（トリム & フェード）
//   - Reverse / Smart Trim（サンプル依存の挙動）
//   - Layer Mute / Solo（Pad 内のレイヤー間で有効/無効）
//   - Velocity Range（このベロシティ範囲のみ発音）
//
// Pad に残す設定（LayerData には入れない）:
//   - padName, padColour, midiNote, playbackMode, chokeGroup, outputAssign
//   - Pad-level Mute/Solo, velocitySens, humanize, padVolume, padPan
// ─────────────────────────────────────────────────────────────────────────────
struct LayerData
{
    // ── サンプル参照 ───────────────────────────────────────────────────────
    juce::String sampleFileName {};
    juce::String sampleFilePath {};
    bool         sampleMissing { false };

    // ── 表示名（空ならサンプル名 / "Layer N" を UI 側で派生表示） ───────────
    juce::String layerName {};

    // ── ミキサー（Layer 単位） ───────────────────────────────────────────────
    // volume は fader position [0..1]（FaderCurve 経由でゲインに変換）
    // 0.75 = 0 dB unity, 1.0 = +12 dB, 0.0 = silence.
    float volume { 0.75f };
    float pan    { 0.0f };       // -1=L, 0=center, 1=R
    float pitch  { 0.0f };       // 半音単位
    float fine   { 0.0f };       // cents (-100..100)

    // ── エンベロープ ───────────────────────────────────────────────────────
    float attack  { 0.0f };      // 秒（0 = 原音の立ち上がりを維持）
    float release { 0.05f  };    // 秒

    // ── トリム ─────────────────────────────────────────────────────────────
    float startPosition { 0.0f };
    float endPosition   { 1.0f };
    float fadeIn        { 0.0f };
    float fadeOut       { 0.0f };

    // ── 再生（サンプル単位の挙動） ─────────────────────────────────────────
    bool reverse   { false };
    bool keepLength { true };
    bool smartTrim { true };

    // ── Layer 単位 Mute / Solo（Pad の Mute/Solo とは独立） ────────────────
    //   優先順位: Pad.mute > Layer.solo（同 Pad 内） > Layer.mute
    bool mute { false };
    bool solo { false };

    // ── Velocity Range（このベロシティ範囲内のみ発音） ─────────────────────
    //   MIDI velocity 0..127 換算。デフォルトは 0..127（全範囲）。
    int velocityMin { 0   };
    int velocityMax { 127 };

    // ── 4 Band EQ（Layer 固定 EQ）──────────────────────────────────────────
    // Low / High は shelf、Low Mid / High Mid は bell。全て DAW automation 対象。
    LayerEq eq {};

    // ── Ordered FX Chain（UI の並び順 = 処理順）───────────────────────────
    std::vector<LayerFxSlot> fxChain {};

    // ── ヘルパー ──────────────────────────────────────────────────────────
    bool hasSample() const noexcept
    {
        return sampleFilePath.isNotEmpty() && ! sampleMissing;
    }
    bool hasSampleReference() const noexcept
    {
        return sampleFileName.isNotEmpty() || sampleFilePath.isNotEmpty();
    }

    // ── シリアライズ ──────────────────────────────────────────────────────
    juce::ValueTree toValueTree() const;
    void            fromValueTree(const juce::ValueTree& vt);
};

// Pad あたりの最大 Layer 数。UI 側 (layerView.ts) と一致させる。
static constexpr int MAX_LAYERS_PER_PAD = 8;

// ─────────────────────────────────────────────────────────────────────────────
// PadData  ─  1 つのパッドが持つすべての情報
//
// 【重要な設計ルール】
//   padName          → パッドの"名前"。ユーザーが管理する。絶対に自動変更しない
//   sampleFileName   → 読み込んだファイルの名前。補助表示用（例: kick_128bpm.wav）
//   sampleFilePath   → フルパス。再読み込みに使う。UIには基本表示しない
//
//   サンプルを差し替えても padName は変わらない。
//   Output Name も padName に基づく（Phase 4 で実装）。
// ─────────────────────────────────────────────────────────────────────────────
struct PadData
{
    // ── 名前管理（最重要：3つは別物です） ────────────────────────────────────
    juce::String padName       { "PAD" };    // UI の主役：ユーザーが管理
    juce::String sampleFileName{};           // 補助表示のみ（ファイル名だけ）
    juce::String sampleFilePath{};           // 再読み込み用フルパス
    bool sampleMissing { false };            // パスは保持しているがファイルが見つからない
    juce::uint32 padColourARGB { 0xff1a2329 }; // Pad / Mixer のカテゴリ色 / user override
    PadColourMode padColourMode { PadColourMode::Auto }; // Auto = use category color; Custom = use padColourARGB as user override

    // ── MIDI ──────────────────────────────────────────────────────────────────
    int midiNote { 36 };                     // Logic 表記 C1（KICK）

    // ── 音量 / パン ───────────────────────────────────────────────────────────
    // volume is a FADER POSITION in [0..1] — NOT a linear gain.
    // 0.75 == 0 dB unity, 1.0 == +12 dB, 0.0 == -inf dB.
    // Convert through FaderCurve::positionToGain() before multiplying
    // into the signal (see VoiceManager::trigger and FaderCurve.h).
    float volume { 0.75f };
    float pan    { 0.0f };                   // -1.0（左）〜 0.0（中央）〜 1.0（右）
    float pitch  { 0.0f };                   // 半音単位（Phase 2 以降で有効化）
    float fine   { 0.0f };                   // cents (-100..100)、Layer 0 mirror

    // ── エンベロープ ──────────────────────────────────────────────────────────
    float attack  { 0.0f };                  // 秒（0 = 即時に最大音量）
    float release { 0.05f  };               // 秒（フェードアウト時間）

    // ── トリム位置（0.0〜1.0） ────────────────────────────────────────────────
    float startPosition { 0.0f };           // 再生開始位置（波形エディタで編集）
    float endPosition   { 1.0f };           // 再生終了位置

    // ── フェード（0.0〜1.0、再生範囲に対する割合） ──────────────────────────
    float fadeIn  { 0.0f };                 // フェードイン長（0=なし, 1=全体）
    float fadeOut { 0.0f };                 // フェードアウト長

    // ── 逆再生 ───────────────────────────────────────────────────────────────
    bool  reverse { false };                // true = 終端から始端に向かって再生
    bool  keepLength { true };              // Pitch変更時もトリム範囲の長さを維持

    // ── 再生モード ────────────────────────────────────────────────────────────
    PlaybackMode playbackMode { PlaybackMode::OneShot };
    int chokeGroup { 0 };                    // 0=無効, 1〜4=グループ（Phase 2 で実装）

    // ── Polyphony / Voice Steal (v7+) ─────────────────────────────────────────
    // polyphony: この Pad で同時発音できる最大ボイス数。0 = Unlimited。
    // 1..16 で上限を切る (Hi-Hat 等の擬似モノラル用途を含む)。
    int polyphony { 0 };
    // 上限到達時の挙動: 0=Oldest 1=Quietest 2=Off
    enum class VoiceStealMode { Oldest = 0, Quietest = 1, Off = 2 };
    VoiceStealMode voiceSteal { VoiceStealMode::Oldest };

    // ── ミキサー ──────────────────────────────────────────────────────────────
    bool mute { false };
    bool solo { false };
    // Phase 4: 0 〜 47 の Output インデックス。
    // デフォルトは KitData::resetToDefaults() で全 Pad を Main に設定。
    // 旧版（-1 = Main / 0〜7 = Aux）の値は KitData::fromValueTree() で 0〜47 に変換される。
    int  outputAssign { 0 };
    bool swapLR { false };                      // Pad出力の左右チャンネルを交換

    // ── ベロシティ / ヒューマナイズ ───────────────────────────────────────────
    float velocitySens { 1.0f };            // 0.0=ベロシティ無視, 1.0=完全追従
    float humanize     { 0.0f };            // 0.0〜1.0（Phase 5 で実装）

    // ── VEL Curve（ベロシティ → 音量カーブ） ─────────────────────────────────
    // 形状は (0,0) → p1 → p2 → (1,1) の折れ線 (ピースワイズ線形)。
    // presetIndex は UI 表示用 (0=Linear, 1=Soft, 2=Hard, 3=LessDynamics, 4=MoreDynamics, 5=Custom)。
    // DSP は p1/p2 の 4 値だけ参照する (preset は表示同期用)。
    struct VelCurve
    {
        int   presetIndex { 0 };       // 0..5
        float p1x { 0.33f };
        float p1y { 0.33f };
        float p2x { 0.66f };
        float p2y { 0.66f };
    };
    VelCurve velCurve;

    // ─────────────────────────────────────────────────────────────────────────
    // Pad-level Volume / Pan / Pitch / Fine（Layer パラメータの上位段）
    //
    // 信号フロー: Layer Vol/Pan/Pitch → Pad Vol/Pan/Pitch/Fine → Output Routing
    // 既存プリセット互換のため初期値は unity / center / 0 semitone / 0 cent。
    // ─────────────────────────────────────────────────────────────────────────
    float padVolume { 0.75f };  // fader position（0.75 = 0 dB unity）
    float padPan    { 0.0f  };  // -1.0〜1.0
    float padPitch  { 0.0f  };  // semitones
    float padFine   { 0.0f  };  // cents (-100..100)

    // ── Layers（1 つ以上、最大 MAX_LAYERS_PER_PAD） ───────────────────────────
    // 既存の flat fields（sampleFilePath / volume / pan / ... など）は
    // **Layer 0 のミラー** として運用する（Step 1 では音声コードはまだ flat を読む）。
    // syncLayer0FromFlat() / syncFlatFromLayer0() で同期する。
    std::vector<LayerData> layers { LayerData{} };

    int layerCount() const noexcept
    {
        return static_cast<int>(layers.size());
    }

    LayerData&       layer(int idx)       noexcept { return layers[(size_t) juce::jlimit(0, (int)layers.size() - 1, idx)]; }
    const LayerData& layer(int idx) const noexcept { return layers[(size_t) juce::jlimit(0, (int)layers.size() - 1, idx)]; }

    // Layer 0 を flat fields からコピー（保存前 / flat 書き換え後に呼ぶ）。
    void syncLayer0FromFlat() noexcept;
    // flat fields を Layer 0 からコピー（読み込み後 / Layer 0 編集後に呼ぶ）。
    void syncFlatFromLayer0() noexcept;

    // ── ヘルパー ──────────────────────────────────────────────────────────────
    bool hasSample() const noexcept { return sampleFilePath.isNotEmpty() && ! sampleMissing; }
    bool hasSampleReference() const noexcept
    {
        return sampleFileName.isNotEmpty() || sampleFilePath.isNotEmpty();
    }

    // ── シリアライズ（Kit 保存 / 読み込み用） ─────────────────────────────────
    juce::ValueTree toValueTree() const;
    void fromValueTree(const juce::ValueTree& vt);
};
