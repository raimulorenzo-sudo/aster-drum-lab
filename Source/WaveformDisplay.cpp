#include "WaveformDisplay.h"
#include "ColorPalette.h"
#include "Typography.h"
#include "Spacing.h"

// ─────────────────────────────────────────────────────────────────────────────
// 内部ヘルパー
// ─────────────────────────────────────────────────────────────────────────────
namespace
{
    juce::String percentText(float value)
    {
        return juce::String(juce::roundToInt(value * 100.0f)) + "%";
    }

    juce::String fadePercentText(float value)
    {
        return juce::String(juce::roundToInt(value * 100.0f)) + "%";
    }

    // labelW=74, h=34 の pill ラベル。centreX は呼び出し元でクランプ済みであること。
    void drawHandleLabel(juce::Graphics& g, int centreX, int y,
                         const juce::String& title,
                         const juce::String& value,
                         juce::Colour colour)
    {
        constexpr int labelW = 74;
        const int x = centreX - labelW / 2;

        g.setColour(ColorPalette::panelInset().withAlpha(0.85f));
        g.fillRoundedRectangle((float)x, (float)y, (float)labelW, 34.0f, Spacing::radiusSmall);

        g.setColour(colour.withAlpha(0.45f));
        g.drawRoundedRectangle((float)x + 0.5f, (float)y + 0.5f,
                               (float)labelW - 1.0f, 33.0f, Spacing::radiusSmall, 1.0f);

        g.setColour(colour);
        g.setFont(Typography::sectionTitle(8.6f));
        g.drawText(title, x, y + 4, labelW, 11, juce::Justification::centred, false);

        g.setColour(ColorPalette::textSub());
        g.setFont(Typography::micro(8.4f));
        g.drawText(value, x, y + 18, labelW, 11, juce::Justification::centred, false);
    }
}

// ═════════════════════════════════════════════════════════════════════════════
WaveformDisplay::WaveformDisplay()
{
    setMouseCursor(juce::MouseCursor::NormalCursor);
}

// ─────────────────────────────────────────────────────────────────────────────
void WaveformDisplay::setSample(const juce::AudioBuffer<float>* buf)
{
    const int w = std::max(getWidth(), 4);
    buildThumbnail(buf, w);
    repaint();
}

void WaveformDisplay::setStartPosition(float v) { startPos = juce::jlimit(0.0f, 1.0f, v); repaint(); }
void WaveformDisplay::setEndPosition  (float v) { endPos   = juce::jlimit(0.0f, 1.0f, v); repaint(); }
void WaveformDisplay::setFadeIn       (float v) { fadeIn   = juce::jlimit(0.0f, 1.0f, v); repaint(); }
void WaveformDisplay::setFadeOut      (float v) { fadeOut  = juce::jlimit(0.0f, 1.0f, v); repaint(); }
void WaveformDisplay::setReversed     (bool  r) { reversed = r; repaint(); }

void WaveformDisplay::setPlayhead(float v, bool active)
{
    playheadPos    = juce::jlimit(0.0f, 1.0f, v);
    playheadActive = active;
    repaint();
}

// ─────────────────────────────────────────────────────────────────────────────
// 描画
//
// 描画レイヤー:
//   1. 背景 / グリッド
//   2. 波形（暗）+ 範囲外ダーク + 波形（明）
//   3. Fade In レイヤー  (独立: ゴールドのグラデオーバーレイ + 上昇カーブ)
//   4. Fade Out レイヤー (独立: ゴールドのグラデオーバーレイ + 下降カーブ)
//   5. プレイヘッド
//   6. ハンドル (Fade: 小さなゴールド円、Start/End: アイスブルーのトップタブ)
//   7. 枠線
//
// Fade In と Fade Out は別レイヤーで独立描画する。
// 中央でつなげて 1 つの図形（ダイヤモンド等）にしない。
// ─────────────────────────────────────────────────────────────────────────────
void WaveformDisplay::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds();
    const int  w      = bounds.getWidth();
    const int  h      = bounds.getHeight();
    const float midY  = h * 0.5f;

    // ── 背景 ─────────────────────────────────────────────────────────────────
    auto rf = bounds.toFloat().reduced(0.5f);
    const float radius = Spacing::radiusCard;

    g.setColour(ColorPalette::shadowOuter().withAlpha(0.34f));
    g.fillRoundedRectangle(rf.translated(0.0f, 2.0f), radius);

    juce::ColourGradient bg(ColorPalette::panelInset().brighter(0.045f), 0.0f, 0.0f,
                            ColorPalette::backgroundDeep(),               0.0f, (float)h, false);
    g.setGradientFill(bg);
    g.fillRoundedRectangle(rf, radius);

    g.setColour(ColorPalette::textMain().withAlpha(0.040f));
    g.drawHorizontalLine(1, rf.getX() + 6.0f, rf.getRight() - 6.0f);
    g.setColour(ColorPalette::shadowInner().withAlpha(0.24f));
    g.drawRoundedRectangle(rf.reduced(1.0f), radius - 1.0f, 1.0f);

    if (!hasSampleData)
    {
        g.setColour(ColorPalette::textDim());
        g.setFont(Typography::ui(12.0f));
        g.drawText("(no sample)", bounds, juce::Justification::centred, false);
        g.setColour(ColorPalette::borderStrong());
        g.drawRoundedRectangle(rf, radius, 1.0f);
        return;
    }

    // ── グリッド ──────────────────────────────────────────────────────────────
    const auto waveArea = bounds.reduced(8, 8);
    g.setColour(ColorPalette::waveformGrid().withAlpha(0.42f));
    for (int i = 1; i < 8; ++i)
        g.drawVerticalLine(i * w / 8, (float)waveArea.getY(), (float)waveArea.getBottom());
    g.setColour(ColorPalette::borderSoft().withAlpha(0.86f));
    g.drawHorizontalLine((int)midY, (float)waveArea.getX(), (float)waveArea.getRight());

    // ── 位置計算 ──────────────────────────────────────────────────────────────
    const int   dataLen   = (int)waveMin.size();
    const int   startX    = (int)positionToX(startPos);
    const int   endX      = (int)positionToX(endPos);
    const float rangePx   = (float)(endX - startX);
    const int   fadeInX   = startX + (int)(rangePx * fadeIn);
    const int   fadeOutX  = endX   - (int)(rangePx * fadeOut);

    auto getSrcX = [&](int x) -> int {
        return reversed
            ? (dataLen - 1 - (int)((float)x / (float)w * dataLen))
            : (int)((float)x / (float)w * dataLen);
    };

    // ── 全体波形（暗め） ──────────────────────────────────────────────────────
    g.setColour(ColorPalette::waveformLine().withAlpha(0.22f));
    for (int x = 0; x < w; ++x)
    {
        const int srcX = getSrcX(x);
        if (srcX < 0 || srcX >= dataLen) continue;
        const auto sx = (size_t)srcX;
        g.drawVerticalLine(x,
            midY - waveMax[sx] * (midY - 18.0f),
            midY - waveMin[sx] * (midY - 18.0f));
    }

    // ── アクティブ範囲外をダーク ──────────────────────────────────────────────
    g.setColour(ColorPalette::backgroundDeep().withAlpha(0.70f));
    if (startX > 0)  g.fillRect(0,     0, startX,     h);
    if (endX   < w)  g.fillRect(endX,  0, w - endX,   h);

    // ── アクティブ範囲: グロー ────────────────────────────────────────────────
    g.setColour(ColorPalette::iceGlow().withAlpha(0.14f));
    for (int x = startX; x <= endX; ++x)
    {
        const int srcX = getSrcX(x);
        if (srcX < 0 || srcX >= dataLen) continue;
        const auto sx = (size_t)srcX;
        g.drawVerticalLine(x,
            midY - waveMax[sx] * (midY - 16.0f) - 2.0f,
            midY - waveMin[sx] * (midY - 16.0f) + 2.0f);
    }

    // ── アクティブ範囲: 波形本体 ──────────────────────────────────────────────
    g.setColour(ColorPalette::waveformLine().withAlpha(0.95f));
    for (int x = startX; x <= endX; ++x)
    {
        const int srcX = getSrcX(x);
        if (srcX < 0 || srcX >= dataLen) continue;
        const auto sx = (size_t)srcX;
        g.drawVerticalLine(x,
            midY - waveMax[sx] * (midY - 18.0f),
            midY - waveMin[sx] * (midY - 18.0f));
    }

    // ═════════════════════════════════════════════════════════════════════════
    //  Fade In  ── 1本の斜め線のみ（左下 → 右上）
    //
    //  シェード三角形・fillPath・fillRect は使わない。
    //  音量変化を示す 1 本の Gold 斜め線 + ごく軽いグローのみ。
    // ═════════════════════════════════════════════════════════════════════════
    if (fadeIn > 0.001f && fadeInX > startX)
    {
        juce::Path line;
        line.startNewSubPath((float)startX,  (float)(h - 8));
        line.lineTo         ((float)fadeInX, 8.0f);

        // 軽いグロー（広い線、低 α）
        g.setColour(ColorPalette::champagne().withAlpha(0.22f));
        g.strokePath(line, juce::PathStrokeType(3.5f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // 主役の細い線
        g.setColour(ColorPalette::champagne().withAlpha(0.95f));
        g.strokePath(line, juce::PathStrokeType(1.4f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // ═════════════════════════════════════════════════════════════════════════
    //  Fade Out  ── 1本の斜め線のみ（左上 → 右下、Fade In と接続しない）
    // ═════════════════════════════════════════════════════════════════════════
    if (fadeOut > 0.001f && fadeOutX < endX)
    {
        juce::Path line;
        line.startNewSubPath((float)fadeOutX, 8.0f);
        line.lineTo         ((float)endX,     (float)(h - 8));

        g.setColour(ColorPalette::champagne().withAlpha(0.22f));
        g.strokePath(line, juce::PathStrokeType(3.5f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        g.setColour(ColorPalette::champagne().withAlpha(0.95f));
        g.strokePath(line, juce::PathStrokeType(1.4f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // ── プレイヘッド ──────────────────────────────────────────────────────────
    if (playheadActive)
    {
        const int phX = juce::jlimit(startX, endX, (int)positionToX(playheadPos));
        g.setColour(ColorPalette::iceGlow().withAlpha(0.18f));
        g.fillRect(phX - 1, 10, 3, h - 20);
        g.setColour(ColorPalette::iceBlueLight().withAlpha(0.96f));
        g.drawVerticalLine(phX, 9.0f, (float)(h - 9));
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  Fade In ハンドル
    //
    //   - カーブ終端（fadeInX, y=10）に小さなゴールド円
    //   - グラブ目印のための薄い縦点線
    //   - ダイヤモンド / 三角ポリゴンは使わない
    //   - ラベル（ドラッグ時のみ）はハンドルの右側に配置
    // ─────────────────────────────────────────────────────────────────────────
    {
        const bool drag = (activeDrag == DragHandle::FadeIn);
        const juce::Colour c = ColorPalette::champagne();

        // 縦点線（fadeIn > 0 のときだけ表示）
        if (fadeIn > 0.001f)
        {
            g.setColour(c.withAlpha(drag ? 0.45f : 0.22f));
            for (int y = 18; y < h - 10; y += 5)
                g.fillRect(fadeInX - 1, y, 2, 2);
        }

        // 小さなゴールドの円ハンドル（カーブ終端の上に乗る）
        {
            const float cx = (float)fadeInX;
            const float cy = 10.0f;
            const float r  = drag ? 5.0f : 4.2f;
            g.setColour(c.withAlpha(drag ? 1.0f : 0.92f));
            g.fillEllipse(cx - r, cy - r, r * 2.0f, r * 2.0f);
            g.setColour(c.brighter(0.35f));
            g.drawEllipse(cx - r, cy - r, r * 2.0f, r * 2.0f, 0.8f);
        }

        // ラベル: ハンドルの右側に配置（波形エリア内にクランプ）
        if (drag)
        {
            constexpr int labelW = 74;
            int wantCx = fadeInX + 8 + labelW / 2;
            wantCx = juce::jlimit(labelW / 2 + 2, w - labelW / 2 - 2, wantCx);
            drawHandleLabel(g, wantCx, h - 60, "FADE IN", fadePercentText(fadeIn), c);
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  Fade Out ハンドル
    //
    //   - カーブ始端（fadeOutX, y=10）に小さなゴールド円
    //   - ラベルはハンドルの「左側」に配置（右端見切れ防止）
    // ─────────────────────────────────────────────────────────────────────────
    {
        const bool drag = (activeDrag == DragHandle::FadeOut);
        const juce::Colour c = ColorPalette::champagne();

        if (fadeOut > 0.001f)
        {
            g.setColour(c.withAlpha(drag ? 0.45f : 0.22f));
            for (int y = 18; y < h - 10; y += 5)
                g.fillRect(fadeOutX - 1, y, 2, 2);
        }

        {
            const float cx = (float)fadeOutX;
            const float cy = 10.0f;
            const float r  = drag ? 5.0f : 4.2f;
            g.setColour(c.withAlpha(drag ? 1.0f : 0.92f));
            g.fillEllipse(cx - r, cy - r, r * 2.0f, r * 2.0f);
            g.setColour(c.brighter(0.35f));
            g.drawEllipse(cx - r, cy - r, r * 2.0f, r * 2.0f, 0.8f);
        }

        // ラベル: ハンドルの「左側」に配置（要件: 右端見切れ防止）
        if (drag)
        {
            constexpr int labelW = 74;
            int wantCx = fadeOutX - 8 - labelW / 2;
            wantCx = juce::jlimit(labelW / 2 + 2, w - labelW / 2 - 2, wantCx);
            drawHandleLabel(g, wantCx, h - 60, "FADE OUT", fadePercentText(fadeOut), c);
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  Start ハンドル  ── トップタブ、Ice Blue
    // ─────────────────────────────────────────────────────────────────────────
    {
        const juce::Colour c = ColorPalette::iceBlue();

        g.setColour(c.withAlpha(0.18f));
        g.fillRect(startX - 1, 18, 3, h - 30);
        g.setColour(c);
        g.drawVerticalLine(startX, 6.0f, (float)(h - 6));

        juce::Path tab;
        tab.addRoundedRectangle((float)startX - 5.0f, 5.0f, 10.0f, 10.0f, 2.0f);
        g.fillPath(tab);

        const int lx = juce::jlimit(37, w - 37, startX);
        drawHandleLabel(g, lx, 19, "START", percentText(startPos), c);
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  End ハンドル  ── トップタブ、Ice Blue Light
    // ─────────────────────────────────────────────────────────────────────────
    {
        const juce::Colour c = ColorPalette::iceBlueLight();

        g.setColour(c.withAlpha(0.16f));
        g.fillRect(endX - 1, 18, 3, h - 30);
        g.setColour(c);
        g.drawVerticalLine(endX, 6.0f, (float)(h - 6));

        juce::Path tab;
        tab.addRoundedRectangle((float)endX - 5.0f, 5.0f, 10.0f, 10.0f, 2.0f);
        g.fillPath(tab);

        const int lx = juce::jlimit(37, w - 37, endX);
        drawHandleLabel(g, lx, 19, "END", percentText(endPos), c);
    }

    // ── 枠線 ─────────────────────────────────────────────────────────────────
    g.setColour(ColorPalette::borderStrong().withAlpha(0.92f));
    g.drawRoundedRectangle(rf, radius, 1.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// リサイズ
// ─────────────────────────────────────────────────────────────────────────────
void WaveformDisplay::resized()
{
    const int newW = getWidth();
    if (newW != thumbnailWidth && newW > 0 && !waveMin.empty() && thumbnailWidth > 0)
    {
        std::vector<float> newMin((size_t)newW), newMax((size_t)newW);
        for (int x = 0; x < newW; ++x)
        {
            const int s = juce::jlimit(0, thumbnailWidth - 1,
                              (int)((float)x / (float)newW * (float)thumbnailWidth));
            newMin[(size_t)x] = waveMin[(size_t)s];
            newMax[(size_t)x] = waveMax[(size_t)s];
        }
        waveMin = std::move(newMin);
        waveMax = std::move(newMax);
        thumbnailWidth = newW;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// マウス操作
// ─────────────────────────────────────────────────────────────────────────────
void WaveformDisplay::mouseDown(const juce::MouseEvent& e)
{
    activeDrag          = detectHandle(e.x, e.y);
    dragAnchorPosX      = (float)e.x;
    dragAnchorStartPos  = startPos;
    dragAnchorEndPos    = endPos;
    dragAnchorFadeIn    = fadeIn;
    dragAnchorFadeOut   = fadeOut;

    if (activeDrag != DragHandle::None)
        repaint();
}

void WaveformDisplay::mouseDrag(const juce::MouseEvent& e)
{
    if (activeDrag == DragHandle::None) return;

    const float width      = juce::jmax(1.0f, (float)getWidth());
    const float newPos     = juce::jlimit(0.0f, 1.0f, xToPosition((float)e.x));
    const bool  fineAdjust = e.mods.isAltDown();
    // Alt: 0.20x スケールで精密調整
    const float deltaPosNorm = ((float)e.x - dragAnchorPosX) / width * 0.20f;

    switch (activeDrag)
    {
        case DragHandle::Start:
        {
            const float cand = fineAdjust ? (dragAnchorStartPos + deltaPosNorm) : newPos;
            startPos = std::min(juce::jlimit(0.0f, 1.0f, cand), endPos - 0.001f);
            if (onStartChanged) onStartChanged(startPos);
            break;
        }
        case DragHandle::End:
        {
            const float cand = fineAdjust ? (dragAnchorEndPos + deltaPosNorm) : newPos;
            endPos = std::max(juce::jlimit(0.0f, 1.0f, cand), startPos + 0.001f);
            if (onEndChanged) onEndChanged(endPos);
            break;
        }
        case DragHandle::FadeIn:
        {
            // fadeIn = (newPos - startPos) / range — ドラッグの直接マッピング
            const float range = juce::jmax(0.01f, endPos - startPos);
            const float fadeDeltaNorm = deltaPosNorm / range;
            const float cand = fineAdjust
                ? (dragAnchorFadeIn + fadeDeltaNorm)
                : ((newPos - startPos) / range);

            // 重なり防止: ドラッグ時のみ Fade Out とぶつからないように上限を制限。
            // fadeOut の値そのものは変更しない（独立性は保つ）。
            const float maxAllowed = juce::jmax(0.0f, 1.0f - fadeOut);
            fadeIn = juce::jlimit(0.0f, maxAllowed, cand);
            if (onFadeInChanged) onFadeInChanged(fadeIn);
            break;
        }
        case DragHandle::FadeOut:
        {
            const float range = juce::jmax(0.01f, endPos - startPos);
            const float fadeDeltaNorm = deltaPosNorm / range;
            const float cand = fineAdjust
                ? (dragAnchorFadeOut - fadeDeltaNorm)
                : ((endPos - newPos) / range);

            // 重なり防止: 同様に fadeIn を変更せず、こちらだけ上限を制限。
            const float maxAllowed = juce::jmax(0.0f, 1.0f - fadeIn);
            fadeOut = juce::jlimit(0.0f, maxAllowed, cand);
            if (onFadeOutChanged) onFadeOutChanged(fadeOut);
            break;
        }
        case DragHandle::None:
        default:
            break;
    }

    repaint();
}

void WaveformDisplay::mouseUp(const juce::MouseEvent&)
{
    activeDrag     = DragHandle::None;
    dragAnchorPosX = 0.0f;
    setMouseCursor(juce::MouseCursor::NormalCursor);
    repaint();
}

void WaveformDisplay::mouseMove(const juce::MouseEvent& e)
{
    const auto h = detectHandle(e.x, e.y);
    setMouseCursor(h != DragHandle::None
        ? juce::MouseCursor::LeftRightResizeCursor
        : juce::MouseCursor::NormalCursor);
}

// ─────────────────────────────────────────────────────────────────────────────
// サムネイル生成
// ─────────────────────────────────────────────────────────────────────────────
void WaveformDisplay::buildThumbnail(const juce::AudioBuffer<float>* buf, int width)
{
    thumbnailWidth = width;
    waveMin.assign((size_t)width, 0.0f);
    waveMax.assign((size_t)width, 0.0f);
    hasSampleData = false;

    if (buf == nullptr || buf->getNumSamples() == 0 || width <= 0) return;

    hasSampleData = true;
    const int total  = buf->getNumSamples();
    const int numCh  = buf->getNumChannels();

    for (int x = 0; x < width; ++x)
    {
        const int sStart = (int)((double)x / width * total);
        const int sEnd   = (int)((double)(x + 1) / width * total);

        float mn = 0.0f, mx = 0.0f;
        for (int s = sStart; s < sEnd && s < total; ++s)
            for (int ch = 0; ch < numCh; ++ch)
            {
                const float v = buf->getSample(ch, s);
                mn = std::min(mn, v);
                mx = std::max(mx, v);
            }

        if (sStart == sEnd && sStart < total)
            for (int ch = 0; ch < numCh; ++ch)
            {
                const float v = buf->getSample(ch, sStart);
                mn = std::min(mn, v);
                mx = std::max(mx, v);
            }

        waveMin[(size_t)x] = mn;
        waveMax[(size_t)x] = mx;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// ヒットテスト
//
//   ゴールドの円ハンドル（Fade）は y=10 付近にある。
//   Ice Blue のタブ（Start/End）も y=5..15 付近にある。
//
//   優先順位:
//     - トップゾーン (y < h×40%): Fade 円ハンドル領域 (y < 22) なら Fade 優先、
//                                 それ以外は Start/End 優先
//     - ボトムゾーン (y ≥ h×40%): Start/End 優先（縦線でグラブできる）、
//                                 Fade はサブ
//   これで、ゴールドの円をクリック → Fade、それ以外の縦線をクリック → Start/End
// ─────────────────────────────────────────────────────────────────────────────
WaveformDisplay::DragHandle WaveformDisplay::detectHandle(int x, int y) const noexcept
{
    const int startX   = (int)positionToX(startPos);
    const int endX     = (int)positionToX(endPos);
    const float rPx    = (float)(endX - startX);
    const int fadeInX  = startX + (int)(rPx * fadeIn);
    const int fadeOutX = endX   - (int)(rPx * fadeOut);

    // Fade 円ハンドルの周辺 (y < 22): Fade を優先
    if (y < 22)
    {
        if (std::abs(x - fadeInX)  <= kHandleGrabRadius && fadeIn  > 0.001f) return DragHandle::FadeIn;
        if (std::abs(x - fadeOutX) <= kHandleGrabRadius && fadeOut > 0.001f) return DragHandle::FadeOut;
        if (std::abs(x - startX)   <= kHandleGrabRadius) return DragHandle::Start;
        if (std::abs(x - endX)     <= kHandleGrabRadius) return DragHandle::End;
        // fade=0 のときの初期ドラッグ: Start/End と被るので Start/End を優先
        if (std::abs(x - fadeInX)  <= kHandleGrabRadius) return DragHandle::FadeIn;
        if (std::abs(x - fadeOutX) <= kHandleGrabRadius) return DragHandle::FadeOut;
    }
    else
    {
        // 通常ゾーン: 縦線（Start/End）優先
        if (std::abs(x - startX)   <= kHandleGrabRadius) return DragHandle::Start;
        if (std::abs(x - endX)     <= kHandleGrabRadius) return DragHandle::End;
        if (std::abs(x - fadeInX)  <= kHandleGrabRadius && fadeIn  > 0.001f) return DragHandle::FadeIn;
        if (std::abs(x - fadeOutX) <= kHandleGrabRadius && fadeOut > 0.001f) return DragHandle::FadeOut;
    }

    return DragHandle::None;
}

float WaveformDisplay::xToPosition(float x) const noexcept
{
    const float wf = (float)getWidth();
    return (wf > 0.0f) ? (x / wf) : 0.0f;
}

float WaveformDisplay::positionToX(float pos) const noexcept
{
    return pos * (float)getWidth();
}
