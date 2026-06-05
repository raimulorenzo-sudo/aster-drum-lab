#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "MixerChannelStrip.h"

// ─────────────────────────────────────────────────────────────────────────────
// MixerView  ─  16ch ミキサー全体（A/B/C ページ対応）
//
// Phase 4 追加要素:
//   - Output Mode セレクタ（Stereo / 16 Outs / 32 Outs / 48 Outs）
//   - Routing Preset ボタン（All to Main / Individual Outs）
//   - Custom Routing 一時保存 / 復元
//
// レイアウト（960 × 596px）:
//   ┌──── 32px ──────────────────────────────────────────────────────────┐
//   │ MIXER  Page A   [Mode▾] [Preset▾]              [A] [B] [C]          │
//   ├────────────────────────────────────────────────────────────────────┤
//   │ ch1 │ ch2 │ ch3 │ ... │ ch16  (16ch × 60px)                        │
//   └────────────────────────────────────────────────────────────────────┘
// ─────────────────────────────────────────────────────────────────────────────
class MixerView : public juce::Component
{
public:
    explicit MixerView(DrumSamplerAudioProcessor& processor);

    void refreshLevels();
    void refreshAllFromKit();

    void paint(juce::Graphics& g) override;
    void resized()               override;

private:
    DrumSamplerAudioProcessor& proc;

    // ── ページボタン（16ch ページ切替）──────────────────────────────────
    juce::TextButton pageBtnA { "A" };
    juce::TextButton pageBtnB { "B" };
    juce::TextButton pageBtnC { "C" };
    int currentPage { 0 };

    // ── Output Mode セレクタ / Routing Preset セレクタ ─────────────────
    juce::ComboBox modeBox;     // OutputMode を選択
    juce::ComboBox presetBox;   // Routing Preset を選択
    juce::Label routingStatusLabel;
    juce::TextButton saveCustomRoutingButton { "SAVE CUSTOM" };
    juce::TextButton recallCustomRoutingButton { "RECALL" };
    juce::TextButton utilityButton { "UTILITY" };

    std::array<int, NUM_PADS> customRoutingSnapshot {};
    bool hasCustomRoutingSnapshot { false };

    std::array<std::unique_ptr<MixerChannelStrip>, 16> strips;

    void setPage(int page);
    void updatePageButtons();

    // Output Mode 変更
    void onOutputModeChanged();

    // Routing Preset 適用
    void onPresetChosen();
    void saveCustomRouting();
    void recallCustomRouting();
    void updateRoutingStatus();

    // Mixer Utility メニュー
    void showUtilityMenu();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerView)
};
