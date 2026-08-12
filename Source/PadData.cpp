#include "PadData.h"

// ═════════════════════════════════════════════════════════════════════════════
//  LayerData
// ═════════════════════════════════════════════════════════════════════════════

juce::ValueTree LayerData::toValueTree() const
{
    juce::ValueTree vt { "Layer" };

    vt.setProperty("sampleFileName",  sampleFileName,  nullptr);
    vt.setProperty("sampleFilePath",  sampleFilePath,  nullptr);
    vt.setProperty("sampleMissing",   sampleMissing,   nullptr);
    vt.setProperty("layerName",       layerName,       nullptr);

    vt.setProperty("volume",          volume,          nullptr);
    vt.setProperty("pan",             pan,             nullptr);
    vt.setProperty("pitch",           pitch,           nullptr);
    vt.setProperty("fine",            fine,            nullptr);

    vt.setProperty("attack",          attack,          nullptr);
    vt.setProperty("release",         release,         nullptr);

    vt.setProperty("startPosition",   startPosition,   nullptr);
    vt.setProperty("endPosition",     endPosition,     nullptr);
    vt.setProperty("fadeIn",          fadeIn,          nullptr);
    vt.setProperty("fadeOut",         fadeOut,         nullptr);

    vt.setProperty("reverse",         reverse,         nullptr);
    vt.setProperty("keepLength",      keepLength,      nullptr);
    vt.setProperty("smartTrim",       smartTrim,       nullptr);

    vt.setProperty("mute",            mute,            nullptr);
    vt.setProperty("solo",            solo,            nullptr);

    vt.setProperty("velocityMin",     velocityMin,     nullptr);
    vt.setProperty("velocityMax",     velocityMax,     nullptr);

    vt.setProperty("eqBypassed",      eq.bypassed,     nullptr);
    vt.setProperty("eqLowMode",       static_cast<int>(eq.lowMode),  nullptr);
    vt.setProperty("eqHighMode",      static_cast<int>(eq.highMode), nullptr);
    vt.setProperty("eqLowFreq",       eq.low.freq,     nullptr);
    vt.setProperty("eqLowGain",       eq.low.gain,     nullptr);
    vt.setProperty("eqLowQ",          eq.low.q,        nullptr);
    vt.setProperty("eqLowMidFreq",    eq.lowMid.freq,  nullptr);
    vt.setProperty("eqLowMidGain",    eq.lowMid.gain,  nullptr);
    vt.setProperty("eqLowMidQ",       eq.lowMid.q,     nullptr);
    vt.setProperty("eqHighMidFreq",   eq.highMid.freq, nullptr);
    vt.setProperty("eqHighMidGain",   eq.highMid.gain, nullptr);
    vt.setProperty("eqHighMidQ",      eq.highMid.q,    nullptr);
    vt.setProperty("eqHighFreq",      eq.high.freq,    nullptr);
    vt.setProperty("eqHighGain",      eq.high.gain,    nullptr);
    vt.setProperty("eqHighQ",         eq.high.q,       nullptr);

    juce::ValueTree fxTree { "FxChain" };
    for (const auto& slot : fxChain)
    {
        juce::ValueTree fx { "FxSlot" };
        fx.setProperty("type",     static_cast<int>(slot.type), nullptr);
        fx.setProperty("bypassed", slot.bypassed, nullptr);

        if (slot.type == LayerFxType::Eq)
        {
            fx.setProperty("eqBypassed",      slot.eq.bypassed,     nullptr);
            fx.setProperty("eqLowMode",       static_cast<int>(slot.eq.lowMode),  nullptr);
            fx.setProperty("eqHighMode",      static_cast<int>(slot.eq.highMode), nullptr);
            fx.setProperty("eqLowFreq",       slot.eq.low.freq,     nullptr);
            fx.setProperty("eqLowGain",       slot.eq.low.gain,     nullptr);
            fx.setProperty("eqLowQ",          slot.eq.low.q,        nullptr);
            fx.setProperty("eqLowMidFreq",    slot.eq.lowMid.freq,  nullptr);
            fx.setProperty("eqLowMidGain",    slot.eq.lowMid.gain,  nullptr);
            fx.setProperty("eqLowMidQ",       slot.eq.lowMid.q,     nullptr);
            fx.setProperty("eqHighMidFreq",   slot.eq.highMid.freq, nullptr);
            fx.setProperty("eqHighMidGain",   slot.eq.highMid.gain, nullptr);
            fx.setProperty("eqHighMidQ",      slot.eq.highMid.q,    nullptr);
            fx.setProperty("eqHighFreq",      slot.eq.high.freq,    nullptr);
            fx.setProperty("eqHighGain",      slot.eq.high.gain,    nullptr);
            fx.setProperty("eqHighQ",         slot.eq.high.q,       nullptr);
        }
        else if (slot.type == LayerFxType::Filter)
        {
            fx.setProperty("hpEnabled",   slot.filter.hpEnabled,   nullptr);
            fx.setProperty("hpCutoff",    slot.filter.hpCutoff,    nullptr);
            fx.setProperty("hpSlope",     slot.filter.hpSlope,     nullptr);
            fx.setProperty("hpResonance", slot.filter.hpResonance, nullptr);
            fx.setProperty("lpEnabled",   slot.filter.lpEnabled,   nullptr);
            fx.setProperty("lpCutoff",    slot.filter.lpCutoff,    nullptr);
            fx.setProperty("lpSlope",     slot.filter.lpSlope,     nullptr);
            fx.setProperty("lpResonance", slot.filter.lpResonance, nullptr);
        }
        else if (slot.type == LayerFxType::Drive)
        {
            fx.setProperty("driveType", slot.drive.type,   nullptr);
            fx.setProperty("amount", slot.drive.amount, nullptr);
            fx.setProperty("tone",   slot.drive.tone,   nullptr);
            fx.setProperty("mix",    slot.drive.mix,    nullptr);
            fx.setProperty("outputDb", slot.drive.outputDb, nullptr);
        }
        else if (slot.type == LayerFxType::Transient)
        {
            fx.setProperty("attack",  slot.transient.attack,  nullptr);
            fx.setProperty("sustain", slot.transient.sustain, nullptr);
            fx.setProperty("outputDb", slot.transient.outputDb, nullptr);
        }
        else if (slot.type == LayerFxType::Compressor)
        {
            fx.setProperty("threshold", slot.compressor.threshold, nullptr);
            fx.setProperty("ratio",     slot.compressor.ratio,     nullptr);
            fx.setProperty("attack",    slot.compressor.attack,    nullptr);
            fx.setProperty("release",   slot.compressor.release,   nullptr);
            fx.setProperty("makeupDb",  slot.compressor.makeupDb,  nullptr);
            fx.setProperty("mix",       slot.compressor.mix,       nullptr);
            fx.setProperty("outputDb",  slot.compressor.outputDb,  nullptr);
        }

        fxTree.addChild(fx, -1, nullptr);
    }
    vt.addChild(fxTree, -1, nullptr);

    return vt;
}

void LayerData::fromValueTree(const juce::ValueTree& vt)
{
    sampleFileName = vt.getProperty("sampleFileName", sampleFileName);
    sampleFilePath = vt.getProperty("sampleFilePath", sampleFilePath);
    sampleMissing  = vt.getProperty("sampleMissing",  sampleMissing);
    layerName      = vt.getProperty("layerName",      layerName);

    volume         = vt.getProperty("volume",         volume);
    pan            = vt.getProperty("pan",            pan);
    pitch          = vt.getProperty("pitch",          pitch);
    fine           = juce::jlimit(-100.0f, 100.0f,
                                  static_cast<float>(vt.getProperty("fine", 0.0f)));

    attack         = vt.getProperty("attack",         attack);
    release        = vt.getProperty("release",        release);

    startPosition  = vt.getProperty("startPosition",  startPosition);
    endPosition    = vt.getProperty("endPosition",    endPosition);
    fadeIn         = vt.getProperty("fadeIn",         fadeIn);
    fadeOut        = vt.getProperty("fadeOut",        fadeOut);

    endPosition    = juce::jlimit(0.001f, 1.0f, endPosition);
    startPosition  = juce::jlimit(0.0f, endPosition - 0.001f, startPosition);
    endPosition    = juce::jlimit(startPosition + 0.001f, 1.0f, endPosition);
    fadeIn         = juce::jlimit(0.0f, 1.0f, fadeIn);
    fadeOut        = juce::jlimit(0.0f, juce::jmax(0.0f, 1.0f - fadeIn), fadeOut);

    reverse        = vt.getProperty("reverse",        reverse);
    // Projects saved before KEEP LENGTH existed adopt the current ON default.
    keepLength     = vt.hasProperty("keepLength")
                   ? static_cast<bool>(vt.getProperty("keepLength"))
                   : true;
    smartTrim      = vt.getProperty("smartTrim",      smartTrim);

    mute           = vt.getProperty("mute",           mute);
    solo           = vt.getProperty("solo",           solo);

    velocityMin    = juce::jlimit(0, 127, (int) vt.getProperty("velocityMin", velocityMin));
    velocityMax    = juce::jlimit(velocityMin, 127, (int) vt.getProperty("velocityMax", velocityMax));

    eq.bypassed    = vt.getProperty("eqBypassed", eq.bypassed);
    eq.lowMode     = static_cast<LayerEqEdgeMode>(juce::jlimit(0, 1, (int) vt.getProperty("eqLowMode", static_cast<int>(eq.lowMode))));
    eq.highMode    = static_cast<LayerEqEdgeMode>(juce::jlimit(0, 1, (int) vt.getProperty("eqHighMode", static_cast<int>(eq.highMode))));
    eq.low.freq    = juce::jlimit(20.0f, 20000.0f, static_cast<float>(vt.getProperty("eqLowFreq", eq.low.freq)));
    eq.low.gain    = juce::jlimit(-18.0f, 18.0f, static_cast<float>(vt.getProperty("eqLowGain", eq.low.gain)));
    eq.low.q       = juce::jlimit(0.2f, 8.0f, static_cast<float>(vt.getProperty("eqLowQ", eq.low.q)));
    eq.lowMid.freq = juce::jlimit(20.0f, 20000.0f, static_cast<float>(vt.getProperty("eqLowMidFreq", eq.lowMid.freq)));
    eq.lowMid.gain = juce::jlimit(-18.0f, 18.0f, static_cast<float>(vt.getProperty("eqLowMidGain", eq.lowMid.gain)));
    eq.lowMid.q    = juce::jlimit(0.2f, 8.0f, static_cast<float>(vt.getProperty("eqLowMidQ", eq.lowMid.q)));
    eq.highMid.freq = juce::jlimit(20.0f, 20000.0f, static_cast<float>(vt.getProperty("eqHighMidFreq", eq.highMid.freq)));
    eq.highMid.gain = juce::jlimit(-18.0f, 18.0f, static_cast<float>(vt.getProperty("eqHighMidGain", eq.highMid.gain)));
    eq.highMid.q   = juce::jlimit(0.2f, 8.0f, static_cast<float>(vt.getProperty("eqHighMidQ", eq.highMid.q)));
    eq.high.freq   = juce::jlimit(20.0f, 20000.0f, static_cast<float>(vt.getProperty("eqHighFreq", eq.high.freq)));
    eq.high.gain   = juce::jlimit(-18.0f, 18.0f, static_cast<float>(vt.getProperty("eqHighGain", eq.high.gain)));
    eq.high.q      = juce::jlimit(0.2f, 8.0f, static_cast<float>(vt.getProperty("eqHighQ", eq.high.q)));

    fxChain.clear();
    if (auto fxTree = vt.getChildWithName("FxChain"); fxTree.isValid())
    {
        fxChain.reserve((size_t) fxTree.getNumChildren());
        for (int i = 0; i < fxTree.getNumChildren(); ++i)
        {
            const auto fx = fxTree.getChild(i);
            LayerFxSlot slot {};
            slot.type = static_cast<LayerFxType>(juce::jlimit(0, 4, (int) fx.getProperty("type", 0)));
            slot.bypassed = fx.getProperty("bypassed", false);

            if (slot.type == LayerFxType::Eq)
            {
                slot.eq.bypassed = fx.getProperty("eqBypassed", slot.eq.bypassed);
                slot.eq.lowMode = static_cast<LayerEqEdgeMode>(juce::jlimit(0, 1, (int) fx.getProperty("eqLowMode", static_cast<int>(slot.eq.lowMode))));
                slot.eq.highMode = static_cast<LayerEqEdgeMode>(juce::jlimit(0, 1, (int) fx.getProperty("eqHighMode", static_cast<int>(slot.eq.highMode))));
                slot.eq.low.freq = juce::jlimit(20.0f, 20000.0f, static_cast<float>(fx.getProperty("eqLowFreq", slot.eq.low.freq)));
                slot.eq.low.gain = juce::jlimit(-18.0f, 18.0f, static_cast<float>(fx.getProperty("eqLowGain", slot.eq.low.gain)));
                slot.eq.low.q = juce::jlimit(0.2f, 8.0f, static_cast<float>(fx.getProperty("eqLowQ", slot.eq.low.q)));
                slot.eq.lowMid.freq = juce::jlimit(20.0f, 20000.0f, static_cast<float>(fx.getProperty("eqLowMidFreq", slot.eq.lowMid.freq)));
                slot.eq.lowMid.gain = juce::jlimit(-18.0f, 18.0f, static_cast<float>(fx.getProperty("eqLowMidGain", slot.eq.lowMid.gain)));
                slot.eq.lowMid.q = juce::jlimit(0.2f, 8.0f, static_cast<float>(fx.getProperty("eqLowMidQ", slot.eq.lowMid.q)));
                slot.eq.highMid.freq = juce::jlimit(20.0f, 20000.0f, static_cast<float>(fx.getProperty("eqHighMidFreq", slot.eq.highMid.freq)));
                slot.eq.highMid.gain = juce::jlimit(-18.0f, 18.0f, static_cast<float>(fx.getProperty("eqHighMidGain", slot.eq.highMid.gain)));
                slot.eq.highMid.q = juce::jlimit(0.2f, 8.0f, static_cast<float>(fx.getProperty("eqHighMidQ", slot.eq.highMid.q)));
                slot.eq.high.freq = juce::jlimit(20.0f, 20000.0f, static_cast<float>(fx.getProperty("eqHighFreq", slot.eq.high.freq)));
                slot.eq.high.gain = juce::jlimit(-18.0f, 18.0f, static_cast<float>(fx.getProperty("eqHighGain", slot.eq.high.gain)));
                slot.eq.high.q = juce::jlimit(0.2f, 8.0f, static_cast<float>(fx.getProperty("eqHighQ", slot.eq.high.q)));
            }
            else if (slot.type == LayerFxType::Filter)
            {
                const auto clampSlope = [] (int s) { return s >= 48 ? 48 : s >= 24 ? 24 : 12; };

                if (fx.hasProperty("hpEnabled") || fx.hasProperty("lpEnabled"))
                {
                    slot.filter.hpEnabled = (bool) fx.getProperty("hpEnabled", slot.filter.hpEnabled);
                    slot.filter.lpEnabled = (bool) fx.getProperty("lpEnabled", slot.filter.lpEnabled);
                    slot.filter.hpCutoff  = juce::jlimit(20.0f, 20000.0f, static_cast<float>(fx.getProperty("hpCutoff", slot.filter.hpCutoff)));
                    slot.filter.lpCutoff  = juce::jlimit(20.0f, 20000.0f, static_cast<float>(fx.getProperty("lpCutoff", slot.filter.lpCutoff)));
                    // per-section があればそれ、無ければ旧 shared slope/resonance をフォールバック
                    const int sharedSlope = (int) fx.getProperty("slope", 12);
                    const float sharedRes = static_cast<float>(fx.getProperty("resonance", 0.7));
                    slot.filter.hpSlope     = clampSlope((int) fx.getProperty("hpSlope", sharedSlope));
                    slot.filter.lpSlope     = clampSlope((int) fx.getProperty("lpSlope", sharedSlope));
                    slot.filter.hpResonance = juce::jlimit(0.2f, 8.0f, static_cast<float>(fx.getProperty("hpResonance", sharedRes)));
                    slot.filter.lpResonance = juce::jlimit(0.2f, 8.0f, static_cast<float>(fx.getProperty("lpResonance", sharedRes)));
                }
                else
                {
                    // legacy (mode 0=HP / 1=LP, cutoff) からの移行
                    const int mode = juce::jlimit(0, 1, (int) fx.getProperty("mode", 0));
                    const float cutoff = juce::jlimit(20.0f, 20000.0f, static_cast<float>(fx.getProperty("cutoff", 80.0f)));
                    const float res = juce::jlimit(0.2f, 8.0f, static_cast<float>(fx.getProperty("resonance", 0.7)));
                    slot.filter.hpEnabled = (mode == 0);
                    slot.filter.lpEnabled = (mode == 1);
                    slot.filter.hpCutoff  = (mode == 0) ? cutoff : 80.0f;
                    slot.filter.lpCutoff  = (mode == 1) ? cutoff : 18000.0f;
                    slot.filter.hpSlope = slot.filter.lpSlope = 12;
                    slot.filter.hpResonance = slot.filter.lpResonance = res;
                }
            }
            else if (slot.type == LayerFxType::Drive)
            {
                slot.drive.type = juce::jlimit(0, 6, (int) fx.getProperty("driveType", slot.drive.type));
                slot.drive.amount = juce::jlimit(0.0f, 1.0f, static_cast<float>(fx.getProperty("amount", slot.drive.amount)));
                slot.drive.tone = juce::jlimit(0.0f, 1.0f, static_cast<float>(fx.getProperty("tone", slot.drive.tone)));
                slot.drive.mix = juce::jlimit(0.0f, 1.0f, static_cast<float>(fx.getProperty("mix", slot.drive.mix)));
                slot.drive.outputDb = juce::jlimit(-24.0f, 12.0f, static_cast<float>(fx.getProperty("outputDb", slot.drive.outputDb)));
            }
            else if (slot.type == LayerFxType::Transient)
            {
                slot.transient.attack = juce::jlimit(-1.0f, 1.0f, static_cast<float>(fx.getProperty("attack", slot.transient.attack)));
                slot.transient.sustain = juce::jlimit(-1.0f, 1.0f, static_cast<float>(fx.getProperty("sustain", slot.transient.sustain)));
                slot.transient.outputDb = juce::jlimit(-24.0f, 12.0f, static_cast<float>(fx.getProperty("outputDb", slot.transient.outputDb)));
            }
            else if (slot.type == LayerFxType::Compressor)
            {
                slot.compressor.threshold = juce::jlimit(-48.0f, 0.0f, static_cast<float>(fx.getProperty("threshold", slot.compressor.threshold)));
                slot.compressor.ratio = juce::jlimit(1.0f, 20.0f, static_cast<float>(fx.getProperty("ratio", slot.compressor.ratio)));
                slot.compressor.attack = juce::jlimit(1.0f, 80.0f, static_cast<float>(fx.getProperty("attack", slot.compressor.attack)));
                slot.compressor.release = juce::jlimit(10.0f, 500.0f, static_cast<float>(fx.getProperty("release", slot.compressor.release)));
                slot.compressor.makeupDb = juce::jlimit(0.0f, 24.0f, static_cast<float>(fx.getProperty("makeupDb", slot.compressor.makeupDb)));
                slot.compressor.mix = juce::jlimit(0.0f, 1.0f, static_cast<float>(fx.getProperty("mix", slot.compressor.mix)));
                slot.compressor.outputDb = juce::jlimit(-24.0f, 12.0f, static_cast<float>(fx.getProperty("outputDb", slot.compressor.outputDb)));
            }
            fxChain.push_back(slot);
        }
    }
}

// ═════════════════════════════════════════════════════════════════════════════
//  PadData ─ Layer 0 ⇄ flat fields の同期
//
//  Step 1 では「音声コード（VoiceManager 等）は引き続き PadData の flat fields を
//  読む」運用にしている。layers[0] は flat fields のミラーとして維持され、
//  保存/復元時に Layer 1+ と一緒にツリーへ書かれる。
//
//  Step 9 で flat fields の役割が縮小し、Layer 0 が音声側からも直接参照される。
// ═════════════════════════════════════════════════════════════════════════════

void PadData::syncLayer0FromFlat() noexcept
{
    if (layers.empty())
        layers.emplace_back();

    auto& L = layers[0];
    L.sampleFileName = sampleFileName;
    L.sampleFilePath = sampleFilePath;
    L.sampleMissing  = sampleMissing;
    // layerName は flat には存在しない → そのまま保持
    L.volume        = volume;
    L.pan           = pan;
    L.pitch         = pitch;
    L.fine          = fine;
    L.attack        = attack;
    L.release       = release;
    L.startPosition = startPosition;
    L.endPosition   = endPosition;
    L.fadeIn        = fadeIn;
    L.fadeOut       = fadeOut;
    L.reverse       = reverse;
    L.keepLength    = keepLength;
    // smartTrim, mute, solo, velocityMin/Max は flat 側に対応フィールドが
    // ないので Layer 側の値をそのまま保持。
}

void PadData::syncFlatFromLayer0() noexcept
{
    if (layers.empty())
    {
        layers.emplace_back();
        return;
    }
    const auto& L = layers[0];
    sampleFileName = L.sampleFileName;
    sampleFilePath = L.sampleFilePath;
    sampleMissing  = L.sampleMissing;
    volume        = L.volume;
    pan           = L.pan;
    pitch         = L.pitch;
    fine          = L.fine;
    attack        = L.attack;
    release       = L.release;
    startPosition = L.startPosition;
    endPosition   = L.endPosition;
    fadeIn        = L.fadeIn;
    fadeOut       = L.fadeOut;
    reverse       = L.reverse;
    keepLength    = L.keepLength;
}

// ═════════════════════════════════════════════════════════════════════════════
//  PadData ─ シリアライズ
//
//  互換ポリシー:
//   * 既存 flat properties は引き続き書き出す（旧バージョンでもサンプル/設定を
//     復元できる「ダウングレード冗長保存」）。Layer 0 の内容と一致させる。
//   * 加えて <Layers> 子ノードに全 Layer をシリアライズ。新バージョンはこちらを優先。
//   * 読み込み時は <Layers> があればそれを採用し、その後 flat fields を
//     Layer 0 と同期させる。<Layers> が無い旧キット → flat fields → Layer 0 ミラー。
// ═════════════════════════════════════════════════════════════════════════════

juce::ValueTree PadData::toValueTree() const
{
    juce::ValueTree vt { "Pad" };

    // ── Pad-level プロパティ ───────────────────────────────────────────────
    vt.setProperty("padName",         padName,                       nullptr);
    vt.setProperty("padColourARGB",   static_cast<int>(padColourARGB), nullptr);
    vt.setProperty("padColourMode",   static_cast<int>(padColourMode), nullptr);
    vt.setProperty("midiNote",        midiNote,                      nullptr);
    vt.setProperty("playbackMode",    static_cast<int>(playbackMode), nullptr);
    vt.setProperty("chokeGroup",      chokeGroup,                    nullptr);
    vt.setProperty("mute",            mute,                          nullptr);
    vt.setProperty("solo",            solo,                          nullptr);
    vt.setProperty("outputAssign",    outputAssign,                  nullptr);
    vt.setProperty("swapLR",          swapLR,                        nullptr);
    vt.setProperty("velocitySens",    velocitySens,                  nullptr);
    vt.setProperty("humanize",        humanize,                      nullptr);

    // Polyphony / Voice Steal (v7+)
    vt.setProperty("polyphony",       polyphony,                     nullptr);
    vt.setProperty("voiceSteal",      static_cast<int>(voiceSteal),  nullptr);

    // VEL Curve（v7+）。古いバージョンは無視 → fromValueTree 側で Linear デフォルトに戻る。
    vt.setProperty("velCurvePreset",  velCurve.presetIndex,          nullptr);
    vt.setProperty("velCurveP1X",     velCurve.p1x,                  nullptr);
    vt.setProperty("velCurveP1Y",     velCurve.p1y,                  nullptr);
    vt.setProperty("velCurveP2X",     velCurve.p2x,                  nullptr);
    vt.setProperty("velCurveP2Y",     velCurve.p2y,                  nullptr);

    // Pad-level Vol/Pan/Pitch/Fine。古いバージョンは無視する。
    vt.setProperty("padVolume",       padVolume,                     nullptr);
    vt.setProperty("padPan",          padPan,                        nullptr);
    vt.setProperty("padPitch",        padPitch,                      nullptr);
    vt.setProperty("padFine",         padFine,                       nullptr);

    // ── Legacy flat fields（ダウングレード互換のためミラー保存） ─────────────
    //   Layer 0 と内容が一致していることを保証してから書き出す。
    vt.setProperty("sampleFileName",  sampleFileName,  nullptr);
    vt.setProperty("sampleFilePath",  sampleFilePath,  nullptr);
    vt.setProperty("sampleMissing",   sampleMissing,   nullptr);
    vt.setProperty("volume",          volume,          nullptr);
    vt.setProperty("pan",             pan,             nullptr);
    vt.setProperty("pitch",           pitch,           nullptr);
    vt.setProperty("fine",            fine,            nullptr);
    vt.setProperty("attack",          attack,          nullptr);
    vt.setProperty("release",         release,         nullptr);
    vt.setProperty("startPosition",   startPosition,   nullptr);
    vt.setProperty("endPosition",     endPosition,     nullptr);
    vt.setProperty("fadeIn",          fadeIn,          nullptr);
    vt.setProperty("fadeOut",         fadeOut,         nullptr);
    vt.setProperty("reverse",         reverse,         nullptr);
    vt.setProperty("keepLength",      keepLength,      nullptr);

    // ── Layers（新形式） ──────────────────────────────────────────────────
    juce::ValueTree layersTree { "Layers" };
    for (const auto& L : layers)
        layersTree.addChild(L.toValueTree(), -1, nullptr);
    vt.addChild(layersTree, -1, nullptr);

    return vt;
}

void PadData::fromValueTree(const juce::ValueTree& vt)
{
    // ── Pad-level プロパティ ───────────────────────────────────────────────
    padName        = vt.getProperty("padName",        padName);
    padColourARGB  = static_cast<juce::uint32>((int) vt.getProperty("padColourARGB", static_cast<int>(padColourARGB)));
    padColourMode  = static_cast<PadColourMode>((int) vt.getProperty("padColourMode", static_cast<int>(PadColourMode::Auto)));
    midiNote       = vt.getProperty("midiNote",       midiNote);
    playbackMode   = static_cast<PlaybackMode>((int) vt.getProperty("playbackMode", (int) playbackMode));
    chokeGroup     = vt.getProperty("chokeGroup",     chokeGroup);
    mute           = vt.getProperty("mute",           mute);
    solo           = vt.getProperty("solo",           solo);
    outputAssign   = vt.getProperty("outputAssign",   outputAssign);
    swapLR         = vt.getProperty("swapLR",         false);
    velocitySens   = juce::jlimit(0.0f, 1.0f, static_cast<float>(vt.getProperty("velocitySens", velocitySens)));
    humanize       = juce::jlimit(0.0f, 1.0f, static_cast<float>(vt.getProperty("humanize", 0.0f)));

    // Polyphony / Voice Steal (v7+)
    polyphony  = juce::jlimit(0, 16, (int) vt.getProperty("polyphony", polyphony));
    voiceSteal = static_cast<VoiceStealMode>(
        juce::jlimit(0, 2, (int) vt.getProperty("voiceSteal", static_cast<int>(voiceSteal))));

    // VEL Curve（v7+）。プロパティが無ければデフォルト = Linear を維持。
    velCurve.presetIndex = juce::jlimit(0, 5, (int) vt.getProperty("velCurvePreset", (int) velCurve.presetIndex));
    velCurve.p1x = juce::jlimit(0.0f, 1.0f, static_cast<float>(vt.getProperty("velCurveP1X", velCurve.p1x)));
    velCurve.p1y = juce::jlimit(0.0f, 1.0f, static_cast<float>(vt.getProperty("velCurveP1Y", velCurve.p1y)));
    velCurve.p2x = juce::jlimit(0.0f, 1.0f, static_cast<float>(vt.getProperty("velCurveP2X", velCurve.p2x)));
    velCurve.p2y = juce::jlimit(0.0f, 1.0f, static_cast<float>(vt.getProperty("velCurveP2Y", velCurve.p2y)));
    // 安全: p1.x < p2.x を保証
    if (velCurve.p1x > velCurve.p2x) std::swap(velCurve.p1x, velCurve.p2x);

    // Pad-level Vol/Pan/Pitch/Fine（無ければ unity / center / 0 で互換）
    padVolume      = juce::jlimit(0.0f, 1.0f, static_cast<float>(vt.getProperty("padVolume", 0.75f)));
    padPan         = juce::jlimit(-1.0f, 1.0f, static_cast<float>(vt.getProperty("padPan",    0.0f)));
    padPitch       = juce::jlimit(-24.0f, 24.0f, static_cast<float>(vt.getProperty("padPitch", 0.0f)));
    padFine        = juce::jlimit(-100.0f, 100.0f, static_cast<float>(vt.getProperty("padFine", 0.0f)));

    // ── Legacy flat fields ────────────────────────────────────────────────
    sampleFileName = vt.getProperty("sampleFileName", sampleFileName);
    sampleFilePath = vt.getProperty("sampleFilePath", sampleFilePath);
    sampleMissing  = vt.getProperty("sampleMissing",  sampleMissing);
    volume         = vt.getProperty("volume",         volume);
    pan            = vt.getProperty("pan",            pan);
    pitch          = vt.getProperty("pitch",          pitch);
    fine           = juce::jlimit(-100.0f, 100.0f,
                                  static_cast<float>(vt.getProperty("fine", 0.0f)));
    attack         = vt.getProperty("attack",         attack);
    release        = vt.getProperty("release",        release);
    startPosition  = vt.getProperty("startPosition",  startPosition);
    endPosition    = vt.getProperty("endPosition",    endPosition);
    fadeIn         = vt.getProperty("fadeIn",         fadeIn);
    fadeOut        = vt.getProperty("fadeOut",        fadeOut);
    endPosition    = juce::jlimit(0.001f, 1.0f, endPosition);
    startPosition  = juce::jlimit(0.0f, endPosition - 0.001f, startPosition);
    endPosition    = juce::jlimit(startPosition + 0.001f, 1.0f, endPosition);
    fadeIn         = juce::jlimit(0.0f, 1.0f, fadeIn);
    fadeOut        = juce::jlimit(0.0f, juce::jmax(0.0f, 1.0f - fadeIn), fadeOut);
    reverse        = vt.getProperty("reverse",        reverse);
    keepLength     = vt.hasProperty("keepLength")
                   ? static_cast<bool>(vt.getProperty("keepLength"))
                   : true;

    // ── Layers（新形式が優先。無ければ flat fields からの自動マイグレーション） ─
    layers.clear();
    if (auto layersTree = vt.getChildWithName("Layers"); layersTree.isValid())
    {
        const int n = juce::jmin(layersTree.getNumChildren(), MAX_LAYERS_PER_PAD);
        for (int i = 0; i < n; ++i)
        {
            const auto child = layersTree.getChild(i);
            if (! child.hasType("Layer")) continue;
            LayerData L;
            L.fromValueTree(child);
            layers.push_back(L);
        }
    }

    if (layers.empty())
    {
        // 旧キット（v <= 5）: flat fields のみ → Layer 0 として再構築
        layers.emplace_back();
        syncLayer0FromFlat();
    }
    else
    {
        // 新形式: Layer 0 を flat fields に反映して両者一致を保つ
        syncFlatFromLayer0();
    }
}
