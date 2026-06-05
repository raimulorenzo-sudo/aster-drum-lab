#pragma once
#include <JuceHeader.h>

// ─────────────────────────────────────────────────────────────────────────────
// SmartTrim  ─  サンプル読み込み時に「自然な再生範囲」を自動検出する
//
//   方針:
//     - 5ms 単位の RMS ウィンドウで先頭の無音を検出
//     - 続く 100ms 以内にエネルギー急増（4倍以上）があればそこをトランジェントと判定
//     - スタート位置はトランジェント直前から 3ms 戻した位置（アタック保持）
//     - エンド位置は末尾の無音を切り、ナチュラルな余韻 50ms を残す
//
//   高度な処理（FFT スペクトラルフラックス等）は使わない。
//   キック・スネア・ハットなどの One Shot で自然に効くことを優先。
//   ループ／FX 素材で誤検出した場合はユーザーが手動修正できる前提。
//
// 使い方:
//   const auto* buf = fileManager.getBufferNoLock(padIndex);
//   if (buf != nullptr)
//   {
//       const auto r = SmartTrim::analyze(*buf, sampleRate);
//       pad.startPosition = r.startPosition;
//       pad.endPosition   = r.endPosition;
//   }
//
// スレッド: メッセージスレッドから呼ぶこと（AudioFileManager の読み取りロック下で）
// ─────────────────────────────────────────────────────────────────────────────
namespace SmartTrim
{
    struct Result
    {
        float startPosition { 0.0f };  // 0.0〜1.0
        float endPosition   { 1.0f };  // 0.0〜1.0
    };

    // ── メイン関数（推奨） ────────────────────────────────────────────────
    Result analyze(const juce::AudioBuffer<float>& buffer,
                   double                          sampleRate) noexcept;

    // ── 旧 API（互換用、内部で analyze を呼ぶ） ──────────────────────────
    // 既存呼び出し箇所が壊れないように残す。新規コードは analyze を使うこと。
    float detectStartPosition(const juce::AudioBuffer<float>& buffer,
                              float thresholdDB     = -50.0f,
                              int   lookbackSamples = 256) noexcept;
}
