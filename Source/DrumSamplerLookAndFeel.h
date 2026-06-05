#pragma once
#include <JuceHeader.h>

// ─────────────────────────────────────────────────────────────────────────────
// DrumSamplerLookAndFeel
//   プラグイン全体のルック&フィール。
//   PluginEditor のコンストラクタで setLookAndFeel(&laf) を呼ぶと、
//   全ての子コンポーネントに自動的に伝播する。
//
// 色は ColorPalette を参照し、ここではロジックだけを書く（テーマ変更しやすく）。
// ─────────────────────────────────────────────────────────────────────────────
class DrumSamplerLookAndFeel : public juce::LookAndFeel_V4
{
public:
    DrumSamplerLookAndFeel();
    ~DrumSamplerLookAndFeel() override = default;

    // ── スライダー ────────────────────────────────────────────────────────
    // LinearHorizontal: パラメータスライダー（薄いトラック + 小さいサム）
    // LinearVertical  : ミキサーフェーダー（メタリック・グラファイト）
    void drawLinearSlider(juce::Graphics&,
                          int x, int y, int width, int height,
                          float sliderPos, float minSliderPos, float maxSliderPos,
                          juce::Slider::SliderStyle,
                          juce::Slider&) override;

    int getSliderThumbRadius(juce::Slider&) override { return 6; }

    // ── ロータリーノブ（Pan ノブなど） ───────────────────────────────────
    void drawRotarySlider(juce::Graphics&,
                          int x, int y, int width, int height,
                          float sliderPosProportional,
                          float rotaryStartAngle, float rotaryEndAngle,
                          juce::Slider&) override;

    // ── TextButton ────────────────────────────────────────────────────────
    void drawButtonBackground(juce::Graphics&,
                              juce::Button&,
                              const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override;

    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;

    // ── ComboBox ──────────────────────────────────────────────────────────
    void drawComboBox(juce::Graphics&,
                      int width, int height, bool isButtonDown,
                      int buttonX, int buttonY, int buttonW, int buttonH,
                      juce::ComboBox&) override;

    juce::Font getComboBoxFont(juce::ComboBox&) override;
    void positionComboBoxText(juce::ComboBox&, juce::Label&) override;

    // ── ToggleButton（チェックボックス） ─────────────────────────────────
    void drawTickBox(juce::Graphics&,
                     juce::Component&,
                     float x, float y, float w, float h,
                     bool ticked, bool isEnabled,
                     bool shouldDrawAsHighlighted,
                     bool shouldDrawAsDown) override;

    void drawToggleButton(juce::Graphics&,
                          juce::ToggleButton&,
                          bool shouldDrawAsHighlighted,
                          bool shouldDrawAsDown) override;

    // ── Label ─────────────────────────────────────────────────────────────
    juce::Font getLabelFont(juce::Label&) override;

    // ── Shared panel helpers ──────────────────────────────────────────────
    static void drawPanelBackground(juce::Graphics&,
                                    juce::Rectangle<float>,
                                    bool raised = true);

    static void drawInsetBackground(juce::Graphics&,
                                    juce::Rectangle<float>);

    static void drawPageSelectorPanel(juce::Graphics&,
                                      juce::Rectangle<float>);

    static void drawPageSelectorButton(juce::Graphics&,
                                       juce::Rectangle<float>,
                                       const juce::String& text,
                                       bool isSelected,
                                       bool isHighlighted,
                                       bool isDown,
                                       bool isEnabled);

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumSamplerLookAndFeel)
};
