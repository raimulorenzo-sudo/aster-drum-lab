#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

// ─────────────────────────────────────────────────────────────────────────────
// MixerChannelStrip  ─  ミキサーの 1 チャンネル分
//
// 上から下へ:
//   カラーバー（パッドカテゴリ色）
//   パッド番号 / パッド名 / ファイル名
//   Output Assign セレクタ
//   Pan ノブ（Rotary）
//   Volume フェーダー（LinearVertical）＋ レベルメーター
//   Mute / Solo ボタン
//
// 直接 processor.getKit().pads[padIndex] に読み書きします。
// ─────────────────────────────────────────────────────────────────────────────
class MixerChannelStrip : public juce::Component
{
public:
    MixerChannelStrip(int padIndex, DrumSamplerAudioProcessor& processor);

    // ── 外部から呼ぶメソッド ──────────────────────────────────────────────
    // kit からすべての値を再読み込みして表示を同期する
    void refreshFromKit();

    // レベルメーター値を更新（VoiceManager から取得した線形振幅 0〜1+）
    void setLevel(float linearPeak, bool clipLatched);
    void clearClipIndicator() noexcept;

    // ── Component ─────────────────────────────────────────────────────────
    void paint(juce::Graphics& g) override;
    void resized()               override;
    void mouseUp(const juce::MouseEvent& e) override;

private:
    DrumSamplerAudioProcessor& proc;
    const int padIdx;

    // ── UI コンポーネント ─────────────────────────────────────────────────
    juce::Slider panKnob;          // Rotary
    juce::Slider volumeFader;      // LinearVertical
    juce::TextButton btnMute  { "M" };
    juce::TextButton btnSolo  { "S" };
    juce::ComboBox   outputBox;    // Output Assign

    // ── レベルメーター内部状態（paint 内で描画） ──────────────────────────
    float meterLevel    { 0.0f };  // 現在の表示レベル（減衰あり）
    float meterPeak     { 0.0f };  // ピークホールド値
    int   peakHoldTicks { 0    };  // ピークホールドカウンタ（0 になると落ち始める）
    bool  clipLatched   { false };

    static constexpr int   kPeakHoldTicks = 15;  // 500ms @ 30 Hz
    static constexpr float kDecayRate     = 0.82f;
    static constexpr float kPeakFallRate  = 0.015f;

    // ── ヘルパー ──────────────────────────────────────────────────────────
    // pad カテゴリ（pad 名）に基づく色（視覚的グルーピング用）
    static juce::Colour padCategoryColor(const juce::String& padName);

    // ComboBox の項目を OutputMode に合わせて再構築する（外部から呼ばれる refreshFromKit で実行）
    void rebuildOutputBox(OutputMode mode);

    // ComboBox id ↔ outputAssign 変換（id = outputAssign + 1）
    //   id 1 → OUT 1 (outputAssign 0)
    //   id 2 → OUT 2 (outputAssign 1)
    static int idToOutputAssign(int id)  noexcept { return id - 1; }
    static int outputAssignToId(int oa)  noexcept { return oa + 1; }

    void wireCallbacks();
    juce::Rectangle<int> getClipIndicatorBounds() const noexcept;
    static juce::Colour meterColourForDb(float dB) noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerChannelStrip)
};
