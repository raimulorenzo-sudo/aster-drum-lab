#include "PadDataJson.h"

namespace PadDataJson
{
    static const char* playbackModeName(PlaybackMode m)
    {
        return (m == PlaybackMode::OneShot) ? "OneShot" : "Gate";
    }

    juce::var padToVar(const PadData& pad)
    {
        auto* obj = new juce::DynamicObject();

        // 名前管理（重要: padName と sampleFileName は別物）
        obj->setProperty("padName",        pad.padName);
        obj->setProperty("sampleFileName", pad.sampleFileName);
        obj->setProperty("sampleFilePath", pad.sampleFilePath);
        obj->setProperty("sampleMissing",  pad.sampleMissing);
        obj->setProperty("hasSample",       pad.hasSample());
        obj->setProperty("padColourARGB",  (juce::int64) pad.padColourARGB);
        obj->setProperty("padColourMode",  (int) pad.padColourMode);

        // MIDI
        obj->setProperty("midiNote", pad.midiNote);

        // 音量・パン・ピッチ
        obj->setProperty("volume", (double) pad.volume);
        obj->setProperty("pan",    (double) pad.pan);
        obj->setProperty("pitch",  (double) pad.pitch);

        // エンベロープ
        obj->setProperty("attack",  (double) pad.attack);
        obj->setProperty("release", (double) pad.release);

        // トリム
        obj->setProperty("startPosition", (double) pad.startPosition);
        obj->setProperty("endPosition",   (double) pad.endPosition);
        obj->setProperty("fadeIn",        (double) pad.fadeIn);
        obj->setProperty("fadeOut",       (double) pad.fadeOut);

        // 再生
        obj->setProperty("reverse",       pad.reverse);
        obj->setProperty("playbackMode",  juce::String(playbackModeName(pad.playbackMode)));
        obj->setProperty("chokeGroup",    pad.chokeGroup);

        // ミキサー
        obj->setProperty("mute",         pad.mute);
        obj->setProperty("solo",         pad.solo);
        obj->setProperty("outputAssign", pad.outputAssign);

        // パフォーマンス
        obj->setProperty("velocitySens", (double) pad.velocitySens);
        obj->setProperty("humanize",     (double) pad.humanize);

        // Polyphony / Voice Steal (v7+)
        obj->setProperty("polyphony", pad.polyphony);
        {
            const char* vsName = "oldest";
            switch (pad.voiceSteal)
            {
                case PadData::VoiceStealMode::Oldest:   vsName = "oldest";   break;
                case PadData::VoiceStealMode::Quietest: vsName = "quietest"; break;
                case PadData::VoiceStealMode::Off:      vsName = "off";      break;
            }
            obj->setProperty("voiceSteal", juce::String(vsName));
        }

        // VEL Curve（v7+） — 5 値を 1 オブジェクトにまとめて送る
        {
            auto* vc = new juce::DynamicObject();
            vc->setProperty("preset", pad.velCurve.presetIndex);
            vc->setProperty("p1x",    (double) pad.velCurve.p1x);
            vc->setProperty("p1y",    (double) pad.velCurve.p1y);
            vc->setProperty("p2x",    (double) pad.velCurve.p2x);
            vc->setProperty("p2y",    (double) pad.velCurve.p2y);
            obj->setProperty("velCurve", juce::var(vc));
        }

        // Pad-level Vol / Pan / Pitch
        obj->setProperty("padVolume",    (double) pad.padVolume);
        obj->setProperty("padPan",       (double) pad.padPan);
        obj->setProperty("padPitch",     (double) pad.padPitch);

        // ── Layers ─────────────────────────────────────────────────────────
        juce::Array<juce::var> layerArr;
        layerArr.ensureStorageAllocated((int) pad.layers.size());
        for (const auto& L : pad.layers)
        {
            auto* lo = new juce::DynamicObject();
            lo->setProperty("sampleFileName", L.sampleFileName);
            lo->setProperty("sampleFilePath", L.sampleFilePath);
            lo->setProperty("sampleMissing",  L.sampleMissing);
            lo->setProperty("layerName",      L.layerName);
            lo->setProperty("volume",  (double) L.volume);
            lo->setProperty("pan",     (double) L.pan);
            lo->setProperty("pitch",   (double) L.pitch);
            lo->setProperty("attack",  (double) L.attack);
            lo->setProperty("release", (double) L.release);
            lo->setProperty("startPosition", (double) L.startPosition);
            lo->setProperty("endPosition",   (double) L.endPosition);
            lo->setProperty("fadeIn",        (double) L.fadeIn);
            lo->setProperty("fadeOut",       (double) L.fadeOut);
            lo->setProperty("reverse",   L.reverse);
            lo->setProperty("smartTrim", L.smartTrim);
            lo->setProperty("mute",      L.mute);
            lo->setProperty("solo",      L.solo);
            lo->setProperty("velocityMin", L.velocityMin);
            lo->setProperty("velocityMax", L.velocityMax);

            auto* eq = new juce::DynamicObject();
            eq->setProperty("bypassed", (bool) L.eq.bypassed);
            eq->setProperty("lowMode",  (int) L.eq.lowMode);
            eq->setProperty("highMode", (int) L.eq.highMode);

            auto addBand = [] (juce::DynamicObject* owner, const char* name, const LayerEqBand& band)
            {
                auto* b = new juce::DynamicObject();
                b->setProperty("freq", (double) band.freq);
                b->setProperty("gain", (double) band.gain);
                b->setProperty("q",    (double) band.q);
                owner->setProperty(name, juce::var(b));
            };
            addBand(eq, "low",     L.eq.low);
            addBand(eq, "lowMid",  L.eq.lowMid);
            addBand(eq, "highMid", L.eq.highMid);
            addBand(eq, "high",    L.eq.high);
            lo->setProperty("eq", juce::var(eq));

            juce::Array<juce::var> fxArr;
            fxArr.ensureStorageAllocated((int) L.fxChain.size());
            for (const auto& slot : L.fxChain)
            {
                auto* fx = new juce::DynamicObject();
                const char* typeName = "EQ";
                if (slot.type == LayerFxType::Filter) typeName = "FILTER";
                else if (slot.type == LayerFxType::Drive) typeName = "DRIVE";
                else if (slot.type == LayerFxType::Transient) typeName = "TRANSIENT";
                else if (slot.type == LayerFxType::Compressor) typeName = "COMPRESSOR";
                fx->setProperty("type", juce::String(typeName));
                fx->setProperty("bypassed", slot.bypassed);

                auto* params = new juce::DynamicObject();
                if (slot.type == LayerFxType::Eq)
                {
                    params->setProperty("bypassed", slot.eq.bypassed);
                    params->setProperty("lowMode",  slot.eq.lowMode == LayerEqEdgeMode::Cut ? "cut" : "shelf");
                    params->setProperty("highMode", slot.eq.highMode == LayerEqEdgeMode::Cut ? "cut" : "shelf");
                    addBand(params, "low",     slot.eq.low);
                    addBand(params, "lowMid",  slot.eq.lowMid);
                    addBand(params, "highMid", slot.eq.highMid);
                    addBand(params, "high",    slot.eq.high);
                }
                else if (slot.type == LayerFxType::Filter)
                {
                    params->setProperty("hpEnabled", slot.filter.hpEnabled);
                    params->setProperty("hpCutoff", (double) slot.filter.hpCutoff);
                    params->setProperty("hpSlope", slot.filter.hpSlope);
                    params->setProperty("hpResonance", (double) slot.filter.hpResonance);
                    params->setProperty("lpEnabled", slot.filter.lpEnabled);
                    params->setProperty("lpCutoff", (double) slot.filter.lpCutoff);
                    params->setProperty("lpSlope", slot.filter.lpSlope);
                    params->setProperty("lpResonance", (double) slot.filter.lpResonance);
                }
                else if (slot.type == LayerFxType::Drive)
                {
                    static const char* driveTypes[] { "SOFT_CLIP", "HARD_CLIP", "TUBE", "TAPE", "FOLD", "BIT_CRUSH", "DOWNSAMPLE" };
                    params->setProperty("type", juce::String(driveTypes[juce::jlimit(0, 6, slot.drive.type)]));
                    params->setProperty("amount", (double) slot.drive.amount);
                    params->setProperty("tone",   (double) slot.drive.tone);
                    params->setProperty("mix",    (double) slot.drive.mix);
                    params->setProperty("output", (double) slot.drive.outputDb);
                }
                else if (slot.type == LayerFxType::Transient)
                {
                    params->setProperty("attack",  (double) slot.transient.attack);
                    params->setProperty("sustain", (double) slot.transient.sustain);
                }
                else if (slot.type == LayerFxType::Compressor)
                {
                    params->setProperty("threshold", (double) slot.compressor.threshold);
                    params->setProperty("ratio",     (double) slot.compressor.ratio);
                    params->setProperty("attack",    (double) slot.compressor.attack);
                    params->setProperty("release",   (double) slot.compressor.release);
                    params->setProperty("mix",       (double) slot.compressor.mix);
                }
                fx->setProperty("params", juce::var(params));
                fxArr.add(juce::var(fx));
            }
            lo->setProperty("fxChain", fxArr);

            layerArr.add(juce::var(lo));
        }
        obj->setProperty("layers", layerArr);

        return juce::var(obj);
    }

    juce::var kitToVar(const KitData& kit,
                       int selectedIndex,
                       int currentPage,
                       const juce::String& kitName)
    {
        auto* obj = new juce::DynamicObject();

        juce::Array<juce::var> padArr;
        padArr.ensureStorageAllocated(NUM_PADS);
        for (const auto& pad : kit.pads)
            padArr.add(padToVar(pad));

        obj->setProperty("pads", padArr);
        obj->setProperty("page", currentPage);
        obj->setProperty("selectedIndex", selectedIndex);
        obj->setProperty("kitName", kitName.isEmpty() ? kit.kitName : kitName);

        // OutputMode を文字列化
        const char* modeStr = "48Outs";
        switch (kit.outputMode)
        {
            case OutputMode::Stereo: modeStr = "Stereo"; break;
            case OutputMode::Outs16: modeStr = "16Outs"; break;
            case OutputMode::Outs32: modeStr = "32Outs"; break;
            case OutputMode::Outs48: modeStr = "48Outs"; break;
        }
        obj->setProperty("outputMode", juce::String(modeStr));

        return juce::var(obj);
    }

    juce::String varToJson(const juce::var& v)
    {
        return juce::JSON::toString(v, true /* allOnOneLine */);
    }
}
