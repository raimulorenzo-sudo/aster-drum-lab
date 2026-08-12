#pragma once
#include <JuceHeader.h>
#include "PadData.h"
#include "KeepLengthEngine.h"

struct BiquadCoefficients
{
    float b0 { 1.0f };
    float b1 { 0.0f };
    float b2 { 0.0f };
    float a1 { 0.0f };
    float a2 { 0.0f };
};

struct BiquadState
{
    float z1 { 0.0f };
    float z2 { 0.0f };

    float process(float x, const BiquadCoefficients& c) noexcept
    {
        const float y = c.b0 * x + z1;
        z1 = c.b1 * x - c.a1 * y + z2;
        z2 = c.b2 * x - c.a2 * y;
        return y;
    }

    void reset() noexcept
    {
        z1 = 0.0f;
        z2 = 0.0f;
    }
};

struct VoiceEqState
{
    std::array<BiquadState, 4> left {};
    std::array<BiquadState, 4> right {};

    void reset() noexcept
    {
        for (auto& s : left)  s.reset();
        for (auto& s : right) s.reset();
    }
};

static constexpr size_t MAX_LAYER_FX_SLOTS = 16;
// 1 スロットの Filter が持てる biquad 段数の上限 (HP 最大4段 + LP 最大4段 = 8)。
static constexpr size_t MAX_FILTER_STAGES = 8;

struct VoiceFxState
{
    std::array<VoiceEqState, MAX_LAYER_FX_SLOTS> eq {};
    // Filter: スロットごとに最大 MAX_FILTER_STAGES 段のカスケード biquad。
    std::array<std::array<BiquadState, MAX_FILTER_STAGES>, MAX_LAYER_FX_SLOTS> filterLeft {};
    std::array<std::array<BiquadState, MAX_FILTER_STAGES>, MAX_LAYER_FX_SLOTS> filterRight {};
    std::array<float, MAX_LAYER_FX_SLOTS> driveToneLeft {};
    std::array<float, MAX_LAYER_FX_SLOTS> driveToneRight {};
    std::array<float, MAX_LAYER_FX_SLOTS> downsampleHoldLeft {};
    std::array<float, MAX_LAYER_FX_SLOTS> downsampleHoldRight {};
    std::array<int, MAX_LAYER_FX_SLOTS> downsampleCounterLeft {};
    std::array<int, MAX_LAYER_FX_SLOTS> downsampleCounterRight {};
    std::array<float, MAX_LAYER_FX_SLOTS> transientFastLeft {};
    std::array<float, MAX_LAYER_FX_SLOTS> transientFastRight {};
    std::array<float, MAX_LAYER_FX_SLOTS> transientSlowLeft {};
    std::array<float, MAX_LAYER_FX_SLOTS> transientSlowRight {};
    std::array<float, MAX_LAYER_FX_SLOTS> compressorEnvLeft {};
    std::array<float, MAX_LAYER_FX_SLOTS> compressorEnvRight {};
    std::array<float, MAX_LAYER_FX_SLOTS> compressorMakeupLeft {};
    std::array<float, MAX_LAYER_FX_SLOTS> compressorMakeupRight {};
    std::array<float, MAX_LAYER_FX_SLOTS> compressorOutputLeft {};
    std::array<float, MAX_LAYER_FX_SLOTS> compressorOutputRight {};
    std::array<bool, MAX_LAYER_FX_SLOTS> compressorGainInitialisedLeft {};
    std::array<bool, MAX_LAYER_FX_SLOTS> compressorGainInitialisedRight {};
    // このブロックで各 Compressor スロットが適用したゲインリダクション量 (dB, 正値)。
    // render() 開始時に 0 リセットし、処理中に max を記録 → VoiceManager が集計。
    std::array<float, MAX_LAYER_FX_SLOTS> compReductionDb {};

    void reset() noexcept
    {
        for (auto& s : eq) s.reset();
        for (auto& slot : filterLeft)  for (auto& s : slot) s.reset();
        for (auto& slot : filterRight) for (auto& s : slot) s.reset();
        driveToneLeft.fill(0.0f);
        driveToneRight.fill(0.0f);
        downsampleHoldLeft.fill(0.0f);
        downsampleHoldRight.fill(0.0f);
        downsampleCounterLeft.fill(0);
        downsampleCounterRight.fill(0);
        transientFastLeft.fill(0.0f);
        transientFastRight.fill(0.0f);
        transientSlowLeft.fill(0.0f);
        transientSlowRight.fill(0.0f);
        compressorEnvLeft.fill(0.0f);
        compressorEnvRight.fill(0.0f);
        compressorMakeupLeft.fill(1.0f);
        compressorMakeupRight.fill(1.0f);
        compressorOutputLeft.fill(1.0f);
        compressorOutputRight.fill(1.0f);
        compressorGainInitialisedLeft.fill(false);
        compressorGainInitialisedRight.fill(false);
        compReductionDb.fill(0.0f);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// DrumVoice  ─  1 つの「発音中の音」を表す構造体
//
// VoiceManager が複数の DrumVoice を管理し、
// MIDI Note On ごとに 1 つの Voice を起動します。
//
// 処理フロー:
//   1. noteOn → VoiceManager::noteOn() → DrumVoice::start()
//   2. processBlock ごとに DrumVoice::render() を呼ぶ
//   3. 終端に達したか envelope が 0 になると isActive = false になる
//   4. Gate モード時は noteOff → triggerRelease() でフェードアウト
// ─────────────────────────────────────────────────────────────────────────────
struct DrumVoice
{
    int   padIndex     { -1    };    // どのパッドか
    int   layerIndex   { 0     };    // どの Layer か（0..MAX_LAYERS_PER_PAD-1）
    bool  isActive     { false };    // 発音中かどうか
    bool  isPreview    { false };    // UIクリックによるプレビュー発音かどうか
    uint64_t triggerSerial { 0 };    // 同一 Pad の最新 Voice を選ぶための発音順

    // ── 再生位置 ──────────────────────────────────────────────────────────
    double samplePos   { 0.0   };    // 現在の再生位置（小数含む）
    double startSample { 0.0   };    // 再生開始位置（リバース時の終端チェックに使用）
    double endSample   { 0.0   };    // 再生終了位置
    double sourceLength { 1.0  };    // 波形上の現在位置算出用

    // ── ゲイン（パンと音量を事前計算して保持） ───────────────────────────
    float gainL        { 1.0f  };
    float gainR        { 1.0f  };
    bool  swapLR       { false };

    // ── 再生モード ────────────────────────────────────────────────────────
    bool  isOneShot    { true  };    // true = Note Off を無視

    // ── エンベロープ ──────────────────────────────────────────────────────
    float envelope     { 0.0f  };    // 現在のエンベロープ値（0.0〜1.0）
    float attackRate   { 0.0f  };    // 1 サンプルあたりの増加量
    float releaseRate  { 0.0f  };    // 1 サンプルあたりの減少量
    bool  isReleasing  { false };

    // ── Phase 2: ピッチ / リバース / フェード ────────────────────────────
    double playbackRatio { 1.0  };   // 再生速度比（2^(semitones/12)）ピッチシフト
    bool   reversed      { false };  // true = 逆再生
    bool   keepLengthEnabled { false };
    float  keepLengthWetMix { 0.0f };
    float  humanizePitchOffset { 0.0f };
    int    fadeInSamples { 0    };   // フェードイン長（出力サンプル数）
    int    fadeOutSamples{ 0    };   // フェードアウト長（出力サンプル数）
    int    samplesRendered{ 0   };   // レンダリング済みサンプル数（フェード計算用）
    int    startDelaySamples{ 0 };   // Humanize 用の発音遅延（出力サンプル数）
    VoiceEqState eqState {};
    VoiceFxState fxState {};

    // ── Phase 3: レベル計測 ───────────────────────────────────────────────
    // render() が呼ばれるたびに更新される。VoiceManager が集計してミキサーメーターに渡す。
    float lastPeakLevel  { 0.0f };   // 直前ブロックのピーク出力振幅（線形、0〜1+）
    KeepLengthEngine keepLengthEngine;

    void prepare(double hostSampleRate, int maximumBlockSize);

    // ── 発音開始 ──────────────────────────────────────────────────────────
    // Phase 2 で追加した引数にはデフォルト値を付けているので、
    // 既存の呼び出しコードはそのままコンパイルできます。
    void start(int    padIdx,
               double startSamp,
               double endSamp,
               float  gainLeft,
               float  gainRight,
               bool   oneShot,
               float  attackTimeSec,
               float  releaseTimeSec,
               double hostSampleRate,
               double pRatio      = 1.0,    // ピッチシフト比
               bool   rev         = false,  // 逆再生
               int    fadeInSamp  = 0,      // フェードインサンプル数
               int    fadeOutSamp = 0,      // フェードアウトサンプル数
               double sourceLen   = 1.0,    // 元サンプル長
               uint64_t serial    = 0,      // 発音順
               int    startDelaySamp = 0,   // Humanize タイミング揺れ
               bool   previewVoice = false, // UI preview専用voice
               bool   swapChannels = false, // Pad output L/R swap
               bool   keepLength = false,
               double sourceRateRatio = 1.0,
               float  initialPitchSemitones = 0.0f,
               float  perVoicePitchOffset = 0.0f
               ) noexcept;

    // ── リリース開始（Gate モード専用） ────────────────────────────────────
    void triggerRelease() noexcept;
    void forceRelease(float releaseTimeSec, double hostSampleRate) noexcept;

    // ── レンダリング（processBlock から呼ぶ） ──────────────────────────────
    // source: このパッドのオーディオバッファ
    // output: 出力先（加算して書き込む）
    // 戻り値: 発音継続中なら true、終了したら false
    bool render(const juce::AudioBuffer<float>& source,
                juce::AudioBuffer<float>&       output,
                int                             outputStartSample,
                int                             numSamples,
                const LayerData&                layer,
                double                          hostSampleRate,
                float                           pitchSemitones) noexcept;

    float getPlaybackPositionNormalized() const noexcept;
};
