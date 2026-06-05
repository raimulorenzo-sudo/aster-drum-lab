#pragma once
#include "SamplerVoice.h"
#include "KitData.h"
#include "AudioFileManager.h"
#include <array>
#include <atomic>

// ─────────────────────────────────────────────────────────────────────────────
// VoiceManager  ─  複数の DrumVoice をマルチアウトに振り分けて再生する
//
// Phase 4 で API 変更:
//   process() は複数のバスバッファ（最大 NUM_OUTPUTS）を受け取り、
//   各 Voice をその Pad の outputAssign に応じて該当バスへレンダリングする。
//   OutputMode == Stereo の場合は全 Voice を bus 0 へ送る。
//
// 【スレッド】
//   noteOn / noteOff / process / allNotesOff はオーディオスレッドから呼ぶ。
//   getPadLevel() / getPadClipLatched() / getPadTriggerLevel() は
//   UI スレッドから呼ぶ（単一読み取りで競合許容）。
// ─────────────────────────────────────────────────────────────────────────────
class VoiceManager
{
public:
    static constexpr int MAX_VOICES = 64;

    VoiceManager() noexcept;

    // ── Note On ────────────────────────────────────────────────────────────
    void noteOn(int                  padIndex,
                float                velocity,
                const KitData&       kit,
                const AudioFileManager& files,
                double               hostSampleRate);
    // Preview: UI で Pad をクリック / Layer を選択中に試聴するときに呼ぶ。
    // layerIndex に 0 以上を渡すとその Layer のみが鳴る（デフォルト = 0）。
    // 全 Layer を試聴したい場合は -1 を渡す。
    void previewNoteOn(int                  padIndex,
                       float                velocity,
                       const KitData&       kit,
                       const AudioFileManager& files,
                       double               hostSampleRate,
                       int                  layerIndex = 0);

    // ── Note Off ───────────────────────────────────────────────────────────
    void noteOff(int padIndex);
    void previewNoteOff(int padIndex, double hostSampleRate);
    void stopPreviewVoices(double hostSampleRate, float fadeOutMs = 7.0f) noexcept;
    bool hasActiveVoices() const noexcept;

    // ── オーディオレンダリング（マルチアウト対応） ─────────────────────────
    // busBuffers[i] は i 番目のバスのバッファ（nullptr の場合は無効バス）。
    // busCount は busBuffers 配列の長さ（最大 NUM_OUTPUTS）。
    // kit は voice → outputAssign の参照に使う（const 参照のみ）。
    // 呼び出し前に fileManager の読み取りロックを保持すること。
    void process(juce::AudioBuffer<float>* const* busBuffers,
                 int                              busCount,
                 OutputMode                       outputMode,
                 const KitData&                   kit,
                 const AudioFileManager&          files,
                 int                              numSamples,
                 double                           hostSampleRate);

    // ── 全ボイス停止 ───────────────────────────────────────────────────────
    void allNotesOff();

    // ── レベルメーター用（UI スレッドから読む） ────────────────────────────
    float getPadLevel(int padIndex) const noexcept;
    void  clearPadLevels() noexcept;
    bool  getPadClipLatched(int padIndex) const noexcept;
    void  clearPadClip(int padIndex) noexcept;
    void  clearAllPadClips() noexcept;
    float getPadTriggerLevel(int padIndex) const noexcept;
    float consumePadTriggerLevel(int padIndex) noexcept;
    bool  getPadPlayheadPosition(int padIndex, float& outPosition) const noexcept;

    // ── Per-layer feedback (UI trigger flash + level meter) ──────────────
    // consumeLayerTriggerLevel: one-shot read (→ 0). Call every broadcast tick.
    // getLayerLevel:            continuous peak (read, not consumed).
    float consumeLayerTriggerLevel(int padIndex, int layerIndex) noexcept;
    float getLayerLevel(int padIndex, int layerIndex) const noexcept;

    // Compressor ゲインリダクション量 (dB, 正値)。(pad, layer, fxスロット) 単位。
    // process() 中に全 Voice の max を集計、broadcastLevelData が読む。
    float getCompReductionDb(int padIndex, int layerIndex, int slotIndex) const noexcept;

private:
    std::array<DrumVoice, MAX_VOICES> voices;
    std::array<float, NUM_PADS>       padPeakLevels {};
    std::array<bool, NUM_PADS>        padClipLatched {};
    std::array<std::atomic<float>, NUM_PADS> padTriggerLevels {};

    // Per (Pad, Layer) one-shot trigger levels (set in startLayerVoice, consumed by UI broadcast)
    std::array<std::array<std::atomic<float>, MAX_LAYERS_PER_PAD>, NUM_PADS> layerTriggerLevels {};
    // Per (Pad, Layer) continuous peak levels (updated in process(), read by broadcastLevelData)
    std::array<std::array<float, MAX_LAYERS_PER_PAD>, NUM_PADS> layerPeakLevels {};
    // Per (Pad, Layer, FxSlot) compressor gain-reduction (dB)。process() で集計。
    std::array<std::array<std::array<float, MAX_LAYER_FX_SLOTS>, MAX_LAYERS_PER_PAD>, NUM_PADS> compReductionDb {};
    uint64_t                          triggerSerialCounter { 0 };
    juce::Random                      random;

    int findFreeVoice() const noexcept;
    void chokeVoicesForPad(int padIndex, const KitData& kit, double hostSampleRate) noexcept;

    /**
     * v7+: Polyphony limit を適用。
     * 戻り値 = true: 新規発音可能 (必要なら voice steal 済み)
     * 戻り値 = false: 'Off' モードで上限到達 → 新規発音を捨てる
     */
    bool enforcePolyphonyForPad(int padIndex, const PadData& pad, double hostSampleRate) noexcept;

public:
    /**
     * v7+: Pad の視覚フラッシュだけを発火する (Voice 生成しない)。
     * MIDI Note On 受信時に必ず呼び、サンプル有無・Mute・Polyphony 等に
     * 関係なく Pad が光るようにする。Voice 生成側 (noteOn) は別経路。
     */
    void notifyPadVisualTrigger(int padIndex, float velocity) noexcept;

private:

    // Pad の全 Layer をループして条件を満たすものを起動する。
    // previewVoice=true の場合、選択中 Layer のみが鳴る（呼び出し側で
    // previewLayerIndex を指定。-1 = 全 Layer）。
    void startVoicesForPad(int                  padIndex,
                           float                velocity,
                           const KitData&       kit,
                           const AudioFileManager& files,
                           double               hostSampleRate,
                           bool                 previewVoice,
                           int                  previewLayerIndex);

    // 1 つの Layer を起動する。Pad/Layer mute/solo/velocity range などの
    // フィルタは呼び出し側で済ませてから呼ぶ。
    void startLayerVoice(int                  padIndex,
                         int                  layerIndex,
                         float                velocity,
                         const KitData&       kit,
                         const AudioFileManager& files,
                         double               hostSampleRate,
                         bool                 previewVoice);
};
