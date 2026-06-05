#include "DrumSamplerLookAndFeel.h"
#include "ColorPalette.h"
#include "Typography.h"
#include "Spacing.h"

namespace
{
    namespace PageSelectorStyle
    {
        constexpr float referencePanelWidth = 620.0f;
        constexpr float referencePanelHeight = 132.0f;

        constexpr float panelRadius = 14.0f;
        constexpr float panelBorderWidth = 1.0f;
        constexpr float panelTopHighlightAlpha = 0.035f;
        constexpr float panelBottomShadowAlpha = 0.28f;
        constexpr float panelOuterShadowAlpha = 0.28f;

        constexpr float buttonSize = 94.0f;
        constexpr float buttonRadius = 16.0f;
        constexpr float buttonBorderWidth = 1.5f;
        constexpr float innerInset = 5.0f;
        constexpr float innerBorderWidth = 1.0f;
        constexpr float buttonTopHighlightAlpha = 0.030f;
        constexpr float buttonBottomShadowAlpha = 0.22f;

        constexpr float selectedGlowNearRadius = 10.0f;
        constexpr float selectedGlowNearAlpha = 0.30f;
        constexpr float selectedGlowMidRadius = 22.0f;
        constexpr float selectedGlowMidAlpha = 0.18f;
        constexpr float selectedGlowFarRadius = 38.0f;
        constexpr float selectedGlowFarAlpha = 0.10f;
        constexpr float selectedPressedGlowScale = 0.75f;

        constexpr float hoverGlowRadius = 10.0f;
        constexpr float hoverGlowAlpha = 0.08f;
        constexpr float pressedYOffset = 1.0f;

        constexpr float buttonLabelSize = 28.0f;
    }

    void drawSoftGlow(juce::Graphics& g, juce::Rectangle<float> bounds,
                      juce::Colour colour, float radius, float maxAlpha)
    {
        for (int i = 3; i >= 1; --i)
        {
            const float amount = static_cast<float>(i) * Spacing::glowSpreadSmall;
            g.setColour(colour.withAlpha(maxAlpha / static_cast<float>(i + 1)));
            g.drawRoundedRectangle(bounds.expanded(amount), radius + amount, 1.0f);
        }
    }

    void drawLayeredPageGlow(juce::Graphics& g,
                             juce::Rectangle<float> bounds,
                             juce::Colour colour,
                             float radius,
                             float spread,
                             float alpha,
                             int layers)
    {
        for (int i = layers; i >= 1; --i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(layers);
            const float expansion = spread * t;
            const float layerAlpha = alpha * (1.0f - t * 0.72f) / static_cast<float>(layers);

            g.setColour(colour.withAlpha(layerAlpha));
            g.fillRoundedRectangle(bounds.expanded(expansion), radius + expansion);
        }
    }
}

// ═════════════════════════════════════════════════════════════════════════════
//  コンストラクタ: PopupMenu / 各 ColourId の既定色を設定
// ═════════════════════════════════════════════════════════════════════════════
DrumSamplerLookAndFeel::DrumSamplerLookAndFeel()
{
    // ── PopupMenu（右クリックメニューなど） ─────────────────────────────
    setColour(juce::PopupMenu::backgroundColourId,            ColorPalette::surface());
    setColour(juce::PopupMenu::textColourId,                  ColorPalette::textPrim());
    setColour(juce::PopupMenu::headerTextColourId,            ColorPalette::champagne());
    setColour(juce::PopupMenu::highlightedBackgroundColourId, ColorPalette::iceBlueBG());
    setColour(juce::PopupMenu::highlightedTextColourId,       ColorPalette::iceBlueLight());

    // ── Slider 既定色（LAF が描画に使う） ───────────────────────────────
    setColour(juce::Slider::backgroundColourId,         ColorPalette::panelInset());
    setColour(juce::Slider::trackColourId,              ColorPalette::iceBlueDim());
    setColour(juce::Slider::thumbColourId,              ColorPalette::iceBlue());
    setColour(juce::Slider::textBoxTextColourId,        ColorPalette::textSec());
    setColour(juce::Slider::textBoxBackgroundColourId,  ColorPalette::bg());
    setColour(juce::Slider::textBoxOutlineColourId,     ColorPalette::border());
    setColour(juce::Slider::rotarySliderFillColourId,   ColorPalette::iceBlue());
    setColour(juce::Slider::rotarySliderOutlineColourId,ColorPalette::borderLight());

    // ── TextButton 既定色 ────────────────────────────────────────────────
    setColour(juce::TextButton::buttonColourId,   ColorPalette::controlBase());
    setColour(juce::TextButton::buttonOnColourId, ColorPalette::iceBlueBG());
    setColour(juce::TextButton::textColourOffId,  ColorPalette::textSec());
    setColour(juce::TextButton::textColourOnId,   ColorPalette::iceBlueLight());

    // ── ComboBox 既定色 ──────────────────────────────────────────────────
    setColour(juce::ComboBox::backgroundColourId, ColorPalette::panelInset());
    setColour(juce::ComboBox::outlineColourId,    ColorPalette::border());
    setColour(juce::ComboBox::textColourId,       ColorPalette::textPrim());
    setColour(juce::ComboBox::arrowColourId,      ColorPalette::textDim());
    setColour(juce::ComboBox::buttonColourId,     ColorPalette::surface());

    // ── ToggleButton 既定色 ─────────────────────────────────────────────
    setColour(juce::ToggleButton::textColourId,         ColorPalette::textSec());
    setColour(juce::ToggleButton::tickColourId,         ColorPalette::iceBlue());
    setColour(juce::ToggleButton::tickDisabledColourId, ColorPalette::border());

    // ── Label ────────────────────────────────────────────────────────────
    setColour(juce::Label::textColourId, ColorPalette::textPrim());
    setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::Label::outlineWhenEditingColourId, ColorPalette::iceBlue());
    setColour(juce::Label::textWhenEditingColourId,    ColorPalette::textPrim());

    // ── TextEditor（Label が編集モードに入ったときに使う） ─────────────
    setColour(juce::TextEditor::backgroundColourId,        ColorPalette::panelInset());
    setColour(juce::TextEditor::textColourId,              ColorPalette::textPrim());
    setColour(juce::TextEditor::outlineColourId,           ColorPalette::border());
    setColour(juce::TextEditor::focusedOutlineColourId,    ColorPalette::iceBlue());
    setColour(juce::TextEditor::highlightColourId,         ColorPalette::iceBlueBG());
}

// ═════════════════════════════════════════════════════════════════════════════
//  スライダー描画
// ═════════════════════════════════════════════════════════════════════════════
void DrumSamplerLookAndFeel::drawLinearSlider(juce::Graphics& g,
                                               int x, int y, int width, int height,
                                               float sliderPos,
                                               float /*minSliderPos*/, float /*maxSliderPos*/,
                                               juce::Slider::SliderStyle style,
                                               juce::Slider& slider)
{
    // ── 水平スライダー（パラメータ用、薄いトラック + 小サム） ───────────
    if (style == juce::Slider::LinearHorizontal || style == juce::Slider::LinearBar)
    {
        const float cy        = y + height * 0.5f;
        const float trackH    = 4.0f;
        const float trackY    = cy - trackH * 0.5f;
        const float trackX0   = static_cast<float>(x);
        const float trackX1   = static_cast<float>(x + width);

        // トラック背景
        g.setColour(slider.findColour(juce::Slider::backgroundColourId));
        g.fillRoundedRectangle(trackX0, trackY, trackX1 - trackX0, trackH, 2.0f);

        // トラック枠
        g.setColour(ColorPalette::borderSoft());
        g.drawRoundedRectangle(trackX0, trackY, trackX1 - trackX0, trackH, 2.0f, 0.5f);

        // 中央基準のパラメータ (Pan のように -1..1) は中央から、それ以外は左端から塗りつぶし
        const auto range  = slider.getRange();
        const bool bipolar = range.getStart() < 0.0 && range.getEnd() > 0.0;

        const float zeroX = bipolar
            ? static_cast<float>(slider.valueToProportionOfLength(0.0)) * (trackX1 - trackX0) + trackX0
            : trackX0;

        const float fillLeft  = juce::jmin(zeroX, sliderPos);
        const float fillRight = juce::jmax(zeroX, sliderPos);

        if (fillRight > fillLeft + 0.5f)
        {
            g.setColour(ColorPalette::iceGlow().withAlpha(0.20f));
            g.fillRoundedRectangle(fillLeft, trackY - 1.5f, fillRight - fillLeft, trackH + 3.0f, 3.0f);

            g.setColour(slider.findColour(juce::Slider::trackColourId));
            g.fillRoundedRectangle(fillLeft, trackY, fillRight - fillLeft, trackH, 2.0f);
        }

        // ── サム（小さい縦長ピル） ────────────────────────────────────────
        const float thumbW = 4.0f;
        const float thumbH = 14.0f;
        const float thumbY = cy - thumbH * 0.5f;
        const float thumbX = sliderPos - thumbW * 0.5f;

        g.setColour(slider.findColour(juce::Slider::thumbColourId));
        g.fillRoundedRectangle(thumbX, thumbY, thumbW, thumbH, 1.5f);

        // ハイライト
        g.setColour(ColorPalette::iceBlueLight().withAlpha(0.5f));
        g.fillRect(thumbX + 0.5f, thumbY + 1.0f, thumbW - 1.0f, 1.5f);
        return;
    }

    // ── 垂直フェーダー（ミキサー用、グラファイト・メタリック） ─────────
    if (style == juce::Slider::LinearVertical)
    {
        const float cx     = x + width * 0.5f;
        const float trackW = 4.0f;

        // トラック背景（縦グラデ：上が暗く、下に向かって少し明るく）
        juce::ColourGradient tg(ColorPalette::faderTrackTop(),    cx, static_cast<float>(y),
                                ColorPalette::faderTrackBottom(), cx, static_cast<float>(y + height),
                                false);
        g.setGradientFill(tg);
        g.fillRoundedRectangle(cx - trackW * 0.5f, static_cast<float>(y), trackW,
                               static_cast<float>(height), 2.0f);

        // トラック枠
        g.setColour(ColorPalette::faderTrackEdge().withAlpha(0.85f));
        g.drawRoundedRectangle(cx - trackW * 0.5f, static_cast<float>(y), trackW,
                               static_cast<float>(height), 2.0f, 0.5f);

        // Unity マーク（フェーダー上から 25%）
        {
            const float uY = y + height * 0.25f;
            g.setColour(ColorPalette::faderUnityMark());
            g.fillRect(cx - 10.0f, uY - 0.5f, 20.0f, 1.0f);
        }

        // ── サム（メタリック） ───────────────────────────────────────────
        const float thumbW = width * 0.72f;
        const float thumbH = 12.0f;
        const float thumbY = sliderPos - thumbH * 0.5f;
        const float thumbX = cx - thumbW * 0.5f;

        // 横方向の控えめなグラデーション（左→右）
        juce::ColourGradient bg(ColorPalette::faderThumbHi(),
                                thumbX,         thumbY,
                                ColorPalette::faderThumbLo(),
                                thumbX + thumbW, thumbY,
                                false);
        g.setGradientFill(bg);
        g.fillRoundedRectangle(thumbX, thumbY, thumbW, thumbH, 3.0f);

        // 上部の細いハイライト
        g.setColour(ColorPalette::faderThumbAccent());
        g.fillRect(thumbX + 1.5f, thumbY + 1.5f, thumbW - 3.0f, 0.8f);

        // 中央の溝
        g.setColour(ColorPalette::faderNotch());
        g.fillRect(thumbX + thumbW * 0.22f, thumbY + thumbH * 0.5f - 0.5f,
                   thumbW * 0.56f, 1.0f);

        // 枠線
        g.setColour(ColorPalette::faderThumbEdge());
        g.drawRoundedRectangle(thumbX, thumbY, thumbW, thumbH, 3.0f, 0.5f);
        return;
    }

    // ── その他のスタイル: 基底クラスに任せる ─────────────────────────────
    juce::LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos,
                                           sliderPos, sliderPos, style, slider);
}

// ═════════════════════════════════════════════════════════════════════════════
//  ロータリーノブ（Pan ノブ）
// ═════════════════════════════════════════════════════════════════════════════
void DrumSamplerLookAndFeel::drawRotarySlider(juce::Graphics& g,
                                               int x, int y, int width, int height,
                                               float sliderPosProportional,
                                               float rotaryStartAngle,
                                               float rotaryEndAngle,
                                               juce::Slider& slider)
{
    const float cx = x + width  * 0.5f;
    const float cy = y + height * 0.5f;
    const float r  = std::min(width, height) * 0.35f;

    g.setColour(ColorPalette::innerShadow().withAlpha(0.55f));
    g.fillEllipse(cx - r - 2.0f, cy - r + 2.0f, (r + 2.0f) * 2.0f, (r + 2.0f) * 2.0f);

    // ── 外側のリング（深いダーク + 微妙なハイライト） ────────────────────
    {
        juce::ColourGradient ringGrad(ColorPalette::controlHover().withAlpha(0.92f), cx, cy - r,
                                      ColorPalette::backgroundDeep(),              cx, cy + r, false);
        g.setGradientFill(ringGrad);
        g.fillEllipse(cx - r, cy - r, r * 2.0f, r * 2.0f);
    }

    // 外枠
    g.setColour(ColorPalette::borderLight().withAlpha(0.64f));
    g.drawEllipse(cx - r, cy - r, r * 2.0f, r * 2.0f, 1.0f);

    g.setColour(ColorPalette::textPrim().withAlpha(0.06f));
    g.drawEllipse(cx - r + 3.0f, cy - r + 3.0f, (r - 3.0f) * 2.0f, (r - 3.0f) * 2.0f, 1.0f);

    // ── 全レンジアーク（薄いダーク） ─────────────────────────────────────
    const float arcInset = 4.0f;
    juce::Path fullArc;
    fullArc.addArc(cx - r + arcInset, cy - r + arcInset,
                   (r - arcInset) * 2.0f, (r - arcInset) * 2.0f,
                   rotaryStartAngle, rotaryEndAngle, true);
    g.setColour(ColorPalette::borderSoft().withAlpha(0.92f));
    g.strokePath(fullArc, juce::PathStrokeType(2.0f,
        juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // ── アクティブアーク（中央基準 or 開始端基準） ───────────────────────
    const auto range   = slider.getRange();
    const bool bipolar = range.getStart() < 0.0 && range.getEnd() > 0.0;
    const float midAngle = (rotaryStartAngle + rotaryEndAngle) * 0.5f;
    const float angle    = rotaryStartAngle
                         + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

    const float fromA = bipolar ? midAngle         : rotaryStartAngle;
    const float toA   = angle;

    if (std::abs(toA - fromA) > 0.005f)
    {
        juce::Path activeArc;
        activeArc.addArc(cx - r + arcInset, cy - r + arcInset,
                         (r - arcInset) * 2.0f, (r - arcInset) * 2.0f,
                         std::min(fromA, toA), std::max(fromA, toA), true);
        g.setColour(ColorPalette::iceGlow().withAlpha(0.20f));
        g.strokePath(activeArc, juce::PathStrokeType(5.2f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        g.setColour(slider.findColour(juce::Slider::rotarySliderFillColourId));
        g.strokePath(activeArc, juce::PathStrokeType(2.4f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // ── インジケータライン ───────────────────────────────────────────────
    const float pr = r - 5.0f;
    const float px = cx + pr * std::sin(angle);
    const float py = cy - pr * std::cos(angle);
    g.setColour(ColorPalette::textSoft());
    g.drawLine(cx, cy, px, py, 1.25f);

    // ── センタードット ───────────────────────────────────────────────────
    g.setColour(ColorPalette::iceBlue().withAlpha(0.92f));
    g.fillEllipse(cx - 1.7f, cy - 1.7f, 3.4f, 3.4f);
}

// ═════════════════════════════════════════════════════════════════════════════
//  TextButton 背景
// ═════════════════════════════════════════════════════════════════════════════
void DrumSamplerLookAndFeel::drawButtonBackground(juce::Graphics& g,
                                                   juce::Button& button,
                                                   const juce::Colour& backgroundColour,
                                                   bool shouldDrawButtonAsHighlighted,
                                                   bool shouldDrawButtonAsDown)
{
    const int groupId = button.getRadioGroupId();
    const bool isTabButton = (groupId == 10);
    const bool isPageButton = (groupId == 1 || groupId == 2);

    auto bounds = button.getLocalBounds().toFloat().reduced(isTabButton ? 0.0f : 0.5f);
    const float radius = isTabButton ? Spacing::radiusSmall
                                     : (isPageButton ? Spacing::radiusControl
                                                     : Spacing::radiusButton);
    const bool isOn = button.getToggleState();
    const bool isEnabled = button.isEnabled();

    juce::Colour fill = isEnabled ? backgroundColour : ColorPalette::controlDisabled();

    if (isTabButton)
        fill = isOn ? ColorPalette::controlRaised().brighter(0.02f)
                    : ColorPalette::backgroundDeep().brighter(0.02f);
    else if (isPageButton)
        fill = isOn ? ColorPalette::iceBlueFill().interpolatedWith(ColorPalette::controlRaised(), 0.56f)
                    : ColorPalette::controlBase();

    if (shouldDrawButtonAsDown)
        fill = ColorPalette::controlPressed();
    else if (shouldDrawButtonAsHighlighted && ! isOn)
        fill = fill.brighter(isTabButton ? 0.045f : 0.070f);

    if (isOn && isEnabled)
        drawSoftGlow(g, bounds, ColorPalette::iceGlow(), radius,
                     isTabButton ? 0.12f : (isPageButton ? 0.16f : 0.18f));

    juce::ColourGradient bg(fill.brighter(isOn ? 0.055f : 0.030f), bounds.getCentreX(), bounds.getY(),
                            fill.darker (isOn ? 0.050f : 0.140f), bounds.getCentreX(), bounds.getBottom(),
                            false);
    g.setGradientFill(bg);
    g.fillRoundedRectangle(bounds, radius);

    if (shouldDrawButtonAsDown)
    {
        g.setColour(ColorPalette::innerShadow().withAlpha(0.30f));
        g.fillRoundedRectangle(bounds.reduced(1.0f), radius - 0.5f);
    }

    // 枠線（On 状態なら Ice Blue 寄り、それ以外は薄い）
    g.setColour(! isEnabled ? ColorPalette::borderSoft().withAlpha(0.42f)
                            : (isOn ? ColorPalette::iceBlue().withAlpha(0.82f)
                                    : (shouldDrawButtonAsHighlighted ? ColorPalette::iceBlueDim().withAlpha(0.55f)
                                                                     : ColorPalette::borderSoft().withAlpha(isTabButton ? 0.52f : 0.82f))));
    g.drawRoundedRectangle(bounds, radius, 1.0f);

    if (isOn && isEnabled)
    {
        g.setColour(ColorPalette::iceBlue().withAlpha(isTabButton ? 0.070f : 0.10f));
        g.fillRoundedRectangle(bounds.reduced(2.0f), radius - 1.0f);

        if (isTabButton)
        {
            g.setColour(ColorPalette::iceBlue().withAlpha(0.85f));
            g.fillRoundedRectangle(bounds.withTop(bounds.getBottom() - 2.0f), 1.0f);
        }
    }

    g.setColour(ColorPalette::textPrim().withAlpha(0.055f));
    g.drawHorizontalLine(1, bounds.getX() + 3.0f, bounds.getRight() - 3.0f);
}

juce::Font DrumSamplerLookAndFeel::getTextButtonFont(juce::TextButton& button, int buttonHeight)
{
    const int groupId = button.getRadioGroupId();

    if (groupId == 10)
        return Typography::buttonText(12.0f);

    if (groupId == 1 || groupId == 2)
        return Typography::buttonText(12.2f);

    const float size = juce::jlimit(9.5f, 12.5f, buttonHeight * 0.42f);
    return Typography::buttonText(size);
}

// ═════════════════════════════════════════════════════════════════════════════
//  ComboBox
// ═════════════════════════════════════════════════════════════════════════════
void DrumSamplerLookAndFeel::drawComboBox(juce::Graphics& g,
                                           int width, int height, bool isButtonDown,
                                           int /*buttonX*/, int /*buttonY*/,
                                           int /*buttonW*/, int /*buttonH*/,
                                           juce::ComboBox& box)
{
    auto bounds = juce::Rectangle<float>(0, 0, (float)width, (float)height).reduced(0.5f);
    const float radius = Spacing::radiusControl;

    // 背景
    juce::ColourGradient bg(ColorPalette::fieldTop(), bounds.getCentreX(), bounds.getY(),
                            ColorPalette::field(),    bounds.getCentreX(), bounds.getBottom(),
                            false);
    g.setGradientFill(bg);
    g.fillRoundedRectangle(bounds, radius);

    g.setColour(ColorPalette::shadowInner().withAlpha(0.18f));
    g.drawRoundedRectangle(bounds.reduced(1.0f), radius - 1.0f, 1.0f);

    // 枠線
    g.setColour(isButtonDown ? ColorPalette::iceBlue().withAlpha(0.65f)
                             : box.findColour(juce::ComboBox::outlineColourId).withAlpha(0.86f));
    g.drawRoundedRectangle(bounds, radius, 1.0f);

    // 矢印
    juce::Path arrow;
    const float arrowW = 7.0f;
    const float arrowH = 4.0f;
    const float ax = width - 10.0f;
    const float ay = height * 0.5f - arrowH * 0.5f;
    arrow.startNewSubPath(ax,            ay);
    arrow.lineTo         (ax + arrowW,   ay);
    arrow.lineTo         (ax + arrowW * 0.5f, ay + arrowH);
    arrow.closeSubPath();

    g.setColour(isButtonDown
                  ? box.findColour(juce::ComboBox::arrowColourId).brighter(0.3f)
                  : box.findColour(juce::ComboBox::arrowColourId));
    g.fillPath(arrow);
}

juce::Font DrumSamplerLookAndFeel::getComboBoxFont(juce::ComboBox& box)
{
    return Typography::comboText(juce::jmin(11.5f, box.getHeight() * 0.52f));
}

void DrumSamplerLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label)
{
    label.setBounds(6, 0, box.getWidth() - 22, box.getHeight());
    label.setFont(getComboBoxFont(box));
}

// ═════════════════════════════════════════════════════════════════════════════
//  ToggleButton（チェックボックス）
// ═════════════════════════════════════════════════════════════════════════════
void DrumSamplerLookAndFeel::drawTickBox(juce::Graphics& g,
                                          juce::Component& /*component*/,
                                          float x, float y, float w, float h,
                                          bool ticked, bool /*isEnabled*/,
                                          bool shouldDrawAsHighlighted,
                                          bool /*shouldDrawAsDown*/)
{
    const float boxSize = juce::jmin(w, h) - 2.0f;
    const auto box = juce::Rectangle<float>(x, y + (h - boxSize) * 0.5f, boxSize, boxSize);

    if (ticked)
        drawSoftGlow(g, box, ColorPalette::iceGlow(), Spacing::radiusSmall, 0.12f);

    juce::Colour fill = ticked ? ColorPalette::iceBlueBG()
                               : (shouldDrawAsHighlighted ? ColorPalette::controlHover()
                                                          : ColorPalette::controlBase());
    juce::ColourGradient bg(fill.brighter(0.04f), box.getCentreX(), box.getY(),
                            fill.darker(0.12f), box.getCentreX(), box.getBottom(), false);
    g.setGradientFill(bg);
    g.fillRoundedRectangle(box, Spacing::radiusSmall);

    // 枠線
    g.setColour(ticked
                  ? ColorPalette::iceBlue()
                  : (shouldDrawAsHighlighted ? ColorPalette::borderLight() : ColorPalette::border()));
    g.drawRoundedRectangle(box, Spacing::radiusSmall, 1.0f);

    // チェックマーク
    if (ticked)
    {
        juce::Path tick;
        const float cx = box.getX(), cy = box.getY(), s = box.getWidth();
        tick.startNewSubPath(cx + s * 0.22f, cy + s * 0.50f);
        tick.lineTo         (cx + s * 0.45f, cy + s * 0.72f);
        tick.lineTo         (cx + s * 0.80f, cy + s * 0.28f);

        g.setColour(ColorPalette::iceBlue());
        g.strokePath(tick, juce::PathStrokeType(1.6f,
                     juce::PathStrokeType::curved,
                     juce::PathStrokeType::rounded));
    }
}

void DrumSamplerLookAndFeel::drawToggleButton(juce::Graphics& g,
                                               juce::ToggleButton& button,
                                               bool shouldDrawAsHighlighted,
                                               bool shouldDrawAsDown)
{
    const float tickSize = 14.0f;
    const float tickY    = (button.getHeight() - tickSize) * 0.5f;

    drawTickBox(g, button,
                4.0f, tickY, tickSize, tickSize,
                button.getToggleState(),
                button.isEnabled(),
                shouldDrawAsHighlighted, shouldDrawAsDown);

    // テキスト
    g.setColour(button.findColour(juce::ToggleButton::textColourId));
    g.setFont(Typography::ui(11.0f));
    g.drawText(button.getButtonText(),
               static_cast<int>(tickSize + 10.0f), 0,
               button.getWidth() - static_cast<int>(tickSize + 12.0f),
               button.getHeight(),
               juce::Justification::centredLeft, false);
}

// ═════════════════════════════════════════════════════════════════════════════
//  Label
// ═════════════════════════════════════════════════════════════════════════════
juce::Font DrumSamplerLookAndFeel::getLabelFont(juce::Label& label)
{
    // ラベル固有のフォントが設定されていればそれを尊重
    return label.getFont();
}

void DrumSamplerLookAndFeel::drawPanelBackground(juce::Graphics& g,
                                                  juce::Rectangle<float> bounds,
                                                  bool raised)
{
    const auto radius = Spacing::radiusPanel;

    g.setColour(ColorPalette::shadowOuter().withAlpha(raised ? 0.34f : 0.22f));
    g.fillRoundedRectangle(bounds.translated(0.0f, raised ? 2.0f : 1.0f), radius);

    const auto top = raised ? ColorPalette::panelRaised().brighter(0.035f)
                            : ColorPalette::panel().brighter(0.018f);
    const auto bottom = raised ? ColorPalette::panel().darker(0.10f)
                               : ColorPalette::backgroundDeep().brighter(0.035f);

    juce::ColourGradient bg(top, bounds.getCentreX(), bounds.getY(),
                            bottom, bounds.getCentreX(), bounds.getBottom(), false);
    g.setGradientFill(bg);
    g.fillRoundedRectangle(bounds, radius);

    g.setColour(ColorPalette::textMain().withAlpha(0.040f));
    g.drawHorizontalLine(static_cast<int>(bounds.getY()) + 1,
                         bounds.getX() + 6.0f, bounds.getRight() - 6.0f);

    g.setColour(ColorPalette::borderStrong().withAlpha(0.86f));
    g.drawRoundedRectangle(bounds, radius, Spacing::hairline);
}

void DrumSamplerLookAndFeel::drawInsetBackground(juce::Graphics& g,
                                                  juce::Rectangle<float> bounds)
{
    const auto radius = Spacing::radiusControl;

    juce::ColourGradient bg(ColorPalette::fieldTop(), bounds.getCentreX(), bounds.getY(),
                            ColorPalette::panelInset(), bounds.getCentreX(), bounds.getBottom(), false);
    g.setGradientFill(bg);
    g.fillRoundedRectangle(bounds, radius);

    g.setColour(ColorPalette::shadowInner().withAlpha(0.20f));
    g.drawRoundedRectangle(bounds.reduced(1.0f), radius - 1.0f, Spacing::hairline);

    g.setColour(ColorPalette::borderSoft().withAlpha(0.90f));
    g.drawRoundedRectangle(bounds, radius, Spacing::hairline);
}

void DrumSamplerLookAndFeel::drawPageSelectorPanel(juce::Graphics& g,
                                                   juce::Rectangle<float> bounds)
{
    const float scale = juce::jmin(bounds.getWidth() / PageSelectorStyle::referencePanelWidth,
                                   bounds.getHeight() / PageSelectorStyle::referencePanelHeight);
    const float radius = PageSelectorStyle::panelRadius * scale;

    bounds = bounds.reduced(PageSelectorStyle::panelBorderWidth * scale);

    g.setColour(juce::Colours::black.withAlpha(PageSelectorStyle::panelOuterShadowAlpha));
    g.fillRoundedRectangle(bounds.translated(0.0f, 8.0f * scale), radius + 2.0f * scale);
    g.setColour(juce::Colours::white.withAlpha(0.020f));
    g.drawRoundedRectangle(bounds.reduced(1.0f * scale), radius - 1.0f * scale, 1.0f * scale);

    juce::ColourGradient bg(ColorPalette::pageSelectorPanelTop().withAlpha(0.22f),
                            bounds.getCentreX(), bounds.getY(),
                            ColorPalette::pageSelectorPanelBottom().withAlpha(0.90f),
                            bounds.getCentreX(), bounds.getBottom(),
                            false);
    bg.addColour(0.12, juce::Colour(0xff0a0e12));
    g.setGradientFill(bg);
    g.fillRoundedRectangle(bounds, radius);

    g.setColour(juce::Colours::white.withAlpha(PageSelectorStyle::panelTopHighlightAlpha));
    g.drawHorizontalLine(static_cast<int>(bounds.getY()) + 1,
                         bounds.getX() + 12.0f * scale, bounds.getRight() - 12.0f * scale);

    auto bottomShade = bounds.withTop(bounds.getBottom() - 18.0f * scale);
    juce::ColourGradient bottomShadow(juce::Colours::transparentBlack, bottomShade.getCentreX(), bottomShade.getY(),
                                      juce::Colours::black.withAlpha(PageSelectorStyle::panelBottomShadowAlpha),
                                      bottomShade.getCentreX(), bottomShade.getBottom(), false);
    g.setGradientFill(bottomShadow);
    g.fillRoundedRectangle(bottomShade, radius);

    g.setColour(juce::Colour(0xff6e7d91).withAlpha(0.18f));
    g.drawRoundedRectangle(bounds, radius, PageSelectorStyle::panelBorderWidth * scale);
}

void DrumSamplerLookAndFeel::drawPageSelectorButton(juce::Graphics& g,
                                                    juce::Rectangle<float> bounds,
                                                    const juce::String& text,
                                                    bool isSelected,
                                                    bool isHighlighted,
                                                    bool isDown,
                                                    bool isEnabled)
{
    const float scale = juce::jmin(bounds.getWidth() / PageSelectorStyle::buttonSize,
                                   bounds.getHeight() / PageSelectorStyle::buttonSize);
    const float radius = PageSelectorStyle::buttonRadius * scale;
    const float borderWidth = PageSelectorStyle::buttonBorderWidth * scale;
    const float innerInset = PageSelectorStyle::innerInset * scale;

    auto button = bounds.reduced(1.0f * scale);
    if (isDown)
        button.translate(0.0f, PageSelectorStyle::pressedYOffset * scale);

    if (isSelected && isEnabled)
    {
        const float glowScale = isDown ? PageSelectorStyle::selectedPressedGlowScale : 1.0f;

        drawLayeredPageGlow(g, button, juce::Colour(0xff2266a8), radius,
                            PageSelectorStyle::selectedGlowFarRadius * scale,
                            PageSelectorStyle::selectedGlowFarAlpha * glowScale, 12);
        drawLayeredPageGlow(g, button, juce::Colour(0xff2f8edc), radius,
                            PageSelectorStyle::selectedGlowMidRadius * scale,
                            PageSelectorStyle::selectedGlowMidAlpha * glowScale, 9);
        drawLayeredPageGlow(g, button, juce::Colour(0xff3fbaff), radius,
                            PageSelectorStyle::selectedGlowNearRadius * scale,
                            PageSelectorStyle::selectedGlowNearAlpha * glowScale, 6);
    }
    else if (isEnabled)
    {
        const float shadowAlpha = isHighlighted ? 0.18f : 0.22f;
        if (isHighlighted)
        {
            drawLayeredPageGlow(g, button, juce::Colour(0xff4696dc), radius,
                                PageSelectorStyle::hoverGlowRadius * scale,
                                PageSelectorStyle::hoverGlowAlpha, 5);
        }

        g.setColour(juce::Colours::black.withAlpha(isDown ? 0.18f : shadowAlpha));
        g.fillRoundedRectangle(button.translated(0.0f, isDown ? 1.0f * scale : 4.0f * scale), radius);
    }

    juce::Colour top;
    juce::Colour middle;
    juce::Colour bottom;
    juce::Colour border;
    juce::Colour innerBorder;
    juce::Colour textColour;

    if (! isEnabled)
    {
        top = juce::Colour(0xff0c1014).withAlpha(0.55f);
        middle = top;
        bottom = juce::Colour(0xff080a0d).withAlpha(0.78f);
        border = juce::Colour(0xff78828c).withAlpha(0.10f);
        innerBorder = juce::Colours::transparentBlack;
        textColour = juce::Colour(0xffb4bec8).withAlpha(0.28f);
    }
    else if (isDown)
    {
        if (isSelected)
        {
            top = juce::Colour(0xff243d52).withAlpha(0.77f);
            middle = juce::Colour(0xff1d3446).withAlpha(0.82f);
            bottom = juce::Colour(0xff121f2a).withAlpha(0.90f);
            border = juce::Colour(0xff69cfff).withAlpha(0.70f);
            innerBorder = juce::Colour(0xffa0e4ff).withAlpha(0.24f);
            textColour = juce::Colour(0xffc6ebff).withAlpha(0.84f);
        }
        else
        {
            top = juce::Colour(0xff11171d).withAlpha(0.68f);
            middle = top;
            bottom = juce::Colour(0xff06090d).withAlpha(0.92f);
            border = juce::Colour(0xff788291).withAlpha(0.18f);
            innerBorder = juce::Colours::transparentBlack;
            textColour = juce::Colour(0xffdbe0e6).withAlpha(0.62f);
        }
    }
    else if (isSelected)
    {
        top = juce::Colour(0xff243d52).withAlpha(0.82f);
        middle = juce::Colour(0xff1d3446).withAlpha(0.88f);
        bottom = juce::Colour(0xff121f2a).withAlpha(0.96f);
        border = juce::Colour(0xff69cfff).withAlpha(0.95f);
        innerBorder = juce::Colour(0xffa0e4ff).withAlpha(0.34f);
        textColour = juce::Colour(0xffc6ebff).withAlpha(0.96f);
    }
    else if (isHighlighted)
    {
        top = juce::Colour(0xff151d25).withAlpha(0.78f);
        middle = top;
        bottom = juce::Colour(0xff0a0f14).withAlpha(0.98f);
        border = juce::Colour(0xff7daad2).withAlpha(0.30f);
        innerBorder = juce::Colours::transparentBlack;
        textColour = juce::Colour(0xffe8eef5).withAlpha(0.84f);
    }
    else
    {
        top = juce::Colour(0xff12181e).withAlpha(0.72f);
        middle = top;
        bottom = juce::Colour(0xff070a0e).withAlpha(0.96f);
        border = juce::Colour(0xff788291).withAlpha(0.22f);
        innerBorder = juce::Colours::transparentBlack;
        textColour = juce::Colour(0xffdbe0e6).withAlpha(0.72f);
    }

    juce::ColourGradient bg(top, button.getCentreX(), button.getY(),
                            bottom, button.getCentreX(), button.getBottom(), false);
    bg.addColour(0.52, middle);
    g.setGradientFill(bg);
    g.fillRoundedRectangle(button, radius);

    g.setColour(juce::Colours::white.withAlpha(isSelected ? 0.060f : PageSelectorStyle::buttonTopHighlightAlpha));
    g.drawHorizontalLine(static_cast<int>(button.getY()) + 1,
                         button.getX() + 8.0f * scale, button.getRight() - 8.0f * scale);

    auto lowerInset = button.withTop(button.getY() + button.getHeight() * 0.48f).reduced(1.0f * scale);
    juce::ColourGradient lowerShadow(juce::Colours::transparentBlack, lowerInset.getCentreX(), lowerInset.getY(),
                                     juce::Colours::black.withAlpha(isDown ? 0.20f : (isSelected ? 0.16f : PageSelectorStyle::buttonBottomShadowAlpha)),
                                     lowerInset.getCentreX(), lowerInset.getBottom(), false);
    g.setGradientFill(lowerShadow);
    g.fillRoundedRectangle(lowerInset, radius - 1.0f * scale);

    if (isSelected && isEnabled)
    {
        g.setColour(juce::Colour(0xff55beff).withAlpha(isDown ? 0.055f : 0.080f));
        g.fillRoundedRectangle(button.reduced(4.0f * scale), radius - 4.0f * scale);

        g.setColour(juce::Colours::white.withAlpha(isDown ? 0.020f : 0.060f));
        g.drawHorizontalLine(static_cast<int>(button.getY()) + 2,
                             button.getX() + 10.0f * scale, button.getRight() - 10.0f * scale);

        g.setColour(innerBorder);
        g.drawRoundedRectangle(button.reduced(innerInset), radius - innerInset, PageSelectorStyle::innerBorderWidth * scale);
    }
    else if (isEnabled && ! isSelected)
    {
        g.setColour(juce::Colours::white.withAlpha(0.015f));
        g.drawHorizontalLine(static_cast<int>(button.getY()) + 2,
                             button.getX() + 8.0f * scale, button.getRight() - 8.0f * scale);
    }

    g.setColour(border);
    g.drawRoundedRectangle(button, radius, isSelected ? borderWidth : 1.0f * scale);

    if (isSelected && isEnabled)
    {
        g.setColour(juce::Colour(0xff5abeff).withAlpha(isDown ? 0.11f : 0.18f));
        g.setFont(Typography::semiBold(PageSelectorStyle::buttonLabelSize * scale));
        g.drawText(text, button.toNearestInt().translated(0, 1), juce::Justification::centred, false);
    }
    g.setColour(textColour);
    g.setFont(Typography::semiBold(PageSelectorStyle::buttonLabelSize * scale));
    g.drawText(text, button.toNearestInt(), juce::Justification::centred, false);
}
