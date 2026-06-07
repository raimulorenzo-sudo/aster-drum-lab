#include "VoiceManager.h"
#include "FaderCurve.h"

VoiceManager::VoiceManager() noexcept
{
    for (auto& level : padTriggerLevels)
        level.store(0.0f, std::memory_order_relaxed);

    for (auto& padLayers : layerTriggerLevels)
        for (auto& lv : padLayers)
            lv.store(0.0f, std::memory_order_relaxed);

    for (auto& padLayers : layerPeakLevels)
        padLayers.fill(0.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// 空きボイスを探す（全部使用中なら index 0 を返す＝最古を奪う）
// ─────────────────────────────────────────────────────────────────────────────
int VoiceManager::findFreeVoice() const noexcept
{
    for (int i = 0; i < MAX_VOICES; ++i)
        if (!voices[static_cast<size_t>(i)].isActive) return i;

    return 0;   // ボイス・スティーリング（Phase 2 でより賢い実装に変える）
}

// ─────────────────────────────────────────────────────────────────────────────
// v7+: Polyphony 上限を Pad 単位で適用。同一 triggerSerial を「1 hit」とみなす。
// hits 数 (distinct triggerSerial 数) >= polyphony なら voiceSteal モードに従って
// 最古/最静の hit を release → 新規発音可能にする。
// ─────────────────────────────────────────────────────────────────────────────
bool VoiceManager::enforcePolyphonyForPad(int padIndex,
                                          const PadData& pad,
                                          double hostSampleRate) noexcept
{
    const int limit = pad.polyphony;
    if (limit <= 0) return true; // Unlimited

    // 同 Pad の distinct triggerSerial を収集
    // (1 hit が複数 layer を起動するので serial で hit を判別)
    struct HitInfo { uint64_t serial; float peakSum; };
    std::array<HitInfo, MAX_VOICES> hits {};
    int hitCount = 0;

    for (auto& v : voices)
    {
        if (! v.isActive || v.padIndex != padIndex) continue;
        // すでに記録済みの serial か?
        bool found = false;
        for (int i = 0; i < hitCount; ++i)
        {
            if (hits[(size_t) i].serial == v.triggerSerial)
            {
                hits[(size_t) i].peakSum += v.lastPeakLevel;
                found = true;
                break;
            }
        }
        if (! found && hitCount < MAX_VOICES)
        {
            hits[(size_t) hitCount].serial  = v.triggerSerial;
            hits[(size_t) hitCount].peakSum = v.lastPeakLevel;
            ++hitCount;
        }
    }

    if (hitCount < limit) return true; // まだ余裕あり

    // 上限到達 → voiceSteal モードで挙動分岐
    if (pad.voiceSteal == PadData::VoiceStealMode::Off)
        return false; // 新規発音を捨てる

    // Oldest / Quietest を選択
    int victimIdx = 0;
    if (pad.voiceSteal == PadData::VoiceStealMode::Quietest)
    {
        float minPeak = hits[0].peakSum;
        for (int i = 1; i < hitCount; ++i)
            if (hits[(size_t) i].peakSum < minPeak)
            {
                minPeak  = hits[(size_t) i].peakSum;
                victimIdx = i;
            }
    }
    else // Oldest
    {
        uint64_t minSerial = hits[0].serial;
        for (int i = 1; i < hitCount; ++i)
            if (hits[(size_t) i].serial < minSerial)
            {
                minSerial = hits[(size_t) i].serial;
                victimIdx = i;
            }
    }

    const uint64_t victimSerial = hits[(size_t) victimIdx].serial;
    // 該当 serial の全 voice を高速 release
    for (auto& v : voices)
    {
        if (v.isActive && v.padIndex == padIndex && v.triggerSerial == victimSerial)
            v.forceRelease(0.004f, hostSampleRate);
    }
    return true;
}

void VoiceManager::chokeVoicesForPad(int padIndex,
                                     const KitData& kit,
                                     double hostSampleRate) noexcept
{
    if (padIndex < 0 || padIndex >= NUM_PADS) return;

    const int group = kit.pads[static_cast<size_t>(padIndex)].chokeGroup;
    if (group <= 0) return;

    for (auto& voice : voices)
    {
        if (! voice.isActive || voice.padIndex == padIndex)
            continue;

        if (voice.padIndex < 0 || voice.padIndex >= NUM_PADS)
            continue;

        const auto& otherPad = kit.pads[static_cast<size_t>(voice.padIndex)];
        if (otherPad.chokeGroup == group)
            voice.forceRelease(0.006f, hostSampleRate);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Note On  ─  Pad に属する全 Layer のうち、有効なものを起動する。
// ─────────────────────────────────────────────────────────────────────────────
void VoiceManager::noteOn(int                     padIndex,
                           float                   velocity,
                           const KitData&          kit,
                           const AudioFileManager& files,
                           double                  hostSampleRate)
{
    startVoicesForPad(padIndex, velocity, kit, files, hostSampleRate,
                      /*previewVoice=*/ false,
                      /*previewLayerIndex=*/ -1);
}

// v7+: Pad の視覚フラッシュだけ発火 (Voice 生成しない)。
// 呼び出し側は MIDI Note On 受信時に必ずこれを呼ぶ。サンプル / Mute / Solo /
// Polyphony 制限に関わらず常に Pad が光る (=「MIDI が届いている」フィードバック)。
void VoiceManager::notifyPadVisualTrigger(int padIndex, float velocity) noexcept
{
    if (padIndex < 0 || padIndex >= NUM_PADS) return;
    // velocity を 0..1 にクランプし、0 のときは触らない (Note Off velocity 0 等の事故防止)
    const float v = juce::jlimit(0.0f, 1.0f, velocity);
    if (v <= 0.0f) return;
    padTriggerLevels[static_cast<size_t>(padIndex)].store(v, std::memory_order_relaxed);
}

void VoiceManager::previewNoteOn(int                     padIndex,
                                 float                   velocity,
                                 const KitData&          kit,
                                 const AudioFileManager& files,
                                 double                  hostSampleRate,
                                 int                     layerIndex)
{
    stopPreviewVoices(hostSampleRate);
    startVoicesForPad(padIndex, velocity, kit, files, hostSampleRate,
                      /*previewVoice=*/ true,
                      /*previewLayerIndex=*/ layerIndex);
}

void VoiceManager::startVoicesForPad(int                     padIndex,
                                     float                   velocity,
                                     const KitData&          kit,
                                     const AudioFileManager& files,
                                     double                  hostSampleRate,
                                     bool                    previewVoice,
                                     int                     previewLayerIndex)
{
    if (padIndex < 0 || padIndex >= NUM_PADS) return;

    const PadData& pad = kit.pads[static_cast<size_t>(padIndex)];

    // Pad-level Mute / Solo は全 Layer を一括スキップ
    if (pad.mute) return;

    const bool anySoloActive = std::any_of(kit.pads.begin(), kit.pads.end(),
                                           [] (const PadData& p) { return p.solo; });
    if (anySoloActive && ! pad.solo) return;

    // ── Pad 内で Layer Solo が立っていれば、その Layer 群だけ通す ───────────
    bool anyLayerSolo = false;
    for (const auto& L : pad.layers)
        if (L.solo) { anyLayerSolo = true; break; }

    // ── MIDI velocity 整数（Layer の velocityMin/Max は 0..127 で判定） ─────
    const int velMidi = juce::jlimit(0, 127, (int) std::round(velocity * 127.0f));

    // Choke は 1 回だけ実行（Pad 単位）
    chokeVoicesForPad(padIndex, kit, hostSampleRate);

    // v7+: Polyphony 上限を適用 (Off モードなら新規発音中止)
    if (! enforcePolyphonyForPad(padIndex, pad, hostSampleRate))
        return;

    // Humanize is generated once per Pad trigger. Every Layer receives the
    // same musical movement, preserving phase and transient relationships.
    PadHumanize humanize;
    const float amount = juce::jlimit(0.0f, 1.0f, pad.humanize);
    if (amount > 0.0f)
    {
        const auto bipolarRandom = [this]() noexcept
        {
            return random.nextFloat() * 2.0f - 1.0f;
        };
        humanize.velocityMultiplier = 1.0f + bipolarRandom() * 0.12f * amount;
        humanize.panOffset          = bipolarRandom() * 0.12f * amount;
        humanize.pitchOffset        = bipolarRandom() * 0.35f * amount;
        humanize.startOffsetAmount  = random.nextFloat() * amount;
        humanize.delayAmount        = random.nextFloat() * amount;
    }

    bool triggeredAny = false;
    const int layerCount = pad.layerCount();
    for (int li = 0; li < layerCount; ++li)
    {
        // Preview で特定 Layer のみ鳴らしたい場合
        if (previewVoice && previewLayerIndex >= 0 && li != previewLayerIndex)
            continue;

        const auto& L = pad.layers[(size_t) li];

        if (L.mute)                                              continue;
        if (anyLayerSolo && ! L.solo)                            continue;
        if (velMidi < L.velocityMin || velMidi > L.velocityMax)  continue;

        // サンプルが読み込まれていなければ Skip（CPU を使わない）
        const juce::AudioBuffer<float>* buf = files.getBufferNoLock(padIndex, li);
        if (buf == nullptr || buf->getNumSamples() == 0)
            continue;

        startLayerVoice(padIndex, li, velocity, kit, files, hostSampleRate,
                        previewVoice, humanize);
        triggeredAny = true;
    }

    if (triggeredAny)
    {
        padTriggerLevels[static_cast<size_t>(padIndex)].store(
            juce::jlimit(0.0f, 1.0f, velocity),
            std::memory_order_relaxed);
    }
}

void VoiceManager::startLayerVoice(int                     padIndex,
                                   int                     layerIndex,
                                   float                   velocity,
                                   const KitData&          kit,
                                   const AudioFileManager& files,
                                   double                  hostSampleRate,
                                   bool                    previewVoice,
                                   const PadHumanize&      humanize)
{
    if (padIndex < 0 || padIndex >= NUM_PADS) return;

    const PadData&   pad = kit.pads[static_cast<size_t>(padIndex)];
    if (layerIndex < 0 || layerIndex >= pad.layerCount()) return;
    const LayerData& L   = pad.layers[static_cast<size_t>(layerIndex)];

    const juce::AudioBuffer<float>* buf = files.getBufferNoLock(padIndex, layerIndex);
    if (buf == nullptr || buf->getNumSamples() == 0) return;

    const float humanizedVelocity = juce::jlimit(
        0.0f, 1.0f, velocity * humanize.velocityMultiplier);
    const float effectivePan = juce::jlimit(
        -1.0f, 1.0f, L.pan + pad.padPan + humanize.panOffset);
    const float humanizedPitch = juce::jlimit(
        -48.0f, 48.0f, L.pitch + pad.padPitch + humanize.pitchOffset);
    double startOffsetSamples = 0.0;
    int startDelaySamples = 0;

    // ── ゲイン計算 ───────────────────────────────────────────────────────
    //   Q3: 信号フロー = FaderCurve(layer.volume) × FaderCurve(pad.padVolume)
    //   pad.padVolume は v6 で導入。既定値 0.75（=unity）なので Step 1 の
    //   時点では既存プリセットの音量に影響しない。
    //
    //   VEL Curve（v7+）: humanizedVelocity を pad.velCurve で写像してから
    //   velocitySens の amount で lerp する。
    //     curveValue = applyVelCurve(velCurve, humanizedVelocity)
    //     velGain    = lerp(1.0, curveValue, velocitySens)
    //   = 1.0 - velocitySens * (1.0 - curveValue)
    //   既存式と互換: velCurve = Linear (p1=0.33/0.33, p2=0.66/0.66) のとき
    //   curveValue ≈ humanizedVelocity となるので、Linear 状態では従来動作と一致。
    auto applyVelCurve = [] (const PadData::VelCurve& c, float xIn) noexcept
    {
        const float x  = juce::jlimit(0.0f, 1.0f, xIn);
        const float ax = juce::jmin(c.p1x, c.p2x);
        const float bx = juce::jmax(c.p1x, c.p2x);
        const float ay = (c.p1x <= c.p2x) ? c.p1y : c.p2y;
        const float by = (c.p1x <= c.p2x) ? c.p2y : c.p1y;
        if (x <= ax)
            return (ax > 1.0e-6f) ? (ay * x / ax) : ay;
        if (x <= bx)
            return ay + (by - ay) * ((x - ax) / juce::jmax(1.0e-6f, bx - ax));
        return by + (1.0f - by) * ((x - bx) / juce::jmax(1.0e-6f, 1.0f - bx));
    };
    const float curveValue  = applyVelCurve(pad.velCurve, humanizedVelocity);
    const float velGain     = 1.0f - pad.velocitySens * (1.0f - curveValue);
    const float layerGain   = FaderCurve::positionToGain(L.volume);
    const float padGain     = FaderCurve::positionToGain(pad.padVolume);
    const float volumeGain  = layerGain * padGain;

    const float panAngle = (effectivePan + 1.0f) * 0.5f
                         * juce::MathConstants<float>::pi * 0.5f;
    const float gl = volumeGain * velGain * std::cos(panAngle);
    const float gr = volumeGain * velGain * std::sin(panAngle);

    // ── 再生範囲 ─────────────────────────────────────────────────────────
    const double totalSamples = static_cast<double>(buf->getNumSamples());
    double startSamp = totalSamples * static_cast<double>(L.startPosition);
    double endSamp   = totalSamples * static_cast<double>(L.endPosition);

    if (humanize.startOffsetAmount > 0.0f || humanize.delayAmount > 0.0f)
    {
        const double srcSampleRateForStart = files.getSampleRate(padIndex, layerIndex);
        const double samplesPerMs = (srcSampleRateForStart > 0.0) ? (srcSampleRateForStart / 1000.0) : 44.1;
        const double maxStartOffset = juce::jmin(8.0 * samplesPerMs,
                                                 (endSamp - startSamp) * 0.02);
        startOffsetSamples = maxStartOffset * static_cast<double>(humanize.startOffsetAmount);
        startDelaySamples  = static_cast<int>(
            4.0f * static_cast<float>(hostSampleRate / 1000.0) * humanize.delayAmount);

        if (L.reverse)
            endSamp = juce::jmax(startSamp + 1.0, endSamp - startOffsetSamples);
        else
            startSamp = juce::jmin(endSamp - 1.0, startSamp + startOffsetSamples);
    }

    const double rangeSamples = endSamp - startSamp;

    // ── ピッチ / リバース / フェード ─────────────────────────────────────
    const double srcSampleRate   = files.getSampleRate(padIndex, layerIndex);
    const double sampleRateRatio = (hostSampleRate > 0.0 && srcSampleRate > 0.0)
                                     ? (srcSampleRate / hostSampleRate)
                                     : 1.0;
    const double pitchRatio      = std::pow(2.0, static_cast<double>(humanizedPitch) / 12.0);
    const double playbackRatio   = sampleRateRatio * pitchRatio;

    const int fadeInSamp  = static_cast<int>(L.fadeIn  * rangeSamples / playbackRatio);
    const int fadeOutSamp = static_cast<int>(L.fadeOut * rangeSamples / playbackRatio);

    // ── ボイスを起動 ──────────────────────────────────────────────────────
    const int slot = findFreeVoice();
    auto& v = voices[static_cast<size_t>(slot)];
    v.layerIndex = layerIndex;

    // 発音通知: UI 発音フラッシュ / レイヤーメーター用
    if (padIndex >= 0 && padIndex < NUM_PADS
        && layerIndex >= 0 && layerIndex < MAX_LAYERS_PER_PAD)
    {
        layerTriggerLevels[static_cast<size_t>(padIndex)][static_cast<size_t>(layerIndex)]
            .store(juce::jlimit(0.0f, 1.0f, velocity), std::memory_order_relaxed);
    }

    v.start(padIndex,
            startSamp,
            endSamp,
            gl,
            gr,
            pad.playbackMode == PlaybackMode::OneShot,
            L.attack,
            L.release,
            hostSampleRate,
            playbackRatio,
            L.reverse,
            fadeInSamp,
            fadeOutSamp,
            totalSamples,
            ++triggerSerialCounter,
            startDelaySamples,
            previewVoice);
}

// ─────────────────────────────────────────────────────────────────────────────
// Note Off
// ─────────────────────────────────────────────────────────────────────────────
void VoiceManager::noteOff(int padIndex)
{
    for (auto& v : voices)
        if (v.isActive && ! v.isPreview && v.padIndex == padIndex)
            v.triggerRelease();   // One Shot のボイスは内部で無視される
}

void VoiceManager::previewNoteOff(int padIndex, double hostSampleRate)
{
    juce::ignoreUnused(hostSampleRate);
    for (auto& v : voices)
        if (v.isActive && v.isPreview && v.padIndex == padIndex)
            v.triggerRelease();   // Gate preview only. OneShot previews stop on the next preview trigger.
}

void VoiceManager::stopPreviewVoices(double hostSampleRate, float fadeOutMs) noexcept
{
    const float releaseSeconds = juce::jmax(0.001f, fadeOutMs * 0.001f);
    for (auto& v : voices)
        if (v.isActive && v.isPreview)
            v.forceRelease(releaseSeconds, hostSampleRate);
}

bool VoiceManager::hasActiveVoices() const noexcept
{
    for (const auto& v : voices)
        if (v.isActive)
            return true;

    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// オーディオレンダリング（マルチアウト対応）
// 呼び出し元が fileManager の読み取りロックを保持している前提
// ─────────────────────────────────────────────────────────────────────────────
void VoiceManager::process(juce::AudioBuffer<float>* const* busBuffers,
                            int                              busCount,
                            OutputMode                       outputMode,
                            const KitData&                   kit,
                            const AudioFileManager&          files,
                            int                              numSamples,
                            double                           hostSampleRate)
{
    // ── このブロックのレベルをリセット ──────────────────────────────────────
    padPeakLevels.fill(0.0f);
    for (auto& pl : layerPeakLevels) pl.fill(0.0f);
    for (auto& pl : compReductionDb) for (auto& ll : pl) ll.fill(0.0f);

    const bool anySoloActive = std::any_of(kit.pads.begin(), kit.pads.end(),
                                           [] (const PadData& p) { return p.solo; });

    // OutputMode == Stereo のときは全 Voice を bus 0 へ集約
    const bool forceMain     = (outputMode == OutputMode::Stereo);
    const int  activeMax     = getActiveOutputCount(outputMode);  // 1/16/32/48
    const int  effectiveMax  = juce::jmin(activeMax, busCount);

    for (auto& voice : voices)
    {
        if (!voice.isActive) continue;

        if (voice.padIndex < 0 || voice.padIndex >= NUM_PADS)
        {
            voice.isActive = false;
            continue;
        }

        // ── どのバスに送るか決める ────────────────────────────────────────
        const auto& pad = kit.pads[static_cast<size_t>(voice.padIndex)];
        if (pad.mute || (anySoloActive && ! pad.solo))
            continue;

        int busIdx = forceMain ? 0 : pad.outputAssign;

        // OutputMode の制限を超えた assign は OUT 1 にフォールバック
        if (busIdx < 0 || busIdx >= effectiveMax) busIdx = 0;
        if (busIdx >= busCount)                   busIdx = 0;

        // 宣言バスの範囲外
        if (busIdx >= busCount)
        {
            voice.isActive = false;
            continue;
        }

        auto* dst = busBuffers[busIdx];

        // 重要: アサインされたバスが DAW で無効化されている場合（Logic で
        // 通常のステレオ楽器として使う場合の Aux バスなど）は Main (Bus 0)
        // にフォールバックする。これがないと Pad 2〜48 が無音になる。
        if (dst == nullptr && busIdx != 0)
            dst = busBuffers[0];

        if (dst == nullptr || dst->getNumChannels() < 2)
            continue;  // Main も無効: 諦めて Voice 維持

        const juce::AudioBuffer<float>* src = files.getBufferNoLock(voice.padIndex, voice.layerIndex);
        if (src == nullptr)
        {
            voice.isActive = false;
            continue;
        }

        if (voice.layerIndex < 0 || voice.layerIndex >= pad.layerCount())
        {
            voice.isActive = false;
            continue;
        }

        const auto& layer = pad.layers[static_cast<size_t>(voice.layerIndex)];
        voice.render(*src, *dst, numSamples, layer, hostSampleRate);

        // ── パッドのピークを集計 ──────────────────────────────────────────
        const auto pu = static_cast<size_t>(voice.padIndex);
        padPeakLevels[pu] = std::max(padPeakLevels[pu], voice.lastPeakLevel);
        if (voice.lastPeakLevel >= 1.0f)
            padClipLatched[pu] = true;

        // ── Layer 単位のピークも集計 (UI レイヤーメーター用) ──────────────
        const auto lu = static_cast<size_t>(voice.layerIndex);
        if (lu < MAX_LAYERS_PER_PAD)
        {
            layerPeakLevels[pu][lu] = std::max(layerPeakLevels[pu][lu], voice.lastPeakLevel);

            // ── Compressor GR を (pad, layer, slot) 単位で集計 ──────────────
            auto& grSlots = compReductionDb[pu][lu];
            for (size_t s = 0; s < MAX_LAYER_FX_SLOTS; ++s)
                grSlots[s] = std::max(grSlots[s], voice.fxState.compReductionDb[s]);
        }
    }
}

float VoiceManager::getPadLevel(int padIndex) const noexcept
{
    if (padIndex < 0 || padIndex >= NUM_PADS) return 0.0f;
    return padPeakLevels[static_cast<size_t>(padIndex)];
}

void VoiceManager::clearPadLevels() noexcept
{
    padPeakLevels.fill(0.0f);
    for (auto& padLayers : layerPeakLevels)
        padLayers.fill(0.0f);
    for (auto& pl : compReductionDb) for (auto& ll : pl) ll.fill(0.0f);
}

bool VoiceManager::getPadClipLatched(int padIndex) const noexcept
{
    if (padIndex < 0 || padIndex >= NUM_PADS) return false;
    return padClipLatched[static_cast<size_t>(padIndex)];
}

void VoiceManager::clearPadClip(int padIndex) noexcept
{
    if (padIndex < 0 || padIndex >= NUM_PADS) return;
    padClipLatched[static_cast<size_t>(padIndex)] = false;
}

void VoiceManager::clearAllPadClips() noexcept
{
    padClipLatched.fill(false);
}

float VoiceManager::getPadTriggerLevel(int padIndex) const noexcept
{
    if (padIndex < 0 || padIndex >= NUM_PADS) return 0.0f;
    return padTriggerLevels[static_cast<size_t>(padIndex)].load(std::memory_order_relaxed);
}

float VoiceManager::consumePadTriggerLevel(int padIndex) noexcept
{
    if (padIndex < 0 || padIndex >= NUM_PADS) return 0.0f;
    return padTriggerLevels[static_cast<size_t>(padIndex)].exchange(0.0f, std::memory_order_relaxed);
}

bool VoiceManager::getPadPlayheadPosition(int padIndex, float& outPosition) const noexcept
{
    if (padIndex < 0 || padIndex >= NUM_PADS)
        return false;

    const DrumVoice* newestVoice = nullptr;

    for (const auto& voice : voices)
    {
        if (! voice.isActive || voice.padIndex != padIndex)
            continue;

        if (newestVoice == nullptr || voice.triggerSerial > newestVoice->triggerSerial)
            newestVoice = &voice;
    }

    if (newestVoice == nullptr)
        return false;

    outPosition = newestVoice->getPlaybackPositionNormalized();
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Per-layer feedback accessors
// ─────────────────────────────────────────────────────────────────────────────
float VoiceManager::consumeLayerTriggerLevel(int padIndex, int layerIndex) noexcept
{
    if (padIndex < 0 || padIndex >= NUM_PADS
        || layerIndex < 0 || layerIndex >= MAX_LAYERS_PER_PAD)
        return 0.0f;
    return layerTriggerLevels[static_cast<size_t>(padIndex)][static_cast<size_t>(layerIndex)]
        .exchange(0.0f, std::memory_order_relaxed);
}

float VoiceManager::getLayerLevel(int padIndex, int layerIndex) const noexcept
{
    if (padIndex < 0 || padIndex >= NUM_PADS
        || layerIndex < 0 || layerIndex >= MAX_LAYERS_PER_PAD)
        return 0.0f;
    return layerPeakLevels[static_cast<size_t>(padIndex)][static_cast<size_t>(layerIndex)];
}

float VoiceManager::getCompReductionDb(int padIndex, int layerIndex, int slotIndex) const noexcept
{
    if (padIndex < 0 || padIndex >= NUM_PADS
        || layerIndex < 0 || layerIndex >= MAX_LAYERS_PER_PAD
        || slotIndex < 0 || slotIndex >= static_cast<int>(MAX_LAYER_FX_SLOTS))
        return 0.0f;
    return compReductionDb[static_cast<size_t>(padIndex)][static_cast<size_t>(layerIndex)][static_cast<size_t>(slotIndex)];
}

// ─────────────────────────────────────────────────────────────────────────────
// 全ボイス停止
// ─────────────────────────────────────────────────────────────────────────────
void VoiceManager::allNotesOff()
{
    for (auto& v : voices)
        v.isActive = false;
}
