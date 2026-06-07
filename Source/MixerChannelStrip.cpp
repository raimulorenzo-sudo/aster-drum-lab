#include "MixerChannelStrip.h"
#include "ColorPalette.h"
#include "Typography.h"

// MixerChannelStrip は親（DrumSamplerAudioProcessorEditor）が設定した
// グローバル DrumSamplerLookAndFeel を継承して描画する。
// 旧 anonymous-namespace MixerLookAndFeel は撤去した。

namespace
{
    juce::String formatDbFromLinear(double value)
    {
        if (value <= 0.0001)
            return "-inf dB";

        return juce::String(20.0 * std::log10(value), 1) + " dB";
    }

    juce::String formatPanValue(double value)
    {
        const double clamped = juce::jlimit(-1.0, 1.0, value);
        if (std::abs(clamped) < 0.005)
            return "C";

        const int amount = juce::roundToInt(std::abs(clamped) * 100.0);
        return (clamped < 0.0 ? "L" : "R") + juce::String(amount);
    }

    juce::String formatOutputPairLabel(int outputAssign)
    {
        return juce::String(outputAssign * 2 + 1) + "-" + juce::String(outputAssign * 2 + 2);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// コンストラクタ
// ─────────────────────────────────────────────────────────────────────────────
MixerChannelStrip::MixerChannelStrip(int padIndex, DrumSamplerAudioProcessor& processor)
    : proc(processor), padIdx(padIndex)
{
    // 各 Component の LookAndFeel は親（DrumSamplerAudioProcessorEditor）から
    // 自動継承される。ここで個別設定はしない。

    // ── Pan ノブ ──────────────────────────────────────────────────────────
    panKnob.setRange(-1.0, 1.0);
    panKnob.setValue(0.0, juce::dontSendNotification);
    panKnob.setSliderStyle(juce::Slider::Rotary);
    panKnob.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    panKnob.setPopupDisplayEnabled(true, false, nullptr);
    panKnob.setDoubleClickReturnValue(true, 0.0);
    panKnob.textFromValueFunction = [] (double v) { return formatPanValue(v); };
    addAndMakeVisible(panKnob);

    // ── Volume フェーダー ─────────────────────────────────────────────────
    volumeFader.setRange(0.0, 1.0);
    volumeFader.setValue(1.0, juce::dontSendNotification);
    volumeFader.setSliderStyle(juce::Slider::LinearVertical);
    volumeFader.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    volumeFader.setPopupDisplayEnabled(true, false, nullptr);
    volumeFader.setDoubleClickReturnValue(true, 1.0);
    volumeFader.textFromValueFunction = [] (double v) { return formatDbFromLinear(v); };
    addAndMakeVisible(volumeFader);

    // ── Mute / Solo ボタン（状態色だけ上書き、それ以外はグローバル LAF） ─
    btnMute.setClickingTogglesState(true);
    btnMute.setColour(juce::TextButton::buttonOnColourId, ColorPalette::muteColor());
    btnMute.setColour(juce::TextButton::textColourOnId,   ColorPalette::textPrim());
    addAndMakeVisible(btnMute);

    btnSolo.setClickingTogglesState(true);
    btnSolo.setColour(juce::TextButton::buttonOnColourId, ColorPalette::soloColor());
    btnSolo.setColour(juce::TextButton::textColourOnId,   ColorPalette::textPrim());
    addAndMakeVisible(btnSolo);

    // ── Output Assign セレクタ（色はグローバル LAF に任せる） ────────────
    addAndMakeVisible(outputBox);
    // 項目は refreshFromKit() で動的に構築する

    wireCallbacks();
    refreshFromKit();
}

// ─────────────────────────────────────────────────────────────────────────────
// コールバック配線
// ─────────────────────────────────────────────────────────────────────────────
void MixerChannelStrip::wireCallbacks()
{
    panKnob.onValueChange = [this]
    {
        proc.setAutomatablePadParameter(padIdx,
                                        PadParameterSpecs::Param::Pan,
                                        static_cast<float>(panKnob.getValue()));
    };

    volumeFader.onValueChange = [this]
    {
        proc.setAutomatablePadParameter(padIdx,
                                        PadParameterSpecs::Param::PadVolume,
                                        static_cast<float>(volumeFader.getValue()));
    };

    btnMute.onClick = [this]
    {
        proc.setAutomatablePadParameter(padIdx,
                                        PadParameterSpecs::Param::Mute,
                                        btnMute.getToggleState() ? 1.0f : 0.0f);
        repaint();  // 背景色を更新（ミュート時暗く）
    };

    btnSolo.onClick = [this]
    {
        proc.setAutomatablePadParameter(padIdx,
                                        PadParameterSpecs::Param::Solo,
                                        btnSolo.getToggleState() ? 1.0f : 0.0f);
    };

    outputBox.onChange = [this]
    {
        const int id = outputBox.getSelectedId();
        if (id <= 0) return;
        proc.getKit().pads[static_cast<size_t>(padIdx)].outputAssign = idToOutputAssign(id);
    };
}

// ─────────────────────────────────────────────────────────────────────────────
// Output ComboBox の項目を構築（OutputMode に応じてアウト数を変える）
// ─────────────────────────────────────────────────────────────────────────────
void MixerChannelStrip::rebuildOutputBox(OutputMode mode)
{
    outputBox.clear(juce::dontSendNotification);

    const int n  = getActiveOutputCount(mode);
    const auto& kit = proc.getKit();

    for (int i = 0; i < n; ++i)
    {
        // 表示: "OUT 1 — KICK OUT" など
        outputBox.addItem(kit.getOutputShortLabel(i), outputAssignToId(i));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// kit からすべての値を再読み込み
// ─────────────────────────────────────────────────────────────────────────────
void MixerChannelStrip::refreshFromKit()
{
    const auto& kit = proc.getKit();
    const auto& pad = kit.pads[static_cast<size_t>(padIdx)];

    panKnob    .setValue(pad.pan,    juce::dontSendNotification);
    volumeFader.setValue(pad.padVolume, juce::dontSendNotification);
    btnMute    .setToggleState(pad.mute, juce::dontSendNotification);
    btnSolo    .setToggleState(pad.solo, juce::dontSendNotification);

    // OutputMode に応じて ComboBox の項目を毎回再構築
    // （Pad 名や Output 名が変わっている可能性があるため）
    rebuildOutputBox(kit.outputMode);

    // 現在の assign を選択。OutputMode 外の割り当ては内部値を保持したまま
    // "95-96*" のように表示して、見えていないステレオペアであることを示す。
    const int activeMax = getActiveOutputCount(kit.outputMode);
    const int oa        = juce::jlimit(0, NUM_OUTPUTS - 1, pad.outputAssign);
    if (oa >= activeMax)
    {
        const auto hiddenLabel = formatOutputPairLabel(oa) + "*";
        outputBox.addItem(hiddenLabel, outputAssignToId(oa));
        outputBox.setTextWhenNothingSelected(hiddenLabel);
    }
    outputBox.setSelectedId(outputAssignToId(oa), juce::dontSendNotification);

    repaint();
}

// ─────────────────────────────────────────────────────────────────────────────
// レベルメーター更新（タイマーから呼ぶ）
// ─────────────────────────────────────────────────────────────────────────────
void MixerChannelStrip::setLevel(float linearPeak, bool newClipLatched)
{
    // レベルが上がった → 即座に反映。下がった → 減衰
    if (linearPeak >= meterLevel)
        meterLevel = linearPeak;
    else
        meterLevel *= kDecayRate;

    // ピークホールド
    if (linearPeak >= meterPeak)
    {
        meterPeak     = linearPeak;
        peakHoldTicks = kPeakHoldTicks;
    }
    else if (peakHoldTicks > 0)
    {
        --peakHoldTicks;
    }
    else
    {
        meterPeak = std::max(0.0f, meterPeak - kPeakFallRate);
    }

    clipLatched = newClipLatched;

    repaint();
}

void MixerChannelStrip::clearClipIndicator() noexcept
{
    clipLatched = false;
    proc.clearPadClipIndicator(padIdx);
    repaint();
}

// ─────────────────────────────────────────────────────────────────────────────
// 描画
// ─────────────────────────────────────────────────────────────────────────────
void MixerChannelStrip::paint(juce::Graphics& g)
{
    const auto  bounds = getLocalBounds();
    const int   w      = bounds.getWidth();
    const auto& pad    = proc.getKit().pads[static_cast<size_t>(padIdx)];

    // ── チャンネル背景（mute 時はわずかに暗く） ───────────────────────────
    juce::Colour bgBase = pad.mute ? ColorPalette::bgDeep()
                                   : ((padIdx % 2 == 0) ? ColorPalette::bg()
                                                        : ColorPalette::surfaceAlt());
    juce::ColourGradient bg(bgBase.brighter(0.025f), 0.0f, 0.0f,
                            bgBase.darker(0.08f),   0.0f, static_cast<float>(bounds.getHeight()), false);
    g.setGradientFill(bg);
    g.fillAll();

    // ── 右側の縦区切り線 ──────────────────────────────────────────────────
    g.setColour(ColorPalette::border());
    g.drawVerticalLine(w - 1, 0.0f, static_cast<float>(bounds.getHeight()));

    // ── 上部カラーバー（パッドカテゴリ色、3px） ──────────────────────────
    g.setColour(padCategoryColor(pad.padName).withAlpha(0.85f));
    g.fillRect(0, 0, w - 1, 3);

    g.setColour(ColorPalette::surfaceHigh().withAlpha(0.52f));
    g.fillRect(1, 4, w - 3, 37);

    // ── パッド番号（Champagne Gold） ─────────────────────────────────────
    g.setColour(ColorPalette::champagne());
    g.setFont(Typography::uiMedium(9.2f));
    g.drawText(juce::String(padIdx + 1).paddedLeft('0', 2),
               2, 3, w - 4, 12, juce::Justification::centred, false);

    // ── パッド名 ──────────────────────────────────────────────────────────
    g.setColour(pad.mute ? ColorPalette::textDim() : ColorPalette::textPrim());
    g.setFont(Typography::uiSemiBold(10.5f));
    g.drawText(pad.padName,
               2, 15, w - 4, 14, juce::Justification::centred, true);

    // ── ファイル名（小さく薄く） ─────────────────────────────────────────
    if (pad.hasSample())
    {
        juce::String fname = pad.sampleFileName;
        if (fname.length() > 8)
            fname = fname.substring(0, 6) + "..";
        g.setColour(ColorPalette::sampleText());
        g.setFont(Typography::ui(8.0f));
        g.drawText(fname, 2, 29, w - 4, 10, juce::Justification::centred, false);
    }
    else if (pad.sampleMissing)
    {
        g.setColour(ColorPalette::danger().brighter(0.10f));
        g.setFont(Typography::ui(8.0f));
        g.drawText("MISSING", 2, 29, w - 4, 10, juce::Justification::centred, false);
    }

    // ── レベルメーター（フェーダーの右に描画） ────────────────────────────
    {
        constexpr int meterW   = 8;
        const int     faderTop = 107;
        const int     meterTop = faderTop;
        const int     meterH   = std::max(10, bounds.getHeight() - 48 - faderTop);
        const int     meterX   = w - 1 - meterW - 1;  // 右端から 2px 内側

        // メーター背景
        g.setColour(ColorPalette::meterBG());
        g.fillRect(meterX, meterTop, meterW, meterH);

        // dB 変換（0.0001 フロアで -60dB に相当）
        auto toNorm = [&](float lin) -> float
        {
            if (lin <= 0.0001f) return 0.0f;
            const float dB  = 20.0f * std::log10(lin);
            return juce::jlimit(0.0f, 1.0f, (dB + 60.0f) / 60.0f);  // -60..0dB → 0..1
        };

        const float normLevel = toNorm(meterLevel);
        const float normPeak  = toNorm(meterPeak);

        // メーターバー
        if (normLevel > 0.001f)
        {
            const int barH = static_cast<int>(normLevel * meterH);
            const int barY = meterTop + meterH - barH;

            for (int y = meterTop + meterH - 1; y >= barY; --y)
            {
                const float normAtY = 1.0f - (float) (y - meterTop) / (float) juce::jmax(1, meterH - 1);
                const float dB = -60.0f + normAtY * 60.0f;
                g.setColour(meterColourForDb(dB));
                g.fillRect(meterX, y, meterW, 1);
            }
        }

        // ピークホールドライン
        if (normPeak > 0.001f)
        {
            const int pkY = meterTop + meterH - static_cast<int>(normPeak * meterH);
            g.setColour(ColorPalette::textPrim());
            g.fillRect(meterX, pkY, meterW, 1);
        }

        const auto clipBounds = getClipIndicatorBounds();
        g.setColour(clipLatched ? ColorPalette::danger()
                                : ColorPalette::borderSoft().brighter(0.2f));
        g.fillRoundedRectangle(clipBounds.toFloat(), 2.0f);
        g.setColour(clipLatched ? ColorPalette::textPrim()
                                : ColorPalette::textDim().withAlpha(0.76f));
        g.setFont(Typography::micro(7.8f));
        g.drawText("CLIP", clipBounds, juce::Justification::centred, false);

        // メーター枠線
        g.setColour(ColorPalette::border());
        g.drawRect(meterX, meterTop, meterW, meterH, 1);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// レイアウト
// ─────────────────────────────────────────────────────────────────────────────
void MixerChannelStrip::resized()
{
    const int w = getWidth();
    const int h = getHeight();

    // ── 上部ヘッダー（描画のみ、コンポーネントなし）─────────────────────
    // 3px カラーバー + 12px 番号 + 14px 名前 + 10px ファイル名 = 39px

    // ── Output Assign ──────────────────────────────────────────────────────
    outputBox.setBounds(2, 41, w - 4, 18);

    // ── Pan ノブ ──────────────────────────────────────────────────────────
    {
        const int knobSize = std::min(w - 4, 40);
        const int knobX    = (w - knobSize) / 2;
        panKnob.setBounds(knobX, 63, knobSize, knobSize);
    }

    // ── フェーダー + レベルメーター エリア ───────────────────────────────
    // 上 y = 107, 下 y = h - 48 (Mute+Soloボタン分を残す)
    const int faderTop    = 107;
    const int faderBottom = h - 48;
    const int faderH      = std::max(10, faderBottom - faderTop);

    // レベルメーターの幅（右端に配置）
    constexpr int meterW = 8;
    const int faderW = w - 4 - meterW - 2;  // 左余白2px + メーター + 間2px

    // フェーダー
    volumeFader.setBounds(2, faderTop, faderW, faderH);

    // ── Mute / Solo ボタン ───────────────────────────────────────────────
    const int btnY = h - 44;
    const int halfW = (w - 3) / 2;
    btnMute.setBounds(1, btnY,     halfW, 20);
    btnSolo.setBounds(2 + halfW, btnY, w - halfW - 3, 20);
}

void MixerChannelStrip::mouseUp(const juce::MouseEvent& e)
{
    if (getClipIndicatorBounds().contains(e.getPosition()))
    {
        clearClipIndicator();
        return;
    }

    juce::Component::mouseUp(e);
}

// ─────────────────────────────────────────────────────────────────────────────
// レベルメーターの描画（paint から呼ぶ）
// resized() では setBounds しないので paint 内で座標を計算して描く
// ─────────────────────────────────────────────────────────────────────────────
// NOTE: JUCE の paint は Component が可視化されるたびに呼ばれる。
//       レベルメーターの描画は paint 内で行い、setLevel() が repaint() を呼ぶ。

// paint() の中で続けてメーターを描く（MixerView 側から見て整合性を保つため
// paint の末尾にメーター描画を追加するかたちで実装）

// ── paint() の末尾に追記（ファイル末尾に続ける）

// ─────────────────────────────────────────────────────────────────────────────
// パッドカテゴリ色（パッド名のキーワードで色分け）
// ─────────────────────────────────────────────────────────────────────────────
juce::Colour MixerChannelStrip::padCategoryColor(const juce::String& name)
{
    const auto n = name.toUpperCase();

    if (n.contains("KICK") || n.contains("808") || n.contains("BASS"))
        return juce::Colour(0xff5060d0);  // 青紫（低音）

    if (n.contains("SNARE") || n.contains("CLAP") || n.contains("RIM"))
        return juce::Colour(0xff30a0c0);  // Ice Blue 系（打音）

    if (n.contains("HAT") || n.contains("CYMBAL") || n.contains("RIDE") || n.contains("CRASH"))
        return juce::Colour(0xff50b050);  // グリーン（シンバル）

    if (n.contains("PERC") || n.contains("TOM") || n.contains("SNAP") || n.contains("STOMP"))
        return juce::Colour(0xffc08030);  // ゴールド系（打楽器）

    if (n.contains("VOX") || n.contains("VOCAL"))
        return juce::Colour(0xffb040a0);  // パープル（ボーカル）

    if (n.contains("FX") || n.contains("SWEEP") || n.contains("RISER") || n.contains("TEXTURE"))
        return juce::Colour(0xff508080);  // ティール（エフェクト）

    return juce::Colour(0xff404040);  // デフォルト（グレー）
}

juce::Rectangle<int> MixerChannelStrip::getClipIndicatorBounds() const noexcept
{
    constexpr int meterW = 8;
    const int meterX = getWidth() - 1 - meterW - 1;
    return { meterX - 6, 86, 20, 10 };
}

juce::Colour MixerChannelStrip::meterColourForDb(float dB) noexcept
{
    if (dB >= 0.0f)   return ColorPalette::danger();
    if (dB >= -1.0f)  return juce::Colour(0xffd88a3c);
    if (dB >= -6.0f)  return juce::Colour(0xffc9a35c);
    if (dB >= -18.0f) return juce::Colour(0xff7ddcff);
    return juce::Colour(0xff1f6986);
}
