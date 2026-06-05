#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "WaveformDisplay.h"

// ─────────────────────────────────────────────────────────────────────────────
// PadDetailEditor  ─  選択中パッドのすべてのパラメータを編集するパネル
//
// 配置（上から順）:
//   ┌─────────────────────────────────────────┐
//   │ PAD NAME [編集可能]       MIDI C2 (36)  │ ← 36px
//   ├─────────────────────────────────────────┤
//   │         WaveformDisplay (波形・S/Eハンドル)│ ← 148px
//   ├─────────────────────────────────────────┤
//   │ Volume  [=========]  Pan  [=========]   │ ← 各 28px
//   │ Pitch   [=========]                     │
//   │ Attack  [=========]  Release [========] │
//   │ FadeIn  [=========]  FadeOut [========] │
//   │ VelSens [=========]  Humanize[========] │
//   ├─────────────────────────────────────────┤
//   │ [1-Shot] [Gate] [Reverse] [Mute] [Solo] │ ← 40px
//   └─────────────────────────────────────────┘
//
// 使い方:
//   loadPad(padIndex) を呼ぶと表示が更新される。
//   各スライダー・ボタンの変更は AudioProcessor.getKit().pads[index] に即時反映。
// ─────────────────────────────────────────────────────────────────────────────
class PadDetailEditor : public juce::Component
{
public:
    explicit PadDetailEditor(DrumSamplerAudioProcessor& processor);

    // 表示するパッドを切り替える（PluginEditor から呼ぶ）
    void loadPad(int padIndex);

    // 現在表示中のパッドの値を kit から再同期（Mixer タブで変更があったとき用）
    // 波形は再生成しない（軽量）
    void refresh();

    // ── Component ─────────────────────────────────────────────────────────
    void paint(juce::Graphics& g) override;
    void resized()               override;

private:
    DrumSamplerAudioProcessor& proc;
    int currentPadIndex { -1 };

    // ── パッド名（クリックで編集） ─────────────────────────────────────────
    juce::Label padNameLabel;    // クリックで TextEditor に切り替わる

    // ── MIDI ノート表示 ──────────────────────────────────────────────────
    juce::Label midiLabel;

    // ── 右側 Pad Settings パネル ────────────────────────────────────────
    juce::Label settingsTitleLabel;
    juce::Label midiTitleLabel;
    juce::Label padNameTitleLabel;
    juce::Label sampleFileTitleLabel;
    juce::Label outputTitleLabel;
    juce::Label playModeTitleLabel;
    juce::Label chokeTitleLabel;
    juce::Label waveformFileLabel;
    juce::Label sampleFileValueLabel;
    juce::TextButton relinkSampleButton { "RELINK" };
    juce::ComboBox outputAssignBox;
    juce::ComboBox playModeBox;
    juce::ComboBox chokeGroupBox;

    // ── 波形表示 ──────────────────────────────────────────────────────────
    WaveformDisplay waveformDisplay;

    // ── スライダーを label + slider のペアで管理する内部構造体 ────────────
    struct ParamSlider
    {
        juce::Label  label;
        juce::Slider slider;
        juce::Label  value;
        int          decimals { 2 };
        juce::String suffix;
    };

    ParamSlider sStart, sEnd;
    ParamSlider sVolume, sPan, sPitch;
    ParamSlider sAttack, sRelease;
    ParamSlider sFadeIn, sFadeOut;
    ParamSlider sVelSens, sHumanize;

    // ── Smart Trim 行（波形の直下） ─────────────────────────────────────
    juce::Label       smartTrimLabel;            // "Smart Trim"
    juce::ToggleButton smartTrimToggle { "On" }; // On/Off
    juce::Label       autoFadeLabel;
    juce::ToggleButton autoFadeToggle { "On" };
    juce::TextButton  btnReanalyze    { "Re-Analyze" };

    // ── モードボタン ──────────────────────────────────────────────────────
    juce::TextButton btnOneShot  { "1-SHOT" };
    juce::TextButton btnGate     { "GATE"   };
    juce::TextButton btnReverse  { "REV"    };
    juce::TextButton btnMute     { "MUTE"   };
    juce::TextButton btnSolo     { "SOLO"   };

    // ── ヘルパー ──────────────────────────────────────────────────────────
    void setupSlider(ParamSlider& ps,
                     const juce::String& labelText,
                     double rangeMin, double rangeMax,
                     double defaultVal,
                     int    decimalPlaces,
                     const juce::String& suffix = "");

    void applyButtonStyle(juce::TextButton& btn, juce::Colour onColour);

    void updateAllFromPad();   // loadPad 内部で呼ぶ
    void updateWaveform();     // 波形表示を現在のパッドで更新
    void updateValueLabel(ParamSlider& ps);
    void rebuildOutputAssignBox();

    // 各スライダーが変化したとき pad データに書き戻す
    void wireCallbacks();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PadDetailEditor)
};
