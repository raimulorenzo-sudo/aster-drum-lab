#include "SamplerVoice.h"

namespace
{
    constexpr float kPi = juce::MathConstants<float>::pi;

    float clampEqFreq(float freq, double sampleRate) noexcept
    {
        const float nyquist = static_cast<float>(sampleRate * 0.5);
        return juce::jlimit(20.0f, juce::jmax(20.0f, nyquist - 20.0f), freq);
    }

    BiquadCoefficients normalise(float b0, float b1, float b2, float a0, float a1, float a2) noexcept
    {
        const float invA0 = (std::abs(a0) > 1.0e-9f) ? (1.0f / a0) : 1.0f;
        return { b0 * invA0, b1 * invA0, b2 * invA0, a1 * invA0, a2 * invA0 };
    }

    BiquadCoefficients makePeak(const LayerEqBand& band, double sampleRate) noexcept
    {
        const float freq = clampEqFreq(band.freq, sampleRate);
        const float q = juce::jlimit(0.2f, 8.0f, band.q);
        const float a = std::pow(10.0f, juce::jlimit(-18.0f, 18.0f, band.gain) / 40.0f);
        const float w0 = 2.0f * kPi * freq / static_cast<float>(sampleRate);
        const float alpha = std::sin(w0) / (2.0f * q);
        const float c = std::cos(w0);

        return normalise(1.0f + alpha * a,
                         -2.0f * c,
                         1.0f - alpha * a,
                         1.0f + alpha / a,
                         -2.0f * c,
                         1.0f - alpha / a);
    }

    BiquadCoefficients makeLowShelf(const LayerEqBand& band, double sampleRate) noexcept
    {
        const float freq = clampEqFreq(band.freq, sampleRate);
        const float q = juce::jlimit(0.2f, 8.0f, band.q);
        const float a = std::pow(10.0f, juce::jlimit(-18.0f, 18.0f, band.gain) / 40.0f);
        const float w0 = 2.0f * kPi * freq / static_cast<float>(sampleRate);
        const float c = std::cos(w0);
        const float s = std::sin(w0);
        const float alpha = s / (2.0f * q);
        const float beta = 2.0f * std::sqrt(a) * alpha;

        return normalise(a * ((a + 1.0f) - (a - 1.0f) * c + beta),
                         2.0f * a * ((a - 1.0f) - (a + 1.0f) * c),
                         a * ((a + 1.0f) - (a - 1.0f) * c - beta),
                         (a + 1.0f) + (a - 1.0f) * c + beta,
                         -2.0f * ((a - 1.0f) + (a + 1.0f) * c),
                         (a + 1.0f) + (a - 1.0f) * c - beta);
    }

    BiquadCoefficients makeHighShelf(const LayerEqBand& band, double sampleRate) noexcept
    {
        const float freq = clampEqFreq(band.freq, sampleRate);
        const float q = juce::jlimit(0.2f, 8.0f, band.q);
        const float a = std::pow(10.0f, juce::jlimit(-18.0f, 18.0f, band.gain) / 40.0f);
        const float w0 = 2.0f * kPi * freq / static_cast<float>(sampleRate);
        const float c = std::cos(w0);
        const float s = std::sin(w0);
        const float alpha = s / (2.0f * q);
        const float beta = 2.0f * std::sqrt(a) * alpha;

        return normalise(a * ((a + 1.0f) + (a - 1.0f) * c + beta),
                         -2.0f * a * ((a - 1.0f) + (a + 1.0f) * c),
                         a * ((a + 1.0f) + (a - 1.0f) * c - beta),
                         (a + 1.0f) - (a - 1.0f) * c + beta,
                         2.0f * ((a - 1.0f) - (a + 1.0f) * c),
                         (a + 1.0f) - (a - 1.0f) * c - beta);
    }

    BiquadCoefficients makeLowPass(float cutoffHz, float resonance, double sampleRate) noexcept
    {
        const float freq = clampEqFreq(cutoffHz, sampleRate);
        const float q = juce::jlimit(0.2f, 8.0f, resonance);
        const float w0 = 2.0f * kPi * freq / static_cast<float>(sampleRate);
        const float c = std::cos(w0);
        const float alpha = std::sin(w0) / (2.0f * q);

        return normalise((1.0f - c) * 0.5f,
                         1.0f - c,
                         (1.0f - c) * 0.5f,
                         1.0f + alpha,
                         -2.0f * c,
                         1.0f - alpha);
    }

    BiquadCoefficients makeHighPass(float cutoffHz, float resonance, double sampleRate) noexcept
    {
        const float freq = clampEqFreq(cutoffHz, sampleRate);
        const float q = juce::jlimit(0.2f, 8.0f, resonance);
        const float w0 = 2.0f * kPi * freq / static_cast<float>(sampleRate);
        const float c = std::cos(w0);
        const float alpha = std::sin(w0) / (2.0f * q);

        return normalise((1.0f + c) * 0.5f,
                         -(1.0f + c),
                         (1.0f + c) * 0.5f,
                         1.0f + alpha,
                         -2.0f * c,
                         1.0f - alpha);
    }

    // slope(dB/oct) → カスケード段数。12=1, 24=2, 48=4。
    int filterStagesForSlope(int slope) noexcept
    {
        if (slope >= 48) return 4;
        if (slope >= 24) return 2;
        return 1;
    }

    bool eqIsAudible(const LayerEq& eq) noexcept
    {
        return ! eq.bypassed
            && (eq.lowMode == LayerEqEdgeMode::Cut
             || eq.highMode == LayerEqEdgeMode::Cut
             || std::abs(eq.low.gain) > 0.001f
             || std::abs(eq.lowMid.gain) > 0.001f
             || std::abs(eq.highMid.gain) > 0.001f
             || std::abs(eq.high.gain) > 0.001f);
    }

    std::array<BiquadCoefficients, 4> makeEqCoefficients(const LayerEq& eq, double sampleRate) noexcept
    {
        return {
            eq.lowMode == LayerEqEdgeMode::Cut
                ? makeHighPass(eq.low.freq, eq.low.q, sampleRate)
                : makeLowShelf(eq.low, sampleRate),
            makePeak(eq.lowMid, sampleRate),
            makePeak(eq.highMid, sampleRate),
            eq.highMode == LayerEqEdgeMode::Cut
                ? makeLowPass(eq.high.freq, eq.high.q, sampleRate)
                : makeHighShelf(eq.high, sampleRate),
        };
    }

    float processEqSample(float x,
                          std::array<BiquadState, 4>& states,
                          const std::array<BiquadCoefficients, 4>& coeffs) noexcept
    {
        for (size_t i = 0; i < coeffs.size(); ++i)
            x = states[i].process(x, coeffs[i]);
        return x;
    }

    enum class RuntimeFxType { Eq, Filter, Drive, Transient, Compressor };

    struct RuntimeFx
    {
        RuntimeFxType type { RuntimeFxType::Eq };
        size_t stateIndex { 0 };
        std::array<BiquadCoefficients, 4> eqCoeffs {};
        std::array<BiquadCoefficients, MAX_FILTER_STAGES> filterCoeffs {};
        int filterStageCount { 0 };
        LayerDriveFx drive {};
        LayerTransientFx transient {};
        LayerCompressorFx compressor {};
        float transientFastCoeff { 0.0f };
        float transientSlowCoeff { 0.0f };
        float compressorAttackCoeff { 0.0f };
        float compressorReleaseCoeff { 0.0f };
    };

    float envelopeCoeff(float timeMs, double sampleRate) noexcept
    {
        const float samples = juce::jmax(1.0f, static_cast<float>(sampleRate * timeMs * 0.001));
        return std::exp(-1.0f / samples);
    }

    float foldSample(float x) noexcept
    {
        while (x > 1.0f || x < -1.0f)
        {
            if (x > 1.0f)
                x = 2.0f - x;
            else if (x < -1.0f)
                x = -2.0f - x;
        }
        return x;
    }

    float processDriveSample(float x,
                             const LayerDriveFx& drive,
                             VoiceFxState& state,
                             size_t stateIndex,
                             bool left) noexcept
    {
        const float amount = juce::jlimit(0.0f, 1.0f, drive.amount);
        const float mix = juce::jlimit(0.0f, 1.0f, drive.mix);
        const float tone = juce::jlimit(0.0f, 1.0f, drive.tone);
        const int type = juce::jlimit(0, 6, drive.type);
        const float inputGain = 1.0f + amount * (type == 0 ? 4.2f
                                             : type == 1 ? 28.0f
                                             : type == 2 ? 9.5f
                                             : type == 3 ? 7.0f
                                             : type == 4 ? 7.5f
                                             : type == 5 ? 2.2f
                                                         : 3.5f);
        float y = x * inputGain;

        switch (type)
        {
            case 1: // Hard Clip
            {
                const float ceiling = 0.92f - amount * 0.50f;
                y = juce::jlimit(-ceiling, ceiling, y) / juce::jmax(0.001f, ceiling);
                y = juce::jlimit(-1.0f, 1.0f, y);
                break;
            }
            case 2: // Tube
            {
                const float shape = 1.05f + amount * 2.35f;
                const float bias = 0.035f + amount * 0.16f;
                const float offset = std::tanh(bias * shape);
                const float normaliser = std::tanh((1.0f + bias) * shape) - offset;
                const float asymmetric = (std::tanh((y + bias) * shape) - offset)
                                       / juce::jmax(0.05f, normaliser);
                const float even = y * y * (y >= 0.0f ? 0.055f : -0.035f) * amount;
                y = juce::jlimit(-1.15f, 1.15f, asymmetric + even);
                break;
            }
            case 3: // Tape
            {
                const float tapeDrive = 1.0f + amount * 3.8f;
                const float compressed = (y * tapeDrive) / (1.0f + std::abs(y) * (0.82f + amount * 1.65f));
                y = std::atan(compressed * (1.25f + amount * 0.8f)) * (2.0f / kPi);
                y *= 0.96f - amount * 0.10f;
                break;
            }
            case 4: // Fold
            {
                const float folded = foldSample(y * (0.88f + amount * 1.65f));
                y = folded * (0.92f + amount * 0.08f);
                break;
            }
            case 5: // Bit Crush
            {
                auto& hold = left ? state.downsampleHoldLeft[stateIndex] : state.downsampleHoldRight[stateIndex];
                auto& counter = left ? state.downsampleCounterLeft[stateIndex] : state.downsampleCounterRight[stateIndex];
                const float bits = 12.0f - amount * 9.0f;
                const float steps = std::pow(2.0f, juce::jlimit(3.0f, 12.0f, bits)) - 1.0f;
                const int holdSamples = 1 + static_cast<int>(std::round(amount * 9.0f));
                if (counter <= 0)
                {
                    hold = std::round(juce::jlimit(-1.15f, 1.15f, y) * steps) / juce::jmax(1.0f, steps);
                    counter = holdSamples;
                }
                y = hold;
                --counter;
                y = juce::jlimit(-1.0f, 1.0f, y);
                break;
            }
            case 6: // Downsample
            {
                auto& hold = left ? state.downsampleHoldLeft[stateIndex] : state.downsampleHoldRight[stateIndex];
                auto& counter = left ? state.downsampleCounterLeft[stateIndex] : state.downsampleCounterRight[stateIndex];
                const int holdSamples = 1 + static_cast<int>(std::round(amount * 28.0f));
                if (counter <= 0)
                {
                    hold = std::tanh(y * (1.0f + amount * 0.8f));
                    counter = holdSamples;
                }
                y = hold;
                --counter;
                break;
            }
            case 0: // Soft Clip
            default:
            {
                const float shape = 0.72f + amount * 0.95f;
                const float normaliser = std::tanh(inputGain * shape);
                y = std::tanh(y * shape) / (normaliser > 0.001f ? normaliser : 1.0f);
                break;
            }
        }

        auto& toneState = left ? state.driveToneLeft[stateIndex] : state.driveToneRight[stateIndex];
        const float toneAlpha = 0.045f + tone * 0.46f;
        toneState += toneAlpha * (y - toneState);
        const float low = toneState;
        if (tone < 0.5f)
            y += (low - y) * ((0.5f - tone) * 1.75f);
        else
            y += (y - low) * ((tone - 0.5f) * 0.8f);

        const float autoGain = 1.0f / (1.0f + amount * (type == 1 ? 1.12f
                                                   : type == 2 ? 0.54f
                                                   : type == 3 ? 0.42f
                                                   : type == 4 ? 0.58f
                                                   : type == 5 || type == 6 ? 0.12f
                                                               : 0.38f));
        const float outputGain = juce::Decibels::decibelsToGain(juce::jlimit(-24.0f, 12.0f, drive.outputDb));
        const float wet = y * autoGain;
        return (x + (wet - x) * mix) * outputGain;
    }

    float processTransientSample(float x,
                                 const LayerTransientFx& transient,
                                 VoiceFxState& state,
                                 size_t stateIndex,
                                 bool left,
                                 float fastCoeff,
                                 float slowCoeff) noexcept
    {
        const float attack = juce::jlimit(-1.0f, 1.0f, transient.attack);
        const float sustain = juce::jlimit(-1.0f, 1.0f, transient.sustain);
        auto& fast = left ? state.transientFastLeft[stateIndex] : state.transientFastRight[stateIndex];
        auto& slow = left ? state.transientSlowLeft[stateIndex] : state.transientSlowRight[stateIndex];

        const float level = std::abs(x);
        fast = fastCoeff * fast + (1.0f - fastCoeff) * level;
        slow = slowCoeff * slow + (1.0f - slowCoeff) * level;

        const float transientEnergy = juce::jlimit(0.0f, 1.0f, (fast - slow) * 10.0f);
        const float bodyEnergy = juce::jlimit(0.0f, 1.0f, (slow - fast * 0.55f) * 5.0f);
        float gain = 1.0f;

        if (attack >= 0.0f)
            gain += attack * transientEnergy * 1.8f;
        else
            gain *= 1.0f + attack * transientEnergy * 0.75f;

        if (sustain >= 0.0f)
            gain += sustain * bodyEnergy * 1.5f;
        else
            gain *= 1.0f + sustain * bodyEnergy * 0.85f;

        return x * juce::jlimit(0.05f, 3.5f, gain);
    }

    float processCompressorSample(float x,
                                  const LayerCompressorFx& compressor,
                                  VoiceFxState& state,
                                  size_t stateIndex,
                                  bool left,
                                  float attackCoeff,
                                  float releaseCoeff) noexcept
    {
        const float threshold = juce::jlimit(-48.0f, 0.0f, compressor.threshold);
        const float ratio = juce::jlimit(1.0f, 20.0f, compressor.ratio);
        const float mix = juce::jlimit(0.0f, 1.0f, compressor.mix);
        auto& env = left ? state.compressorEnvLeft[stateIndex] : state.compressorEnvRight[stateIndex];

        const float level = std::abs(x);
        const float coeff = level > env ? attackCoeff : releaseCoeff;
        env = coeff * env + (1.0f - coeff) * level;

        const float envDb = juce::Decibels::gainToDecibels(juce::jmax(env, 1.0e-6f), -100.0f);
        float gainDb = 0.0f;
        if (envDb > threshold)
        {
            const float compressedDb = threshold + (envDb - threshold) / ratio;
            gainDb = compressedDb - envDb;
        }

        // ゲインリダクション量 (dB, 正値) を記録。mix を考慮した実効リダクション。
        const float reductionDb = -gainDb * mix;
        if (reductionDb > state.compReductionDb[stateIndex])
            state.compReductionDb[stateIndex] = reductionDb;

        const float wet = x * juce::Decibels::decibelsToGain(gainDb);
        return x + (wet - x) * mix;
    }

    float processFxChainSample(float x,
                               bool left,
                               VoiceFxState& state,
                               const std::array<RuntimeFx, MAX_LAYER_FX_SLOTS>& runtime,
                               size_t runtimeCount) noexcept
    {
        for (size_t i = 0; i < runtimeCount; ++i)
        {
            const auto& fx = runtime[i];
            if (fx.type == RuntimeFxType::Eq)
            {
                auto& eqStates = left ? state.eq[fx.stateIndex].left : state.eq[fx.stateIndex].right;
                x = processEqSample(x, eqStates, fx.eqCoeffs);
            }
            else if (fx.type == RuntimeFxType::Filter)
            {
                auto& filterStates = left ? state.filterLeft[fx.stateIndex] : state.filterRight[fx.stateIndex];
                for (int s = 0; s < fx.filterStageCount; ++s)
                    x = filterStates[static_cast<size_t>(s)].process(x, fx.filterCoeffs[static_cast<size_t>(s)]);
            }
            else if (fx.type == RuntimeFxType::Drive)
            {
                x = processDriveSample(x, fx.drive, state, fx.stateIndex, left);
            }
            else if (fx.type == RuntimeFxType::Transient)
            {
                x = processTransientSample(x, fx.transient, state, fx.stateIndex, left,
                                           fx.transientFastCoeff, fx.transientSlowCoeff);
            }
            else if (fx.type == RuntimeFxType::Compressor)
            {
                x = processCompressorSample(x, fx.compressor, state, fx.stateIndex, left,
                                            fx.compressorAttackCoeff, fx.compressorReleaseCoeff);
            }
        }
        return x;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 発音開始
// ─────────────────────────────────────────────────────────────────────────────
void DrumVoice::start(int    padIdx,
                      double startSamp,
                      double endSamp,
                      float  gainLeft,
                      float  gainRight,
                      bool   oneShot,
                      float  attackTimeSec,
                      float  releaseTimeSec,
                      double hostSampleRate,
                      double pRatio,
                      bool   rev,
                      int    fadeInSamp,
                      int    fadeOutSamp,
                      double sourceLen,
                      uint64_t serial,
                      int    startDelaySamp,
                      bool   previewVoice,
                      bool   swapChannels) noexcept
{
    padIndex      = padIdx;
    isActive      = true;
    isPreview     = previewVoice;
    triggerSerial = serial;
    startSample   = startSamp;
    endSample     = endSamp;
    sourceLength  = (sourceLen > 1.0) ? sourceLen : 1.0;
    gainL         = gainLeft;
    gainR         = gainRight;
    swapLR        = swapChannels;
    isOneShot     = oneShot;
    isReleasing   = false;
    playbackRatio = (pRatio > 0.001) ? pRatio : 0.001;   // ゼロ除算ガード
    reversed      = rev;
    fadeInSamples  = fadeInSamp;
    fadeOutSamples = fadeOutSamp;
    samplesRendered = 0;
    startDelaySamples = juce::jmax(0, startDelaySamp);
    eqState.reset();
    fxState.reset();

    // 逆再生の場合は終端から開始
    if (reversed)
        samplePos = endSamp - playbackRatio;
    else
        samplePos = startSamp;

    // アタックレート: 1サンプルで envelope が増える量
    if (attackTimeSec > 0.001f && hostSampleRate > 0.0)
        attackRate = 1.0f / static_cast<float>(attackTimeSec * hostSampleRate);
    else
        attackRate = 1.0f;   // 即座に最大値

    // リリースレート
    if (releaseTimeSec > 0.001f && hostSampleRate > 0.0)
        releaseRate = 1.0f / static_cast<float>(releaseTimeSec * hostSampleRate);
    else
        releaseRate = 0.1f;

    // エンベロープ初期値
    envelope = (attackRate >= 1.0f) ? 1.0f : 0.0f;
}

// ─────────────────────────────────────────────────────────────────────────────
// リリース開始（Gate モードで Note Off が来たとき）
// ─────────────────────────────────────────────────────────────────────────────
void DrumVoice::triggerRelease() noexcept
{
    if (isOneShot) return;
    isReleasing = true;
}

void DrumVoice::forceRelease(float releaseTimeSec, double hostSampleRate) noexcept
{
    if (! isActive)
        return;

    isOneShot = false;
    isReleasing = true;

    if (releaseTimeSec > 0.001f && hostSampleRate > 0.0)
        releaseRate = 1.0f / static_cast<float>(releaseTimeSec * hostSampleRate);
    else
        releaseRate = 1.0f;
}

// ─────────────────────────────────────────────────────────────────────────────
// レンダリング（processBlock 内で毎回呼ばれる）
//
// 設計メモ:
//   envelope  = アタック/リリースエンベロープ（Note On/Off で制御）
//   fadeGain  = ユーザー設定のフェードイン/アウト（サンプル先頭・末尾の形状）
//   両者を乗算して最終ゲインとする
//
// リバース再生:
//   samplePos を endSample から startSample へ向かって減算する。
//   補間は常に floor(pos) ↔ floor(pos)+1 で行うため、前進・後退で式が変わらない。
// ─────────────────────────────────────────────────────────────────────────────
bool DrumVoice::render(const juce::AudioBuffer<float>& source,
                       juce::AudioBuffer<float>&       output,
                       int                             numSamples,
                       const LayerData&                layer,
                       double                          hostSampleRate) noexcept
{
    if (!isActive) return false;

    lastPeakLevel = 0.0f;   // このブロックのピークをリセット
    fxState.compReductionDb.fill(0.0f);   // Compressor GR メーター用

    const int srcLen = source.getNumSamples();
    if (srcLen <= 0) { isActive = false; return false; }

    // ソースがモノラルの場合は L を両チャンネルに使う
    const bool isStereo = source.getNumChannels() >= 2;
    const float* srcL   = source.getReadPointer(0);
    const float* srcR   = isStereo ? source.getReadPointer(1) : srcL;

    if (output.getNumChannels() < 2) { isActive = false; return false; }
    float* outL = output.getWritePointer(0);
    float* outR = output.getWritePointer(1);
    const bool useEq = layer.fxChain.empty() && eqIsAudible(layer.eq) && hostSampleRate > 0.0;
    const auto eqCoeffs = useEq ? makeEqCoefficients(layer.eq, hostSampleRate)
                                : std::array<BiquadCoefficients, 4> {};
    std::array<RuntimeFx, MAX_LAYER_FX_SLOTS> runtimeFx {};
    size_t runtimeFxCount = 0;
    if (hostSampleRate > 0.0)
    {
        const size_t slotCount = std::min(layer.fxChain.size(), MAX_LAYER_FX_SLOTS);
        for (size_t slotIndex = 0; slotIndex < slotCount; ++slotIndex)
        {
            const auto& slot = layer.fxChain[slotIndex];
            if (slot.bypassed)
                continue;

            if (slot.type == LayerFxType::Eq && eqIsAudible(slot.eq))
            {
                auto& fx = runtimeFx[runtimeFxCount++];
                fx.type = RuntimeFxType::Eq;
                fx.stateIndex = slotIndex;
                fx.eqCoeffs = makeEqCoefficients(slot.eq, hostSampleRate);
            }
            else if (slot.type == LayerFxType::Filter
                  && (slot.filter.hpEnabled || slot.filter.lpEnabled))
            {
                auto& fx = runtimeFx[runtimeFxCount++];
                fx.type = RuntimeFxType::Filter;
                fx.stateIndex = slotIndex;

                // HP → LP の順にカスケード。セクションごとに slope/Q が独立。
                // 各セクションの先頭段だけユーザー Q、残り段は Butterworth (0.707)。
                int n = 0;
                if (slot.filter.hpEnabled)
                {
                    const int stages = filterStagesForSlope(slot.filter.hpSlope);
                    for (int s = 0; s < stages; ++s)
                        fx.filterCoeffs[static_cast<size_t>(n++)] =
                            makeHighPass(slot.filter.hpCutoff, s == 0 ? slot.filter.hpResonance : 0.70710678f, hostSampleRate);
                }
                if (slot.filter.lpEnabled)
                {
                    const int stages = filterStagesForSlope(slot.filter.lpSlope);
                    for (int s = 0; s < stages; ++s)
                        fx.filterCoeffs[static_cast<size_t>(n++)] =
                            makeLowPass(slot.filter.lpCutoff, s == 0 ? slot.filter.lpResonance : 0.70710678f, hostSampleRate);
                }
                fx.filterStageCount = n;
            }
            else if (slot.type == LayerFxType::Drive
                  && (slot.drive.mix > 0.001f || std::abs(slot.drive.outputDb) > 0.001f))
            {
                auto& fx = runtimeFx[runtimeFxCount++];
                fx.type = RuntimeFxType::Drive;
                fx.stateIndex = slotIndex;
                fx.drive = slot.drive;
            }
            else if (slot.type == LayerFxType::Transient
                  && (std::abs(slot.transient.attack) > 0.001f || std::abs(slot.transient.sustain) > 0.001f))
            {
                auto& fx = runtimeFx[runtimeFxCount++];
                fx.type = RuntimeFxType::Transient;
                fx.stateIndex = slotIndex;
                fx.transient = slot.transient;
                fx.transientFastCoeff = envelopeCoeff(1.5f, hostSampleRate);
                fx.transientSlowCoeff = envelopeCoeff(85.0f, hostSampleRate);
            }
            else if (slot.type == LayerFxType::Compressor
                  && slot.compressor.mix > 0.001f
                  && slot.compressor.ratio > 1.001f)
            {
                auto& fx = runtimeFx[runtimeFxCount++];
                fx.type = RuntimeFxType::Compressor;
                fx.stateIndex = slotIndex;
                fx.compressor = slot.compressor;
                fx.compressorAttackCoeff = envelopeCoeff(slot.compressor.attack, hostSampleRate);
                fx.compressorReleaseCoeff = envelopeCoeff(slot.compressor.release, hostSampleRate);
            }
        }
    }
    const bool useFxChain = runtimeFxCount > 0;

    const bool canUseUnityForwardPath =
        ! reversed &&
        ! isReleasing &&
        startDelaySamples <= 0 &&
        attackRate >= 1.0f &&
        envelope >= 1.0f &&
        fadeInSamples <= 0 &&
        fadeOutSamples <= 0 &&
        std::abs(playbackRatio - 1.0) < 1.0e-9;

    if (canUseUnityForwardPath)
    {
        const int startPos = static_cast<int>(samplePos);
        const int endPos = juce::jlimit(0, srcLen, static_cast<int>(endSample));
        const int available = juce::jlimit(0, numSamples, endPos - startPos);

        if (available <= 0 || startPos < 0 || startPos >= srcLen)
        {
            isActive = false;
            return false;
        }

        for (int i = 0; i < available; ++i)
        {
            const int pos = startPos + i;
            const float rawL = srcL[pos];
            const float rawR = srcR[pos];
            const float sL = useFxChain ? processFxChainSample(rawL, true, fxState, runtimeFx, runtimeFxCount)
                           : useEq ? processEqSample(rawL, eqState.left, eqCoeffs)
                           : rawL;
            const float sR = useFxChain ? processFxChainSample(rawR, false, fxState, runtimeFx, runtimeFxCount)
                           : useEq ? processEqSample(rawR, eqState.right, eqCoeffs)
                           : rawR;
            const float outSampleL = sL * gainL;
            const float outSampleR = sR * gainR;
            outL[i] += swapLR ? outSampleR : outSampleL;
            outR[i] += swapLR ? outSampleL : outSampleR;
            lastPeakLevel = std::max(lastPeakLevel,
                                     std::max(std::abs(outSampleL), std::abs(outSampleR)));
        }

        samplePos += static_cast<double>(available);
        samplesRendered += available;

        if (available < numSamples || samplePos >= endSample || samplePos >= static_cast<double>(srcLen))
            isActive = false;

        return isActive;
    }

    for (int i = 0; i < numSamples; ++i)
    {
        if (startDelaySamples > 0)
        {
            --startDelaySamples;
            continue;
        }

        // ── エンベロープ更新 ───────────────────────────────────────────────
        if (isReleasing)
        {
            envelope -= releaseRate;
            if (envelope <= 0.0f)
            {
                isActive = false;
                break;
            }
        }
        else
        {
            if (envelope < 1.0f)
                envelope = std::min(1.0f, envelope + attackRate);
        }

        // ── 再生位置チェック ───────────────────────────────────────────────
        const int pos = static_cast<int>(samplePos);

        if (reversed)
        {
            // 逆再生: samplePos が startSample を下回ったら終了
            if (samplePos < startSample || pos < 0)
            {
                isActive = false;
                break;
            }
        }
        else
        {
            // 通常再生: endSample または バッファ末尾に達したら終了
            if (pos >= static_cast<int>(endSample) || pos >= srcLen)
            {
                isActive = false;
                break;
            }
        }

        // ── 線形補間でサンプルを読み取る ──────────────────────────────────
        // 補間は常に pos と pos+1 の間（再生方向に依存しない）
        const int   clampedPos = juce::jlimit(0, srcLen - 1, pos);
        const int   nextPos    = std::min(pos + 1, srcLen - 1);
        const float frac       = static_cast<float>(samplePos - static_cast<double>(pos));

        const float sL = srcL[clampedPos] + frac * (srcL[nextPos] - srcL[clampedPos]);
        const float sR = srcR[clampedPos] + frac * (srcR[nextPos] - srcR[clampedPos]);

        // ── フェードゲイン計算 ────────────────────────────────────────────
        float fadeGain = 1.0f;

        // フェードイン: 発音開始から fadeInSamples かけて 0→1 に上げる
        if (fadeInSamples > 0 && samplesRendered < fadeInSamples)
            fadeGain *= static_cast<float>(samplesRendered)
                      / static_cast<float>(fadeInSamples);

        // フェードアウト: 再生終端に近づくにつれて 1→0 に下げる
        if (fadeOutSamples > 0)
        {
            // 残り再生サンプル数を推定（playbackRatio 考慮）
            const double remaining = reversed
                ? (samplePos - startSample) / playbackRatio
                : (endSample  - samplePos)  / playbackRatio;

            if (remaining < static_cast<double>(fadeOutSamples))
            {
                fadeGain *= static_cast<float>(remaining)
                          / static_cast<float>(fadeOutSamples);
                fadeGain = std::max(0.0f, fadeGain);
            }
        }

        // ── 出力バッファに加算 & ピーク計測 ──────────────────────────────
        const float fxL = useFxChain ? processFxChainSample(sL, true, fxState, runtimeFx, runtimeFxCount)
                        : useEq ? processEqSample(sL, eqState.left, eqCoeffs)
                        : sL;
        const float fxR = useFxChain ? processFxChainSample(sR, false, fxState, runtimeFx, runtimeFxCount)
                        : useEq ? processEqSample(sR, eqState.right, eqCoeffs)
                        : sR;
        const float outSampleL = fxL * gainL * envelope * fadeGain;
        const float outSampleR = fxR * gainR * envelope * fadeGain;
        outL[i] += swapLR ? outSampleR : outSampleL;
        outR[i] += swapLR ? outSampleL : outSampleR;
        lastPeakLevel = std::max(lastPeakLevel,
                                 std::max(std::abs(outSampleL), std::abs(outSampleR)));

        // ── 再生位置を進める ───────────────────────────────────────────────
        if (reversed)
            samplePos -= playbackRatio;
        else
            samplePos += playbackRatio;

        ++samplesRendered;
    }

    return isActive;
}

float DrumVoice::getPlaybackPositionNormalized() const noexcept
{
    if (sourceLength <= 1.0)
        return 0.0f;

    // Reverse mode mirrors the waveform display. Mirror the source position
    // into the trimmed display range as well so the playhead always travels
    // from the visible start marker to the end marker.
    const double displayPosition = reversed
        ? startSample + (endSample - samplePos)
        : samplePos;

    return juce::jlimit(0.0f, 1.0f,
                        static_cast<float>(displayPosition / sourceLength));
}
