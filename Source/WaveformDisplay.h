#pragma once
#include <JuceHeader.h>
#include <vector>
#include <functional>

// ─────────────────────────────────────────────────────────────────────────────
// WaveformDisplay  ─  サンプルの波形を表示し、Start/End 位置をドラッグ編集できる
//
// 機能:
//   - 波形サムネイル（ピクセル単位の min/max 表示）
//   - Start ハンドル（Ice Blue の縦線 + 三角ハンドル）をドラッグして開始位置を変更
//   - End ハンドル（明るい Ice Blue）をドラッグして終了位置を変更
//   - Fade In / Fade Out 領域をグラデーションオーバーレイで表示
//   - Reverse フラグが立つと波形を左右反転表示
//
// 使い方:
//   1. setSample(buffer) でサンプルを渡すと波形を生成
//   2. setXxx() で各パラメータを反映（repaint を自動呼び出し）
//   3. onStartChanged / onEndChanged コールバックを設定して変更を受け取る
// ─────────────────────────────────────────────────────────────────────────────
class WaveformDisplay : public juce::Component
{
public:
    WaveformDisplay();

    // ── サンプルを設定（読み取りロック下で呼ぶこと） ─────────────────────
    // buf が nullptr の場合は「サンプルなし」表示になる
    void setSample(const juce::AudioBuffer<float>* buf);

    // ── パラメータの反映（変更のたびに呼ぶ） ────────────────────────────
    void setStartPosition(float v);   // 0.0〜1.0
    void setEndPosition(float v);     // 0.0〜1.0
    void setFadeIn(float v);          // 0.0〜1.0 （再生範囲に対する割合）
    void setFadeOut(float v);
    void setReversed(bool r);
    void setPlayhead(float v, bool active);

    // ── コールバック（ユーザーがドラッグで変更したとき） ─────────────────
    std::function<void(float)> onStartChanged;
    std::function<void(float)> onEndChanged;
    std::function<void(float)> onFadeInChanged;
    std::function<void(float)> onFadeOutChanged;

    // ── Component ─────────────────────────────────────────────────────────
    void paint(juce::Graphics& g) override;
    void resized()               override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp  (const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;   // カーソル変更用

private:
    // ── 波形サムネイルデータ（ピクセル単位の min/max） ─────────────────
    std::vector<float> waveMin, waveMax;
    int  thumbnailWidth { 0 };
    bool hasSampleData  { false };

    // ── 表示パラメータ ────────────────────────────────────────────────────
    float startPos { 0.0f };
    float endPos   { 1.0f };
    float fadeIn   { 0.0f };
    float fadeOut  { 0.0f };
    bool  reversed { false };
    float playheadPos { 0.0f };
    bool  playheadActive { false };

    // ── ドラッグ状態 ──────────────────────────────────────────────────────
    enum class DragHandle { None, Start, End, FadeIn, FadeOut };
    DragHandle activeDrag { DragHandle::None };
    float dragAnchorPosX { 0.0f };
    float dragAnchorStartPos { 0.0f };
    float dragAnchorEndPos { 1.0f };
    float dragAnchorFadeIn { 0.0f };
    float dragAnchorFadeOut { 0.0f };

    // ── ヘルパー ──────────────────────────────────────────────────────────
    void buildThumbnail(const juce::AudioBuffer<float>* buf, int width);
    DragHandle detectHandle(int x, int y) const noexcept;  // hitTest は JUCE 仮想関数名と衝突するため避ける
    float xToPosition(float x) const noexcept;
    float positionToX(float pos) const noexcept;

    // ハンドルの当たり判定半径（ピクセル）
    static constexpr int kHandleGrabRadius = 7;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaveformDisplay)
};
