#include "SmartTrim.h"
#include <cmath>
#include <vector>

namespace SmartTrim
{

// ─────────────────────────────────────────────────────────────────────────────
// 内部ヘルパー: 短いウィンドウごとの RMS を計算
// ─────────────────────────────────────────────────────────────────────────────
static std::vector<float> computeWindowRMS(const juce::AudioBuffer<float>& buf,
                                            int                              windowSize)
{
    const int total   = buf.getNumSamples();
    const int numCh   = buf.getNumChannels();
    const int numWin  = total / windowSize;

    std::vector<float> out;
    if (numWin <= 0 || numCh <= 0) return out;

    out.resize(static_cast<size_t>(numWin), 0.0f);

    for (int w = 0; w < numWin; ++w)
    {
        const int start = w * windowSize;
        float sumSq = 0.0f;

        for (int ch = 0; ch < numCh; ++ch)
        {
            const float* data = buf.getReadPointer(ch);
            for (int i = 0; i < windowSize; ++i)
            {
                const float s = data[start + i];
                sumSq += s * s;
            }
        }

        const float meanSq = sumSq / static_cast<float>(windowSize * numCh);
        out[static_cast<size_t>(w)] = std::sqrt(meanSq);
    }

    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// メイン解析関数
// ─────────────────────────────────────────────────────────────────────────────
Result analyze(const juce::AudioBuffer<float>& buffer, double sampleRate) noexcept
{
    Result r;  // デフォルト: 0.0, 1.0

    const int total = buffer.getNumSamples();
    if (total <= 0 || sampleRate <= 0.0) return r;

    // ── ウィンドウサイズ: 5ms。極端に短いサンプルは解析しない ──────────
    const int windowSize = juce::jmax(1, static_cast<int>(sampleRate * 0.005));
    if (total < windowSize * 4)
    {
        // 短すぎるサンプルはそのまま全範囲を使う
        return r;
    }

    const auto rms = computeWindowRMS(buffer, windowSize);
    const int  numWin = static_cast<int>(rms.size());
    if (numWin < 2) return r;

    // ── しきい値 ──────────────────────────────────────────────────────────
    const float silenceThresh  = juce::Decibels::decibelsToGain(-50.0f);
    const float transientFloor = juce::Decibels::decibelsToGain(-40.0f);

    // ── 先頭の無音終端を探す ──────────────────────────────────────────────
    int firstActive = 0;
    while (firstActive < numWin
           && rms[static_cast<size_t>(firstActive)] < silenceThresh)
        ++firstActive;

    if (firstActive >= numWin)
    {
        // 完全無音 → そのまま返す（変更なし）
        return r;
    }

    // ── トランジェント検出（先頭の有音から 100ms 以内を走査） ────────────
    const int searchWindows = juce::jmin(
        numWin - firstActive,
        static_cast<int>(0.100 * sampleRate / windowSize) + 1);

    int   transientWin = firstActive;
    float bestRatio    = 1.0f;
    const float floorRMS = silenceThresh * 0.5f;  // ゼロ除算回避

    for (int w = firstActive; w < firstActive + searchWindows && w < numWin; ++w)
    {
        if (w == 0) continue;

        const float cur  = rms[static_cast<size_t>(w)];
        const float prev = juce::jmax(rms[static_cast<size_t>(w - 1)], floorRMS);
        const float ratio = cur / prev;

        // エネルギーが急増 (4倍以上) かつ十分大きい値 → トランジェント
        if (ratio > 4.0f && cur > transientFloor && ratio > bestRatio)
        {
            transientWin = w;
            bestRatio    = ratio;
            break;  // 最初に見つけた強いトランジェントを採用
        }
    }

    // ── スタート位置: トランジェント直前から 3ms 戻す（アタック保持） ───
    const int backoffSamples = static_cast<int>(0.003 * sampleRate);
    int startSamp = transientWin * windowSize - backoffSamples;
    startSamp = juce::jlimit(0, total - 1, startSamp);

    // ── 末尾の無音検出（後方から走査） ────────────────────────────────
    int lastActive = numWin - 1;
    while (lastActive > firstActive
           && rms[static_cast<size_t>(lastActive)] < silenceThresh)
        --lastActive;

    // 末尾余韻 50ms を残してエンド位置を決定
    const int tailSamples = static_cast<int>(0.050 * sampleRate);
    int endSamp = (lastActive + 1) * windowSize + tailSamples;
    endSamp = juce::jlimit(startSamp + 1, total, endSamp);

    // ── 結果を正規化 ────────────────────────────────────────────────────
    r.startPosition = static_cast<float>(startSamp) / static_cast<float>(total);
    r.endPosition   = static_cast<float>(endSamp)   / static_cast<float>(total);

    // 範囲が極端に狭くなった場合の保険
    if (r.endPosition - r.startPosition < 0.01f)
    {
        // 10ms 未満しか残らないようなら自動調整を放棄
        r.startPosition = 0.0f;
        r.endPosition   = 1.0f;
    }

    return r;
}

// ─────────────────────────────────────────────────────────────────────────────
// 旧 API: 新しい analyze() を呼んで startPosition のみ返す
// ─────────────────────────────────────────────────────────────────────────────
float detectStartPosition(const juce::AudioBuffer<float>& buffer,
                          float /*thresholdDB*/,
                          int   /*lookbackSamples*/) noexcept
{
    // サンプルレートは外から渡されないので 44100 を仮定（互換のため）
    // 新規コードは analyze(buffer, sampleRate) を直接使うこと。
    const auto r = analyze(buffer, 44100.0);
    return r.startPosition;
}

} // namespace SmartTrim
