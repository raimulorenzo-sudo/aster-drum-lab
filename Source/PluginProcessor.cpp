#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "WebViewEditor.h"
#include "SmartTrim.h"
#include "FaderCurve.h"
#include "LayerParameterSpecs.h"

namespace
{
    constexpr float kMinTrimGap = 0.001f;

    // マスター出力ボリュームの APVTS パラメータ ID。Pad/Layer のように index を持たない
    // 単一グローバルパラメータ。
    constexpr const char* kMasterVolumeParamId = "masterVolume";
    constexpr const char* kAutomationSlotPrefix = "asterAutomationSlot";
    const juce::Identifier kAutomationSlotsStateId { "AUTOMATION_SLOTS" };

    juce::NormalisableRange<float> rangeFor(const PadParameterSpecs::Spec& spec)
    {
        return { spec.minValue, spec.maxValue };
    }

    juce::NormalisableRange<float> rangeFor(const LayerParameterSpecs::Spec& spec)
    {
        return { spec.minValue, spec.maxValue };
    }

    struct NormalizedTrim
    {
        float start { 0.0f };
        float end { 1.0f };
        float fadeIn { 0.0f };
        float fadeOut { 0.0f };
    };

    NormalizedTrim normalizeTrim(float start, float end, float fadeIn, float fadeOut)
    {
        NormalizedTrim result;
        result.end = juce::jlimit(kMinTrimGap, 1.0f, end);
        result.start = juce::jlimit(0.0f, result.end - kMinTrimGap, start);
        result.end = juce::jlimit(result.start + kMinTrimGap, 1.0f, result.end);
        result.fadeIn = juce::jlimit(0.0f, 1.0f, fadeIn);
        result.fadeOut = juce::jlimit(0.0f, juce::jmax(0.0f, 1.0f - result.fadeIn), fadeOut);
        return result;
    }

    constexpr float kAutoTrimFadeInMs  = 0.0f;
    constexpr float kAutoTrimFadeOutMs = 5.0f;
    constexpr int kMaxRecentKits = 10;

    // Bumped to 2 when pad.volume changed from "linear gain (max 0 dB)" to
    // "fader position (0.75 = 0 dB, 1.0 = +12 dB)". Older states are
    // migrated in setStateInformation().
    constexpr int kCurrentStateVersion = 2;
    const juce::Identifier kStateVersionId { "stateVersion" };

    const juce::String kKitFileExtension { ".asterkit" };
    const juce::String kKitFormatName { "ASTERKit" };
    const juce::String kAppDataFolderName { "ASTER Drum Lab" };
    const juce::String kLegacyAppDataFolderName { "ASTER Drum Machine" };

    juce::File getAppDataDirectory()
    {
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile(kAppDataFolderName);
    }

    juce::File getLegacyAppDataDirectory()
    {
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile(kLegacyAppDataFolderName);
    }

    void copyLegacyAppDataIfNeeded()
    {
        auto current = getAppDataDirectory();
        const auto legacy = getLegacyAppDataDirectory();
        if (! current.exists() && legacy.exists())
            legacy.copyDirectoryTo(current);
    }

    juce::var valueTreeToJsonVar(const juce::ValueTree& tree)
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty("type", tree.getType().toString());

        auto* props = new juce::DynamicObject();
        for (int i = 0; i < tree.getNumProperties(); ++i)
        {
            const auto name = tree.getPropertyName(i);
            props->setProperty(name, tree.getProperty(name));
        }
        obj->setProperty("properties", juce::var(props));

        juce::Array<juce::var> children;
        children.ensureStorageAllocated(tree.getNumChildren());
        for (int i = 0; i < tree.getNumChildren(); ++i)
            children.add(valueTreeToJsonVar(tree.getChild(i)));

        obj->setProperty("children", children);
        return juce::var(obj);
    }

    juce::ValueTree jsonVarToValueTree(const juce::var& value)
    {
        if (! value.isObject())
            return {};

        const auto typeName = value.getProperty("type", juce::var()).toString();
        if (typeName.isEmpty())
            return {};

        juce::ValueTree tree { juce::Identifier(typeName) };

        if (const auto* props = value.getProperty("properties", juce::var()).getDynamicObject())
        {
            const auto& properties = props->getProperties();
            for (int i = 0; i < properties.size(); ++i)
                tree.setProperty(properties.getName(i), properties.getValueAt(i), nullptr);
        }

        if (const auto* children = value.getProperty("children", juce::var()).getArray())
        {
            for (const auto& child : *children)
            {
                auto childTree = jsonVarToValueTree(child);
                if (childTree.isValid())
                    tree.addChild(childTree, -1, nullptr);
            }
        }

        return tree;
    }

    juce::var makeAsterKitJson(const KitData& kit, const juce::String& kitName)
    {
        auto tree = kit.toValueTree();
        tree.setProperty("kitName", kitName, nullptr);

        auto* obj = new juce::DynamicObject();
        obj->setProperty("format", kKitFormatName);
        obj->setProperty("formatVersion", 1);
        obj->setProperty("pluginVersion", JucePlugin_VersionString);
        obj->setProperty("kit", valueTreeToJsonVar(tree));
        return juce::var(obj);
    }

    juce::ValueTree readKitTreeFromFile(const juce::File& file)
    {
        if (! file.existsAsFile())
            return {};

        const auto extension = file.getFileExtension();
        if (extension.equalsIgnoreCase(kKitFileExtension))
        {
            const auto json = juce::JSON::parse(file.loadFileAsString());
            if (json.isVoid())
                return {};

            const auto kitJson = json.getProperty("kit", juce::var());
            const auto tree = jsonVarToValueTree(kitJson);
            return tree.hasType("Kit") ? tree : tree.getChildWithName("Kit");
        }

        juce::FileInputStream stream(file);
        if (! stream.openedOk())
            return {};

        const auto tree = juce::ValueTree::readFromStream(stream);
        return tree.hasType("Kit") ? tree : tree.getChildWithName("Kit");
    }

    void mergeLoadedKit(KitData& target,
                        const KitData& loaded,
                        const DrumSamplerAudioProcessor::KitLoadOptions& options)
    {
        target.kitVersion = loaded.kitVersion;
        target.pluginVersion = loaded.pluginVersion;

        target.smartTrimOnSampleLoad = loaded.smartTrimOnSampleLoad;
        target.autoFadeOnTrim = loaded.autoFadeOnTrim;
        target.previewOnPadClick = loaded.previewOnPadClick;
        target.preservePadNameOnSampleLoad = loaded.preservePadNameOnSampleLoad;
        target.outputNameFollowsPadName = loaded.outputNameFollowsPadName;
        target.defaultOutputMode = loaded.defaultOutputMode;
        target.defaultFadeOutMs = loaded.defaultFadeOutMs;

        if (options.routing)
        {
            target.outputMode = loaded.outputMode;
            target.outputs = loaded.outputs;
        }

        for (int i = 0; i < NUM_PADS; ++i)
        {
            auto& dst = target.pads[static_cast<size_t>(i)];
            const auto& src = loaded.pads[static_cast<size_t>(i)];
            const auto keepMidiNote = dst.midiNote;
            const bool mergeLayers = options.samples
                                  || options.padNamesAndColours
                                  || options.padParameters
                                  || options.mixerSettings;

            if (mergeLayers)
            {
                if (dst.layers.empty())
                    dst.layers.emplace_back();

                const auto targetLayerCount = options.samples
                    ? src.layerCount()
                    : juce::jmin(dst.layerCount(), src.layerCount());

                while (dst.layerCount() < targetLayerCount)
                    dst.layers.emplace_back();
                while (dst.layerCount() > juce::jmax(1, targetLayerCount))
                    dst.layers.pop_back();

                for (int li = 0; li < juce::jmin(dst.layerCount(), src.layerCount()); ++li)
                {
                    auto& dL = dst.layers[static_cast<size_t>(li)];
                    const auto& sL = src.layers[static_cast<size_t>(li)];

                    if (options.samples)
                    {
                        dL.sampleFileName = sL.sampleFileName;
                        dL.sampleFilePath = sL.sampleFilePath;
                        dL.sampleMissing = sL.sampleMissing;
                    }

                    if (options.padNamesAndColours)
                        dL.layerName = sL.layerName;

                    if (options.padParameters)
                    {
                        dL.pitch = sL.pitch;
                        dL.fine = sL.fine;
                        dL.attack = sL.attack;
                        dL.release = sL.release;
                        dL.startPosition = sL.startPosition;
                        dL.endPosition = sL.endPosition;
                        dL.fadeIn = sL.fadeIn;
                        dL.fadeOut = sL.fadeOut;
                        dL.reverse = sL.reverse;
                        dL.keepLength = sL.keepLength;
                        dL.smartTrim = sL.smartTrim;
                        dL.velocityMin = sL.velocityMin;
                        dL.velocityMax = sL.velocityMax;
                        dL.eq = sL.eq;
                    }

                    if (options.mixerSettings)
                    {
                        dL.volume = sL.volume;
                        dL.pan = sL.pan;
                        dL.mute = sL.mute;
                        dL.solo = sL.solo;
                    }
                }
            }

            if (options.samples)
            {
                dst.sampleFileName = src.sampleFileName;
                dst.sampleFilePath = src.sampleFilePath;
                dst.sampleMissing = src.sampleMissing;
            }

            if (options.padNamesAndColours)
            {
                dst.padName = src.padName;
                dst.padColourARGB = src.padColourARGB;
                dst.padColourMode = src.padColourMode;
            }

            if (options.padParameters)
            {
                dst.pitch = src.pitch;
                dst.fine = src.fine;
                dst.padPitch = src.padPitch;
                dst.padFine = src.padFine;
                dst.attack = src.attack;
                dst.release = src.release;
                dst.startPosition = src.startPosition;
                dst.endPosition = src.endPosition;
                dst.fadeIn = src.fadeIn;
                dst.fadeOut = src.fadeOut;
                dst.reverse = src.reverse;
                dst.keepLength = src.keepLength;
                dst.playbackMode = src.playbackMode;
                dst.chokeGroup = src.chokeGroup;
                dst.velocitySens = src.velocitySens;
                dst.humanize = src.humanize;
                dst.velCurve = src.velCurve;
            }

            if (options.mixerSettings)
            {
                dst.volume = src.volume;
                dst.pan = src.pan;
                dst.padVolume = src.padVolume;
                dst.padPan = src.padPan;
                dst.swapLR = src.swapLR;
                dst.mute = src.mute;
                dst.solo = src.solo;
            }

            if (options.routing)
                dst.outputAssign = src.outputAssign;

            dst.midiNote = keepMidiNote;
            dst.syncFlatFromLayer0();
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// バスレイアウトの構築（48 ステレオアウト）
//
//   Bus 0       : "Main / Out 1" （常に有効）
//   Bus 1〜47   : "Out 2" 〜 "Out 48" （DAW で個別に有効化/無効化可能、初期は無効）
//
// 注: バス名は DAW のミキサーに表示される静的な名前です。
//     プラグイン内部の「Output Name（Pad 名追従）」は別管理で、
//     Mixer タブの UI に表示されます。DAW のトラック名を動的に変える保証はしません。
// ─────────────────────────────────────────────────────────────────────────────
juce::AudioProcessor::BusesProperties DrumSamplerAudioProcessor::buildBuses()
{
    BusesProperties b;
    b = b.withOutput("Main / Out 1", juce::AudioChannelSet::stereo(), true);

    for (int i = 2; i <= NUM_OUTPUTS; ++i)
    {
        // 2 つ目以降のバスは初期状態では「無効」にする
        // → 軽量モードで起動し、ユーザーが必要に応じて DAW で有効化する
        b = b.withOutput("Out " + juce::String(i),
                         juce::AudioChannelSet::stereo(),
                         false);
    }
    return b;
}

// ─────────────────────────────────────────────────────────────────────────────
// コンストラクタ
// ─────────────────────────────────────────────────────────────────────────────
DrumSamplerAudioProcessor::DrumSamplerAudioProcessor()
    : AudioProcessor(buildBuses()),
      parameters(*this, nullptr, "PARAMETERS", createParameterLayout())
{
    for (auto& targetIndex : automationSlotTargetIndices)
        targetIndex.store(-1, std::memory_order_relaxed);
    for (auto& targetCode : automationSlotFxTargetCodes)
        targetCode.store(-1, std::memory_order_relaxed);
    for (auto& value : automationSlotFxValues)
        value.store(0.0f, std::memory_order_relaxed);
    for (auto& dirty : automationSlotFxDirty)
        dirty.store(false, std::memory_order_relaxed);
    midiNoteTopad.fill(-1);
    rebuildMidiMap();
    syncParametersFromKit();
    registerParameterListeners();
    loadRecentKitPaths();
    clearKitDirty();
}

// ─────────────────────────────────────────────────────────────────────────────
// isBusesLayoutSupported  ─  DAW がレイアウト変更を試みたとき呼ばれる
//   - 入力なし（IS_SYNTH）
//   - メイン出力は必ずステレオ
//   - 各 Aux 出力はステレオ or 無効
// ─────────────────────────────────────────────────────────────────────────────
bool DrumSamplerAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    // 入力バスはすべて無効でなければならない
    for (auto i = 0; i < layouts.inputBuses.size(); ++i)
        if (! layouts.inputBuses.getReference(i).isDisabled())
            return false;

    // メイン出力（バス 0）は必ずステレオ
    if (layouts.outputBuses.size() < 1) return false;
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // 残りのバスはステレオ or 無効のみ許容
    for (auto i = 1; i < layouts.outputBuses.size(); ++i)
    {
        const auto& set = layouts.outputBuses.getReference(i);
        if (! set.isDisabled() && set != juce::AudioChannelSet::stereo())
            return false;
    }

    return true;
}

DrumSamplerAudioProcessor::~DrumSamplerAudioProcessor()
{
    removeParameterListeners();
}

// ─────────────────────────────────────────────────────────────────────────────
// MIDI ノート → パッドインデックスのテーブルを構築
// kit.pads[i].midiNote を変更したあとに呼ぶこと
// ─────────────────────────────────────────────────────────────────────────────
void DrumSamplerAudioProcessor::rebuildMidiMap()
{
    midiNoteTopad.fill(-1);
    for (int i = 0; i < NUM_PADS; ++i)
    {
        const int note = kit.pads[static_cast<size_t>(i)].midiNote;
        if (note >= 0 && note < 128)
            midiNoteTopad[static_cast<size_t>(note)] = i;
    }
}

int DrumSamplerAudioProcessor::setPadMidiNote(int padIndex, int midiNote)
{
    if (padIndex < 0 || padIndex >= NUM_PADS || midiNote < 0 || midiNote > 127)
        return -2;

    auto& targetPad = kit.pads[static_cast<size_t>(padIndex)];
    const int previousNote = targetPad.midiNote;
    int swappedPadIndex = -1;

    for (int i = 0; i < NUM_PADS; ++i)
    {
        if (i != padIndex && kit.pads[static_cast<size_t>(i)].midiNote == midiNote)
        {
            kit.pads[static_cast<size_t>(i)].midiNote = previousNote;
            swappedPadIndex = i;
            break;
        }
    }

    targetPad.midiNote = midiNote;
    rebuildMidiMap();
    markKitDirty();
    return swappedPadIndex;
}

void DrumSamplerAudioProcessor::beginMidiLearnForPad(int padIndex) noexcept
{
    if (padIndex >= 0 && padIndex < NUM_PADS)
        midiLearnTargetPad.store(padIndex, std::memory_order_release);
}

void DrumSamplerAudioProcessor::cancelMidiLearn() noexcept
{
    midiLearnTargetPad.store(-1, std::memory_order_release);
}

bool DrumSamplerAudioProcessor::consumeLearnedMidiNote(int& padIndex, int& midiNote) noexcept
{
    const int note = learnedMidiNote.exchange(-1, std::memory_order_acq_rel);
    const int pad = learnedMidiPad.exchange(-1, std::memory_order_acq_rel);

    if (pad < 0 || pad >= NUM_PADS || note < 0 || note > 127)
        return false;

    padIndex = pad;
    midiNote = note;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// prepareToPlay  ─  DAW が再生を始める前に呼ばれる
// ─────────────────────────────────────────────────────────────────────────────
void DrumSamplerAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    hostSampleRate = sampleRate;
    voiceManager.allNotesOff();
    voiceManager.prepare(sampleRate, samplesPerBlock);
    // 開始時の不要な ramp を避けるため、現在のマスターボリュームにゲインを合わせる
    masterGainSmoothed = FaderCurve::positionToGain(juce::jlimit(0.0f, 1.0f, kit.masterVolume));
    if (parametersNeedSync.exchange(false, std::memory_order_acq_rel))
        syncKitFromParameters();
    syncFxAutomationSlots();
}

void DrumSamplerAudioProcessor::releaseResources()
{
    voiceManager.allNotesOff();
}

// ─────────────────────────────────────────────────────────────────────────────
// processBlock  ─  オーディオ処理のメインループ（リアルタイムスレッド）
//
// 呼ばれるたびに:
//   1. MIDI イベント位置までアクティブな Voice をレンダリング
//   2. そのサンプル位置で発音/停止を処理し、次のイベントまで繰り返す
// ─────────────────────────────────────────────────────────────────────────────
void DrumSamplerAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                              juce::MidiBuffer&         midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    // 全バスの出力をクリア（前のブロックの残りを消す）
    buffer.clear();

    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0) return;

   #if ASTER_DEMO_BUILD
    if (isNonRealtime())
    {
        demoOfflineRenderBlocked.store(true, std::memory_order_release);
        voiceManager.allNotesOff();
        voiceManager.clearPadLevels();
        midiMessages.clear();
        return;
    }
    demoOfflineRenderBlocked.store(false, std::memory_order_release);

    // Once the shared 20-minute window has ended, newly triggered voices must
    // not leak any audio.
    if (AsterDemoMode::hasExpired() && ! voiceManager.hasActiveVoices())
    {
        voiceManager.clearPadLevels();
        midiMessages.clear();
        return;
    }
   #endif

    if (parametersNeedSync.exchange(false, std::memory_order_acq_rel))
        syncKitFromParameters();
    syncFxAutomationSlots();

    const bool hasMidi = ! midiMessages.isEmpty();
    if (! hasMidi && ! voiceManager.hasActiveVoices())
    {
        voiceManager.clearPadLevels();
        return;
    }

    const auto processStartTicks = juce::Time::getHighResolutionTicks();

    const auto finishCpuMeasurement = [this, numSamples, processStartTicks]() noexcept
    {
        const auto processEndTicks = juce::Time::getHighResolutionTicks();
        updateAudioProcessLoad(numSamples,
                               processEndTicks - processStartTicks,
                               processEndTicks);
    };

    // ── 読み取りロックをブロック全体で保持 ────────────────────────────────
    juce::ScopedReadLock rl(fileManager.getReadWriteLock());

    // ── 有効バスのサブバッファを集める ────────────────────────────────────
    // バスごとに `getBusBuffer<float>(buffer, false, i)` で AudioBuffer を取り出す。
    // 無効バスや非ステレオバスは nullptr を入れる（VoiceManager 側でスキップ）。
    std::array<juce::AudioBuffer<float>, NUM_OUTPUTS> busBufStorage;
    std::array<juce::AudioBuffer<float>*, NUM_OUTPUTS> busBuffers {};
    const int numBuses = juce::jmin(getBusCount(false), NUM_OUTPUTS);

    for (int i = 0; i < numBuses; ++i)
    {
        auto* bus = getBus(false, i);
        if (bus != nullptr && bus->isEnabled())
        {
            busBufStorage[static_cast<size_t>(i)] = getBusBuffer(buffer, false, i);
            if (busBufStorage[static_cast<size_t>(i)].getNumChannels() >= 2)
                busBuffers[static_cast<size_t>(i)] = &busBufStorage[static_cast<size_t>(i)];
        }
    }

    voiceManager.beginProcessBlock();
    bool renderedAnySegment = false;
    int renderPosition = 0;

    const auto renderVoicesUntil = [&] (int endSample)
    {
        const int clampedEnd = juce::jlimit(renderPosition, numSamples, endSample);
        const int segmentLength = clampedEnd - renderPosition;
        if (segmentLength > 0 && busBuffers[0] != nullptr && voiceManager.hasActiveVoices())
        {
            voiceManager.process(busBuffers.data(),
                                 numBuses,
                                 kit.outputMode,
                                 kit,
                                 fileManager,
                                 renderPosition,
                                 segmentLength,
                                 hostSampleRate);
            renderedAnySegment = true;
        }
        renderPosition = clampedEnd;
    };

    // ── MIDI イベントをサンプル位置どおりに処理 ────────────────────────────
    for (const auto meta : midiMessages)
    {
        renderVoicesUntil(juce::jlimit(0, numSamples, meta.samplePosition));
        const auto msg = meta.getMessage();

        if (msg.isNoteOn())
        {
            const int note     = msg.getNoteNumber();
            int learnPad = midiLearnTargetPad.load(std::memory_order_acquire);
            if (learnPad >= 0 && learnPad < NUM_PADS
                && midiLearnTargetPad.compare_exchange_strong(learnPad, -1, std::memory_order_acq_rel))
            {
                learnedMidiPad.store(learnPad, std::memory_order_release);
                learnedMidiNote.store(note, std::memory_order_release);
                continue;
            }

            const int padIndex = midiNoteTopad[static_cast<size_t>(note)];
            if (padIndex >= 0)
            {
                const float vel = msg.getFloatVelocity();
                // v7+: 視覚フラッシュは必ず発火 (サンプル / Mute / Solo / Polyphony
                // 制限に関係なく "MIDI が届いている" ことを UI で可視化)。
                voiceManager.notifyPadVisualTrigger(padIndex, vel);
                // Voice 生成は startVoicesForPad 内で従来通り条件分岐される
                // (空 Layer はスキップ、Mute/Solo 反映、Polyphony 'Off' で skip 等)。
                voiceManager.noteOn(padIndex, vel, kit, fileManager, hostSampleRate);
               #if ASTER_DEMO_BUILD
                // Empty/muted pads must not start the demo clock.  Start it only
                // after the note has actually created an audible voice.
                if (voiceManager.hasActiveVoices())
                    AsterDemoMode::beginOnFirstSound();
               #endif
            }
        }
        else if (msg.isNoteOff())
        {
            const int note     = msg.getNoteNumber();
            const int padIndex = midiNoteTopad[static_cast<size_t>(note)];
            if (padIndex >= 0)
                voiceManager.noteOff(padIndex);
        }
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
        {
            voiceManager.allNotesOff();
        }
    }

    renderVoicesUntil(numSamples);

    if (! renderedAnySegment && ! voiceManager.hasActiveVoices())
    {
        voiceManager.clearPadLevels();
        markAudioActivity();
        finishCpuMeasurement();
        return;
    }

    markAudioActivity();

    // バス 0 が無効 or 存在しないなら何もできない
    if (busBuffers[0] == nullptr)
    {
        finishCpuMeasurement();
        return;
    }

    // ── マスター出力ボリューム適用 ───────────────────────────────────────
    // フェーダー位置 → 線形ゲイン。全有効バスへ均一に掛け、ブロック間は
    // applyGainRamp で補間して zipper noise を防ぐ。ピーク計測の前に掛けるので
    // マスターメーターもノブに追従する。
    {
        const float targetGain = FaderCurve::positionToGain(
            juce::jlimit(0.0f, 1.0f, kit.masterVolume));
        const float prevGain = masterGainSmoothed;
        if (! (juce::approximatelyEqual(prevGain, 1.0f)
               && juce::approximatelyEqual(targetGain, 1.0f)))
        {
            for (int i = 0; i < numBuses; ++i)
                if (busBuffers[static_cast<size_t>(i)] != nullptr)
                    busBuffers[static_cast<size_t>(i)]->applyGainRamp(0, numSamples, prevGain, targetGain);
        }
        masterGainSmoothed = targetGain;
    }

   #if ASTER_DEMO_BUILD
    // The first five minutes are unrestricted. From 05:00 onward, apply a
    // two-second mute every 60 seconds with short fades at both edges. The
    // same wall-clock schedule is shared by every plug-in instance.
    const auto demoElapsedAtBlockStart = AsterDemoMode::getElapsedSeconds();
    const auto demoBlockDuration = hostSampleRate > 0.0
        ? static_cast<double>(numSamples) / hostSampleRate
        : 0.0;
    const auto demoGainAtStart = AsterDemoMode::getScheduledOutputGain(
        demoElapsedAtBlockStart);
    const auto demoGainAtEnd = AsterDemoMode::getScheduledOutputGain(
        demoElapsedAtBlockStart + demoBlockDuration);

    if (! (juce::approximatelyEqual(demoGainAtStart, 1.0f)
           && juce::approximatelyEqual(demoGainAtEnd, 1.0f)))
    {
        for (int i = 0; i < numBuses; ++i)
            if (busBuffers[static_cast<size_t>(i)] != nullptr)
                busBuffers[static_cast<size_t>(i)]->applyGainRamp(
                    0, numSamples, demoGainAtStart, demoGainAtEnd);
    }

    if (AsterDemoMode::hasExpired())
        voiceManager.allNotesOff();
   #endif

    // ── マスター出力ピーク計測（Bus 0, WebView メーター用） ───────────────
    if (busBuffers[0] != nullptr)
    {
        const float pL = busBuffers[0]->getMagnitude(0, 0, numSamples);
        const float pR = busBuffers[0]->getMagnitude(1, 0, numSamples);

        // CAS ループで atomic max を実現 (std::atomic<float> に fetch_max がないため)
        for (float cur = masterPeakL.load(std::memory_order_relaxed);
             pL > cur;
             cur = masterPeakL.load(std::memory_order_relaxed))
        {
            if (masterPeakL.compare_exchange_weak(cur, pL, std::memory_order_relaxed)) break;
        }
        for (float cur = masterPeakR.load(std::memory_order_relaxed);
             pR > cur;
             cur = masterPeakR.load(std::memory_order_relaxed))
        {
            if (masterPeakR.compare_exchange_weak(cur, pR, std::memory_order_relaxed)) break;
        }
    }

    finishCpuMeasurement();
}

void DrumSamplerAudioProcessor::updateAudioProcessLoad(int numSamples,
                                                       int64 elapsedTicks,
                                                       int64 processEndTicks) noexcept
{
    if (numSamples <= 0 || hostSampleRate <= 0.0 || elapsedTicks <= 0)
        return;

    const double elapsedSeconds = juce::Time::highResolutionTicksToSeconds(elapsedTicks);
    const double bufferSeconds = static_cast<double>(numSamples) / hostSampleRate;
    if (bufferSeconds <= 0.0)
        return;

    const float measured = juce::jlimit(0.0f, 100.0f,
        static_cast<float>((elapsedSeconds / bufferSeconds) * 100.0));
    const float previous = audioProcessLoadPercent.load(std::memory_order_relaxed);
    audioProcessLoadPercent.store(previous + (measured - previous) * 0.12f,
                                  std::memory_order_relaxed);
    lastAudioProcessTicks.store(processEndTicks, std::memory_order_relaxed);
}

float DrumSamplerAudioProcessor::getAudioProcessLoadPercent() const noexcept
{
    const auto lastProcessTicks = lastAudioProcessTicks.load(std::memory_order_relaxed);
    if (lastProcessTicks <= 0)
        return 0.0f;

    const auto ageTicks = juce::Time::getHighResolutionTicks() - lastProcessTicks;
    if (ageTicks < 0 || juce::Time::highResolutionTicksToSeconds(ageTicks) > 1.0)
        return 0.0f;

    return audioProcessLoadPercent.load(std::memory_order_relaxed);
}

void DrumSamplerAudioProcessor::markAudioActivity(int64 ticks) noexcept
{
    lastAudioActivityTicks.store(ticks, std::memory_order_relaxed);
}

bool DrumSamplerAudioProcessor::hasRecentAudioActivity(double holdSeconds) const noexcept
{
    const auto lastActivityTicks = lastAudioActivityTicks.load(std::memory_order_relaxed);
    if (lastActivityTicks <= 0)
        return false;

    const auto ageTicks = juce::Time::getHighResolutionTicks() - lastActivityTicks;
    if (ageTicks < 0)
        return false;

    return juce::Time::highResolutionTicksToSeconds(ageTicks) <= holdSeconds;
}

// ─────────────────────────────────────────────────────────────────────────────
// サンプルの読み込み（UI から呼ばれる）
// 【重要】padName は変更しない。sampleFileName だけを更新する。
// ─────────────────────────────────────────────────────────────────────────────
bool DrumSamplerAudioProcessor::loadSampleForPad(int padIndex, const juce::File& file)
{
    juce::Logger::writeToLog("[ASTER DND] C++ loadSampleForPad start padIndex="
                             + juce::String(padIndex)
                             + " file=" + file.getFullPathName());

    if (!file.existsAsFile())
    {
        juce::Logger::writeToLog("[ASTER DND] C++ loadSampleForPad error: file does not exist");
        return false;
    }

    if (!fileManager.loadFileForPad(padIndex, file))
    {
        juce::Logger::writeToLog("[ASTER DND] C++ loadSampleForPad error: reader failed");
        return false;
    }
    voiceManager.clearPadClip(padIndex);

    auto& pad = kit.pads[static_cast<size_t>(padIndex)];
    pad.sampleFileName = file.getFileName();          // 補助表示用
    pad.sampleFilePath = file.getFullPathName();      // 再ロード用
    pad.sampleMissing  = false;
    resetSampleDependentParameters(padIndex);

    // Layer 0 を flat fields からミラー（VoiceManager は layers[] を読む）
    pad.syncLayer0FromFlat();

    markKitDirty();
    juce::Logger::writeToLog("[ASTER DND] C++ loadSampleForPad success padIndex="
                             + juce::String(padIndex)
                             + " sampleFileName=" + pad.sampleFileName
                             + " sampleFilePath=" + pad.sampleFilePath);
    return true;
}

void DrumSamplerAudioProcessor::resetSampleDependentParameters(int padIndex)
{
    if (padIndex < 0 || padIndex >= NUM_PADS) return;

    auto& pad = kit.pads[static_cast<size_t>(padIndex)];

    const juce::String keepPadName = pad.padName;
    const juce::String keepSampleFileName = pad.sampleFileName;
    const juce::String keepSampleFilePath = pad.sampleFilePath;
    const bool keepSampleMissing = pad.sampleMissing;
    const juce::uint32 keepColour = pad.padColourARGB;
    const auto keepColourMode = pad.padColourMode;
    const int keepMidi = pad.midiNote;
    const int keepOutput = pad.outputAssign;
    const bool keepSwapLR = pad.swapLR;
    const float keepVolume = pad.volume;
    const float keepPan = pad.pan;

    PadData defaults;
    pad.pan = defaults.pan;
    pad.pitch = defaults.pitch;
    pad.fine = defaults.fine;
    pad.attack = defaults.attack;
    pad.release = defaults.release;
    pad.startPosition = defaults.startPosition;
    pad.endPosition = defaults.endPosition;
    pad.fadeIn = defaults.fadeIn;
    pad.fadeOut = defaults.fadeOut;
    pad.reverse = defaults.reverse;
    pad.keepLength = keepLengthOnSampleLoad.load(std::memory_order_relaxed);
    pad.playbackMode = defaults.playbackMode;
    pad.chokeGroup = defaults.chokeGroup;
    pad.mute = defaults.mute;
    pad.solo = defaults.solo;
    pad.velocitySens = defaults.velocitySens;
    pad.humanize = defaults.humanize;
    pad.velCurve = defaults.velCurve;

    pad.padName = keepPadName;
    pad.sampleFileName = keepSampleFileName;
    pad.sampleFilePath = keepSampleFilePath;
    pad.sampleMissing = keepSampleMissing;
    pad.padColourARGB = keepColour;
    pad.padColourMode = keepColourMode;
    pad.midiNote = keepMidi;
    pad.outputAssign = keepOutput;
    pad.swapLR = keepSwapLR;
    pad.volume = keepVolume;
    pad.pan = keepPan;

    // Layer 0 を flat fields に同期（再生は layers[] を参照）
    pad.syncLayer0FromFlat();

    syncParametersFromKit();
}

// ─────────────────────────────────────────────────────────────────────────────
// 試聴（Pad クリックから呼ばれる）
// UI スレッドから呼ぶ。ファイル読み取りロックを取って noteOn を発火する。
// ─────────────────────────────────────────────────────────────────────────────
void DrumSamplerAudioProcessor::auditionPadOn(int padIndex, float velocity)
{
    if (padIndex < 0 || padIndex >= NUM_PADS) return;

    // Layer 0 だけでなく、いずれかの Layer にバッファがあれば鳴らす。
    // MAIN が空で L2+ にだけサンプルが入っている状態でも Pad クリックで発音できる。
    const auto& padRef = kit.pads[static_cast<size_t>(padIndex)];
    bool anyLayerHasSample = false;
    for (int li = 0; li < padRef.layerCount(); ++li)
    {
        if (fileManager.hasSample(padIndex, li))
        {
            anyLayerHasSample = true;
            break;
        }
    }
    if (! anyLayerHasSample) return;

   #if ASTER_DEMO_BUILD
    if (AsterDemoMode::hasExpired()) return;
    AsterDemoMode::beginOnFirstSound();
   #endif

    juce::Logger::writeToLog("[ASTER PLAY] auditionPadOn padIndex=" + juce::String(padIndex)
                             + " velocity=" + juce::String(velocity, 2)
                             + " layerCount=" + juce::String(padRef.layerCount()));

    juce::ScopedReadLock rl(fileManager.getReadWriteLock());
    // layerIndex = -1: play ALL eligible layers (respects layer mute/solo/velocityRange).
    // Using the default (0) would silently skip every layer except MAIN,
    // making pad-click behave differently from MIDI noteOn.
    voiceManager.previewNoteOn(padIndex, velocity, kit, fileManager, hostSampleRate, -1);
}

void DrumSamplerAudioProcessor::auditionLayerOn(int padIndex,
                                                int layerIndex,
                                                float velocity)
{
    if (padIndex < 0 || padIndex >= NUM_PADS) return;

    const auto& padRef = kit.pads[static_cast<size_t>(padIndex)];
    if (layerIndex < 0 || layerIndex >= padRef.layerCount()) return;
    if (! fileManager.hasSample(padIndex, layerIndex)) return;

   #if ASTER_DEMO_BUILD
    if (AsterDemoMode::hasExpired()) return;
    AsterDemoMode::beginOnFirstSound();
   #endif

    juce::ScopedReadLock rl(fileManager.getReadWriteLock());
    // Waveform audition is an editor-focused preview: play only the visible
    // layer from its configured START, even if that layer/pad is muted or
    // outside its velocity range.
    voiceManager.previewNoteOn(padIndex,
                               juce::jlimit(0.0f, 1.0f, velocity),
                               kit,
                               fileManager,
                               hostSampleRate,
                               layerIndex,
                               -1.0f,
                               /*ignoreMuteSoloAndVelocityRange=*/ true);
}

void DrumSamplerAudioProcessor::auditionPadOff(int padIndex)
{
    if (padIndex < 0 || padIndex >= NUM_PADS) return;
    voiceManager.previewNoteOff(padIndex, hostSampleRate);
}

// ─────────────────────────────────────────────────────────────────────────────
// Smart Trim を現在のサンプルに対して再実行
// Re-Analyze ボタンから呼ばれる（smartTrimOnSampleLoad の状態に関係なく必ず実行）
// ─────────────────────────────────────────────────────────────────────────────
void DrumSamplerAudioProcessor::reanalyzePad(int padIndex)
{
    if (padIndex < 0 || padIndex >= NUM_PADS) return;
    if (! fileManager.hasSample(padIndex))    return;

    juce::ScopedReadLock rl(fileManager.getReadWriteLock());
    const auto* buf = fileManager.getBufferNoLock(padIndex);
    if (buf == nullptr) return;

    const auto r = SmartTrim::analyze(*buf, fileManager.getSampleRate(padIndex));
    kit.pads[static_cast<size_t>(padIndex)].startPosition = r.startPosition;
    kit.pads[static_cast<size_t>(padIndex)].endPosition   = r.endPosition;
    setAutomatablePadParameter(padIndex, PadParameterSpecs::Param::Start, r.startPosition, false);
    setAutomatablePadParameter(padIndex, PadParameterSpecs::Param::End,   r.endPosition,   false);
    markKitDirty();
}

void DrumSamplerAudioProcessor::applyAutoFadeOnTrim(int padIndex)
{
    if (padIndex < 0 || padIndex >= NUM_PADS) return;
    if (! kit.autoFadeOnTrim) return;

    auto& pad = kit.pads[static_cast<size_t>(padIndex)];

    juce::ScopedReadLock rl(fileManager.getReadWriteLock());
    const auto* buf = fileManager.getBufferNoLock(padIndex);
    const double sampleRate = fileManager.getSampleRate(padIndex);
    if (buf == nullptr || buf->getNumSamples() <= 0 || sampleRate <= 0.0)
        return;

    const double totalSamples = static_cast<double>(buf->getNumSamples());
    const double startSample = totalSamples * static_cast<double>(pad.startPosition);
    const double endSample   = totalSamples * static_cast<double>(pad.endPosition);
    const double rangeSamples = juce::jmax(1.0, endSample - startSample);
    const double rangeSeconds = rangeSamples / sampleRate;

    const float minFadeInRatio = (kAutoTrimFadeInMs <= 0.0f || rangeSeconds <= 0.0)
        ? 0.0f
        : juce::jlimit(0.0f, 1.0f, static_cast<float>((kAutoTrimFadeInMs / 1000.0) / rangeSeconds));

    const float targetFadeOutMs = juce::jmax(kAutoTrimFadeOutMs, kit.defaultFadeOutMs);
    const float minFadeOutRatio = (targetFadeOutMs <= 0.0f || rangeSeconds <= 0.0)
        ? 0.0f
        : juce::jlimit(0.0f, 1.0f, static_cast<float>((targetFadeOutMs / 1000.0) / rangeSeconds));

    const float nextFadeIn = juce::jmax(pad.fadeIn, minFadeInRatio);
    const float nextFadeOut = juce::jmax(pad.fadeOut, minFadeOutRatio);

    bool changed = false;

    if (nextFadeIn > pad.fadeIn + 0.000001f)
    {
        setAutomatablePadParameter(padIndex, PadParameterSpecs::Param::FadeIn, nextFadeIn, false);
        changed = true;
    }

    if (nextFadeOut > pad.fadeOut + 0.000001f)
    {
        setAutomatablePadParameter(padIndex, PadParameterSpecs::Param::FadeOut, nextFadeOut, false);
        changed = true;
    }

    if (changed)
        markKitDirty();
}

// ─────────────────────────────────────────────────────────────────────────────
// パッドのサンプルを消去（padName は残す）
// ─────────────────────────────────────────────────────────────────────────────
void DrumSamplerAudioProcessor::clearPadSample(int padIndex)
{
    if (padIndex < 0 || padIndex >= NUM_PADS) return;

    fileManager.clearPad(padIndex);
    voiceManager.clearPadClip(padIndex);
    auto& padRef = kit.pads[static_cast<size_t>(padIndex)];
    padRef.sampleFileName.clear();
    padRef.sampleFilePath.clear();
    padRef.sampleMissing = false;
    // Pad 全 Layer を空にリセット（Layer 0 のみ残し、サンプル参照は空）
    padRef.layers.clear();
    padRef.layers.emplace_back();
    padRef.syncLayer0FromFlat();
    // padName はクリアしない
    syncParametersFromKit();
    markKitDirty();
}

// 任意 Layer にサンプルを読み込む（Layer 0 は flat fields とミラーする）。
bool DrumSamplerAudioProcessor::loadSampleForLayer(int padIndex, int layerIndex, const juce::File& file)
{
    if (padIndex < 0 || padIndex >= NUM_PADS) return false;
    if (! file.existsAsFile()) return false;

    auto& pad = kit.pads[static_cast<size_t>(padIndex)];

    // Layer 0 への load は既存の pad-level load にディスパッチ (resetSampleDependentParameters
    // など Pad-wide な副作用を維持するため)。
    if (layerIndex == 0)
        return loadSampleForPad(padIndex, file);

    // L2+: AudioFileManager の slot に読み込み、Layer メタデータだけ更新する。
    if (layerIndex < 0 || layerIndex >= pad.layerCount()) return false;

    if (! fileManager.loadFileForPad(padIndex, layerIndex, file))
        return false;

    voiceManager.clearPadClip(padIndex);

    auto& L = pad.layers[static_cast<size_t>(layerIndex)];
    L.sampleFileName = file.getFileName();
    L.sampleFilePath = file.getFullPathName();
    L.sampleMissing  = false;
    // 新サンプルに対しては Trim/Fade を初期値に戻す (典型的な使い勝手)。
    L.startPosition = 0.0f;
    L.endPosition   = 1.0f;
    L.fadeIn        = 0.0f;
    L.fadeOut       = 0.0f;
    L.keepLength    = keepLengthOnSampleLoad.load(std::memory_order_relaxed);

    syncParametersFromKit();
    markKitDirty();
    return true;
}

// 任意 Layer のサンプルだけ消去 (Layer 構造は維持し、空 Layer として残す)。
void DrumSamplerAudioProcessor::clearLayerSample(int padIndex, int layerIndex)
{
    if (padIndex < 0 || padIndex >= NUM_PADS) return;

    auto& pad = kit.pads[static_cast<size_t>(padIndex)];
    if (layerIndex < 0 || layerIndex >= pad.layerCount()) return;

    if (layerIndex == 0)
    {
        // Layer 0 は flat fields と AudioFileManager の slot 0 をクリア
        fileManager.clearLayer(padIndex, 0);
        voiceManager.clearPadClip(padIndex);
        pad.sampleFileName.clear();
        pad.sampleFilePath.clear();
        pad.sampleMissing = false;
        pad.layers[0].sampleFileName.clear();
        pad.layers[0].sampleFilePath.clear();
        pad.layers[0].sampleMissing = false;
        pad.syncLayer0FromFlat();
    }
    else
    {
        fileManager.clearLayer(padIndex, layerIndex);
        auto& L = pad.layers[static_cast<size_t>(layerIndex)];
        L.sampleFileName.clear();
        L.sampleFilePath.clear();
        L.sampleMissing = false;
    }

    syncParametersFromKit();
    markKitDirty();
}

// ═════════════════════════════════════════════════════════════════════════════
//  Pad 操作（Copy / Paste / Clear / Reset / Swap / Replace）
//
//  すべて UI スレッド（メッセージスレッド）から呼ばれる前提。
//  オーディオスレッドはサンプルバッファ書き換えの間 ScopedWriteLock で
//  ブロックされるので、再生中のクリックノイズは発生しないが
//  一瞬の音切れは起こりうる（許容範囲）。
// ═════════════════════════════════════════════════════════════════════════════

// ─────────────────────────────────────────────────────────────────────────────
// Copy : 全フィールドをクリップボードへ（midiNote は除いて保存しても、
//        Paste では適用しないので入っていても問題ない）
// ─────────────────────────────────────────────────────────────────────────────
void DrumSamplerAudioProcessor::copyPad(int padIndex)
{
    if (padIndex < 0 || padIndex >= NUM_PADS) return;

    padClipboard   = kit.pads[static_cast<size_t>(padIndex)];
    clipboardValid = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Paste : クリップボードのフィールドを適用。
//   - midiNote は保持（位置依存なので上書きしない）
//   - sampleFilePath が空でなければ AudioFileManager に再ロードを依頼
// ─────────────────────────────────────────────────────────────────────────────
void DrumSamplerAudioProcessor::pastePad(int padIndex)
{
    if (! clipboardValid)                       return;
    if (padIndex < 0 || padIndex >= NUM_PADS)   return;

    auto& dst = kit.pads[static_cast<size_t>(padIndex)];
    const int keepMidi = dst.midiNote;
    const int keepOutput = dst.outputAssign;
    const bool keepSwapLR = dst.swapLR;

    // 全フィールドをコピー → midiNote だけ元に戻す
    dst = padClipboard;
    dst.midiNote = keepMidi;
    dst.outputAssign = keepOutput;
    dst.swapLR = keepSwapLR;

    // サンプルもコピー: Layer ごとのパスを再ロードする。Layer 0 だけを見ると
    // multi-layer pad の Paste 後に L2+ が無音になるため、全 Layer を同期する。
    fileManager.clearPad(padIndex);
    for (int li = 0; li < dst.layerCount(); ++li)
    {
        auto& L = dst.layers[static_cast<size_t>(li)];
        if (L.sampleFilePath.isNotEmpty())
        {
            const juce::File f(L.sampleFilePath);
            if (f.existsAsFile() && fileManager.loadFileForPad(padIndex, li, f))
            {
                L.sampleMissing = false;
            }
            else
            {
                L.sampleMissing = true;
            }
        }
        else
        {
            L.sampleMissing = false;
        }
    }
    dst.syncFlatFromLayer0();

    voiceManager.clearPadClip(padIndex);
    syncParametersFromKit();
    markKitDirty();
}

// ─────────────────────────────────────────────────────────────────────────────
// Clear (Full) : サンプル + すべての設定をリセット（padName と midiNote は保持）
// ─────────────────────────────────────────────────────────────────────────────
void DrumSamplerAudioProcessor::clearPadFull(int padIndex)
{
    if (padIndex < 0 || padIndex >= NUM_PADS) return;

    auto& pad = kit.pads[static_cast<size_t>(padIndex)];

    // 保持する値を退避
    const juce::String keepName = pad.padName;
    const int          keepMidi = pad.midiNote;
    const auto         keepColour = pad.padColourARGB;
    const auto         keepColourMode = pad.padColourMode;

    // バッファとサンプル情報を消去
    fileManager.clearPad(padIndex);
    voiceManager.clearPadClip(padIndex);

    // 全フィールドをデフォルトに戻す
    pad = PadData{};

    // 退避していた値を復元
    pad.padName      = keepName;
    pad.midiNote     = keepMidi;
    // Full reset follows the kit default routing: Main.
    pad.outputAssign = 0;
    pad.swapLR = false;
    pad.padColourARGB = keepColour;
    pad.padColourMode = keepColourMode;
    syncParametersFromKit();
    markKitDirty();
}

// ─────────────────────────────────────────────────────────────────────────────
// Reset Pad Settings : サンプルは残し、パラメータだけリセット
//   保持: padName, midiNote, sampleFileName, sampleFilePath
// ─────────────────────────────────────────────────────────────────────────────
void DrumSamplerAudioProcessor::resetPadSettings(int padIndex)
{
    if (padIndex < 0 || padIndex >= NUM_PADS) return;

    auto& pad = kit.pads[static_cast<size_t>(padIndex)];

    // 保持する値を退避（サンプル関連 + 名前 + MIDI）
    const juce::String keepName   = pad.padName;
    const juce::String keepFile   = pad.sampleFileName;
    const juce::String keepPath   = pad.sampleFilePath;
    const bool         keepMissing = pad.sampleMissing;
    const int          keepMidi   = pad.midiNote;
    const int          keepOutput = pad.outputAssign;
    const bool         keepSwapLR = pad.swapLR;
    const float        keepVolume = pad.volume;
    const auto         keepColour = pad.padColourARGB;
    const auto         keepColourMode = pad.padColourMode;

    // 全フィールドをデフォルトに
    pad = PadData{};

    // 復元
    pad.padName        = keepName;
    pad.midiNote       = keepMidi;
    pad.volume         = keepVolume;
    pad.sampleFileName = keepFile;
    pad.sampleFilePath = keepPath;
    pad.sampleMissing  = keepMissing;
    pad.outputAssign   = keepOutput;
    pad.swapLR         = keepSwapLR;
    pad.padColourARGB  = keepColour;
    pad.padColourMode  = keepColourMode;
    pad.syncLayer0FromFlat();
    voiceManager.clearPadClip(padIndex);
    syncParametersFromKit();
    markKitDirty();
}

// ─────────────────────────────────────────────────────────────────────────────
// Swap : 2 パッドのサウンド内容を交換
//   - PADスロット固定情報（midiNote / outputAssign）は交換しない
//   - Pad名、サンプル、Trim、Playback、Dynamics、Mixer系の音作り設定は交換
//   - サンプルバッファも交換（AudioFileManager::swapPads）
// ─────────────────────────────────────────────────────────────────────────────
void DrumSamplerAudioProcessor::swapPads(int a, int b)
{
    if (a == b)                 return;
    if (a < 0 || a >= NUM_PADS) return;
    if (b < 0 || b >= NUM_PADS) return;

    auto& pa = kit.pads[static_cast<size_t>(a)];
    auto& pb = kit.pads[static_cast<size_t>(b)];

    // Slot-fixed fields stay attached to the physical pad position.
    const int midiA = pa.midiNote;
    const int midiB = pb.midiNote;
    const int outputA = pa.outputAssign;
    const int outputB = pb.outputAssign;

    std::swap(pa, pb);

    pa.midiNote = midiA;
    pb.midiNote = midiB;
    pa.outputAssign = outputA;
    pb.outputAssign = outputB;

    // サンプルバッファも交換
    fileManager.swapPads(a, b);
    syncParametersFromKit();
    markKitDirty();
}

// ─────────────────────────────────────────────────────────────────────────────
// Replace Sample : サンプルだけ差し替え（padName は変更しない）
//   loadSampleForPad の薄いラッパー。Smart Trim も走る。
// ─────────────────────────────────────────────────────────────────────────────
bool DrumSamplerAudioProcessor::replacePadSample(int padIndex, const juce::File& file)
{
    return loadSampleForPad(padIndex, file);
}

bool DrumSamplerAudioProcessor::relinkPadSampleOnly(int padIndex,
                                                    const juce::File& file,
                                                    bool updateSampleFileName)
{
    if (padIndex < 0 || padIndex >= NUM_PADS) return false;
    if (! file.existsAsFile()) return false;
    if (! fileManager.loadFileForPad(padIndex, file)) return false;

    auto& pad = kit.pads[static_cast<size_t>(padIndex)];

    // Relink は「参照先の復旧」だけを行う。padName / trim / fade / routing /
    // choke などの編集済み設定は変えない。
    if (updateSampleFileName || pad.sampleFileName.isEmpty())
        pad.sampleFileName = file.getFileName();

    pad.sampleFilePath = file.getFullPathName();
    pad.sampleMissing = false;
    voiceManager.clearPadClip(padIndex);
    return true;
}

bool DrumSamplerAudioProcessor::relinkPadSample(int padIndex, const juce::File& file)
{
    if (padIndex < 0 || padIndex >= NUM_PADS) return false;
    if (! file.existsAsFile()) return false;

    bool relinkedAny = false;
    if (relinkPadSampleOnly(padIndex, file, true))
        relinkedAny = true;

    const juce::File parentDir = file.getParentDirectory();
    if (! parentDir.isDirectory())
    {
        if (relinkedAny)
            markKitDirty();
        return relinkedAny;
    }

    for (int i = 0; i < NUM_PADS; ++i)
    {
        if (i == padIndex) continue;

        auto& pad = kit.pads[static_cast<size_t>(i)];
        if (! pad.sampleMissing || pad.sampleFileName.isEmpty())
            continue;

        const juce::File candidate = parentDir.getChildFile(pad.sampleFileName);
        if (! candidate.existsAsFile())
            continue;

        if (relinkPadSampleOnly(i, candidate, false))
            relinkedAny = true;
    }

    if (relinkedAny)
        markKitDirty();

    return relinkedAny;
}

void DrumSamplerAudioProcessor::clearAllSolo()
{
    bool changed = false;
    for (int i = 0; i < NUM_PADS; ++i)
    {
        auto& pad = kit.pads[static_cast<size_t>(i)];
        if (pad.solo)
        {
            pad.solo = false;
            changed = true;
        }
    }

    if (changed)
    {
        syncParametersFromKit();
        markKitDirty();
    }
}

void DrumSamplerAudioProcessor::clearAllMute()
{
    bool changed = false;
    for (int i = 0; i < NUM_PADS; ++i)
    {
        auto& pad = kit.pads[static_cast<size_t>(i)];
        if (pad.mute)
        {
            pad.mute = false;
            changed = true;
        }
    }

    if (changed)
    {
        syncParametersFromKit();
        markKitDirty();
    }
}

void DrumSamplerAudioProcessor::resetMixerPage(int page)
{
    const int safePage = juce::jlimit(0, NUM_PAGES - 1, page);
    const int first = KitData::firstPadOnPage(safePage);
    const int last = juce::jmin(NUM_PADS - 1, first + PADS_PER_PAGE - 1);

    for (int i = first; i <= last; ++i)
    {
        auto& pad = kit.pads[static_cast<size_t>(i)];
        pad.volume = 1.0f;
        pad.pan = 0.0f;
        pad.mute = false;
        pad.solo = false;
        pad.outputAssign = 0;
        pad.syncLayer0FromFlat();
        voiceManager.clearPadClip(i);
    }

    syncParametersFromKit();
    markKitDirty();
}

void DrumSamplerAudioProcessor::newKit()
{
    kit.resetToDefaults();
    kit.outputMode = kit.defaultOutputMode;
    currentKitFile = juce::File{};

    for (int i = 0; i < NUM_PADS; ++i)
    {
        fileManager.clearPad(i);
        voiceManager.clearPadClip(i);
    }

    hydrateRuntimeFromKit(false);
    clearKitDirty();
}

juce::File DrumSamplerAudioProcessor::getUserKitsDirectory()
{
    copyLegacyAppDataIfNeeded();
    return getAppDataDirectory().getChildFile("User Kits");
}

juce::Array<juce::File> DrumSamplerAudioProcessor::getSavedKitFiles()
{
    auto dir = getUserKitsDirectory();
    if (! dir.exists())
        dir.createDirectory();

    juce::Array<juce::File> files;
    dir.findChildFiles(files, juce::File::findFiles, false, "*.asterkit");
    struct KitFileSorter
    {
        static int compareElements(const juce::File& a, const juce::File& b)
        {
            return a.getFileNameWithoutExtension().compareIgnoreCase(b.getFileNameWithoutExtension());
        }
    };
    KitFileSorter sorter;
    files.sort(sorter);
    return files;
}

bool DrumSamplerAudioProcessor::isDefaultKitName(const juce::String& name)
{
    return name.trim().equalsIgnoreCase("Default");
}

bool DrumSamplerAudioProcessor::saveKitToFile(const juce::File& file)
{
   #if ASTER_DEMO_BUILD
    juce::ignoreUnused(file);
    return false;
   #endif
    if (file == juce::File{}) return false;

    syncKitFromParameters();
    syncFxAutomationSlots();
    kit.kitVersion = CURRENT_KIT_VERSION;
    kit.pluginVersion = JucePlugin_VersionString;

    juce::File outFile = file;
    if (outFile.getFileExtension().isEmpty())
        outFile = outFile.withFileExtension(kKitFileExtension);
    else if (! outFile.getFileExtension().equalsIgnoreCase(kKitFileExtension))
        outFile = outFile.withFileExtension(kKitFileExtension);

    if (! outFile.getParentDirectory().exists())
        outFile.getParentDirectory().createDirectory();

    const auto nextKitName = outFile.getFileNameWithoutExtension().trim();
    if (isDefaultKitName(nextKitName))
        return false;

    const auto json = makeAsterKitJson(kit, nextKitName);
    const auto text = juce::JSON::toString(json, false);

    if (! outFile.replaceWithText(text))
        return false;

    kit.kitName = nextKitName;
    currentKitFile = outFile;
    addRecentKitPath(outFile);
    clearKitDirty();
    return true;
}

bool DrumSamplerAudioProcessor::saveKitToCurrentFile()
{
    if (currentKitFile == juce::File{})
        return false;
    if (isDefaultKitName(kit.kitName))
        return false;

    return saveKitToFile(currentKitFile);
}

bool DrumSamplerAudioProcessor::loadKitFromFile(const juce::File& file)
{
    return loadKitFromFile(file, {});
}

bool DrumSamplerAudioProcessor::loadKitFromFile(const juce::File& file,
                                                const KitLoadOptions& options)
{
    if (! file.existsAsFile())
        return false;

    const auto kitTree = readKitTreeFromFile(file);
    if (! kitTree.isValid())
        return false;

    KitData loadedKit;
    loadedKit.fromValueTree(kitTree);
    loadedKit.kitName = file.getFileNameWithoutExtension();

    const bool loadEverything = options.samples
                             && options.padNamesAndColours
                             && options.padParameters
                             && options.mixerSettings
                             && options.routing;

    if (loadEverything)
    {
        kit = loadedKit;
        kit.kitName = file.getFileNameWithoutExtension();
        currentKitFile = file;
        addRecentKitPath(file);
        hydrateRuntimeFromKit(false);
        clearKitDirty();
    }
    else
    {
        // Partial load: merge the requested categories into the *current* kit
        // without changing the kit's identity. The header still shows the
        // currently selected kit, but kitDirty=true makes the "*" appear so
        // the user can see the on-disk and in-memory state diverged.
        mergeLoadedKit(kit, loadedKit, options);
        addRecentKitPath(file);
        hydrateRuntimeFromKit(false);
        markKitDirty();
    }
    return true;
}

bool DrumSamplerAudioProcessor::applyDefaultKit(const KitLoadOptions& options)
{
    const bool loadEverything = options.samples
                             && options.padNamesAndColours
                             && options.padParameters
                             && options.mixerSettings
                             && options.routing;

    if (loadEverything)
    {
        newKit();
        return true;
    }

    // Partial: merge default values into the current kit, keep the active
    // kit's identity, mark dirty.
    KitData defaultKit;  // ctor calls resetToDefaults()
    defaultKit.outputMode = defaultKit.defaultOutputMode;

    mergeLoadedKit(kit, defaultKit, options);

    if (options.samples)
    {
        for (int i = 0; i < NUM_PADS; ++i)
            fileManager.clearPad(i);
    }

    hydrateRuntimeFromKit(false);
    markKitDirty();
    return true;
}

bool DrumSamplerAudioProcessor::loadRecentKit(int recentIndex)
{
    if (recentIndex < 0 || recentIndex >= recentKitPaths.size())
        return false;

    return loadKitFromFile(juce::File(recentKitPaths[recentIndex]));
}

// ─────────────────────────────────────────────────────────────────────────────
// Kit 保存（DAW がセッションを保存するときに呼ばれる）
// ─────────────────────────────────────────────────────────────────────────────
void DrumSamplerAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    // Flush any host/automation changes that arrived since the last audio
    // block so the Kit snapshot remains the complete source of truth.
    syncKitFromParameters();
    syncFxAutomationSlots();
    auto root = juce::ValueTree { "DrumSamplerState" };
    root.setProperty(kStateVersionId, kCurrentStateVersion, nullptr);
    root.addChild(kit.toValueTree(), -1, nullptr);
    root.addChild(parameters.copyState(), -1, nullptr);

    juce::ValueTree automationSlotsState { kAutomationSlotsStateId };
    for (int slot = 0; slot < automationSlotCount; ++slot)
        automationSlotsState.setProperty("target" + juce::String(slot + 1).paddedLeft('0', 2),
                                         automationSlotTargets[static_cast<size_t>(slot)],
                                         nullptr);
    root.addChild(automationSlotsState, -1, nullptr);

    juce::MemoryOutputStream stream(destData, false);
    root.writeToStream(stream);
}

// ─────────────────────────────────────────────────────────────────────────────
// Kit 読み込み（DAW がセッションを開いたときに呼ばれる）
// ─────────────────────────────────────────────────────────────────────────────
void DrumSamplerAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    juce::MemoryInputStream stream(data, static_cast<size_t>(sizeInBytes), false);
    const auto tree = juce::ValueTree::readFromStream(stream);

    if (!tree.isValid()) return;

    const auto kitTree = tree.hasType("Kit") ? tree : tree.getChildWithName("Kit");
    if (! kitTree.isValid()) return;

    // Detect state version. States written before kCurrentStateVersion = 2
    // stored pad.volume as a linear gain (0..1, max = 0 dB) — we now use it
    // as a fader position (0..1, max = +12 dB, 0.75 = 0 dB).
    const int loadedVersion = (int) tree.getProperty(kStateVersionId, 1);
    const bool needsVolumeMigration = (loadedVersion < 2);

    kit.fromValueTree(kitTree);
    const auto parameterTree = tree.getChildWithName("PARAMETERS");
    if (parameterTree.isValid())
        parameters.replaceState(parameterTree);

    const auto automationSlotsState = tree.getChildWithName(kAutomationSlotsStateId);
    for (int slot = 0; slot < automationSlotCount; ++slot)
    {
        const auto propertyName = "target" + juce::String(slot + 1).paddedLeft('0', 2);
        setAutomationSlotTarget(slot,
            automationSlotsState.isValid()
                ? automationSlotsState.getProperty(propertyName).toString()
                : juce::String{});
    }

    if (needsVolumeMigration)
    {
        // Convert each pad's legacy linear-gain volume to the new fader
        // position so the audible result and the displayed dB stay the
        // same after migration: old V (gain) → new pos with same gain.
        for (int i = 0; i < NUM_PADS; ++i)
        {
            const float legacyGain = kit.pads[static_cast<size_t>(i)].volume;
            kit.pads[static_cast<size_t>(i)].volume = FaderCurve::gainToPosition(legacyGain);
        }
        // Push the migrated kit values back into AudioParameterFloats so
        // DAW automation lanes are also rewritten (the host re-reads the
        // current parameter value on the next sync).
        syncParametersFromKit();
        markKitDirty();
    }

    // The Kit tree is the complete plug-in snapshot and is also where values
    // that are not exposed as parameters (for example Layer trim) live.  Keep
    // it authoritative when restoring a DAW project, then mirror it into the
    // APVTS.  This also prevents stale duplicate Layer-0 parameter values from
    // overwriting the values that were actually saved in the Kit tree.
    hydrateRuntimeFromKit(false);
    currentKitFile = juce::File{};
    // currentKitFile is the source of truth for the kit's identity. Once we
    // clear it, kit.kitName must follow so the UI never shows a saved-kit
    // name that has no entry in the kit list.
    kit.kitName = "Default";
    if (! needsVolumeMigration) clearKitDirty();
}

void DrumSamplerAudioProcessor::reloadSamplesFromCurrentKit()
{
    for (int i = 0; i < NUM_PADS; ++i)
    {
        auto& pad = kit.pads[static_cast<size_t>(i)];
        fileManager.clearPad(i);

        for (int li = 0; li < pad.layerCount(); ++li)
        {
            auto& L = pad.layers[static_cast<size_t>(li)];
            const juce::String& path = L.sampleFilePath;

            if (path.isNotEmpty())
            {
                const juce::File f(path);
                if (f.existsAsFile() && fileManager.loadFileForPad(i, li, f))
                {
                    L.sampleMissing = false;
                }
                else
                {
                    L.sampleMissing = true;
                }
            }
            else
            {
                L.sampleMissing = false;
            }
        }

        pad.syncFlatFromLayer0();
    }
}

void DrumSamplerAudioProcessor::hydrateRuntimeFromKit(bool restoreFromParametersTree)
{
    if (! restoreFromParametersTree)
        syncParametersFromKit();
    syncKitFromParameters();
    rebuildMidiMap();
    reloadSamplesFromCurrentKit();
    voiceManager.clearAllPadClips();
}

void DrumSamplerAudioProcessor::addRecentKitPath(const juce::File& file)
{
    const auto path = file.getFullPathName();
    recentKitPaths.removeString(path);
    recentKitPaths.insert(0, path);

    while (recentKitPaths.size() > kMaxRecentKits)
        recentKitPaths.remove(recentKitPaths.size() - 1);

    saveRecentKitPaths();
}

juce::File DrumSamplerAudioProcessor::getRecentKitStoreFile()
{
    copyLegacyAppDataIfNeeded();
    return getAppDataDirectory().getChildFile("recent-kits.txt");
}

void DrumSamplerAudioProcessor::loadRecentKitPaths()
{
    recentKitPaths.clear();
    const auto file = getRecentKitStoreFile();
    if (! file.existsAsFile())
        return;

    for (const auto& line : juce::StringArray::fromLines(file.loadFileAsString()))
    {
        const auto trimmed = line.trim();
        if (trimmed.isNotEmpty())
            recentKitPaths.addIfNotAlreadyThere(trimmed);
    }
}

void DrumSamplerAudioProcessor::saveRecentKitPaths() const
{
    const auto file = getRecentKitStoreFile();
    file.getParentDirectory().createDirectory();

    juce::String content;
    for (const auto& path : recentKitPaths)
        content << path << "\n";

    file.replaceWithText(content);
}

juce::String DrumSamplerAudioProcessor::automationSlotParameterID(int slotIndex)
{
    return juce::String(kAutomationSlotPrefix)
         + juce::String(slotIndex + 1).paddedLeft('0', 2);
}

int DrumSamplerAudioProcessor::automationSlotIndexFromParameterID(const juce::String& parameterID)
{
    if (! parameterID.startsWith(kAutomationSlotPrefix))
        return -1;

    const int oneBased = parameterID.substring(juce::String(kAutomationSlotPrefix).length()).getIntValue();
    return juce::isPositiveAndBelow(oneBased - 1, automationSlotCount) ? oneBased - 1 : -1;
}

int DrumSamplerAudioProcessor::findParameterIndex(const juce::String& parameterID) const
{
    const auto* target = parameters.getParameter(parameterID);
    if (target == nullptr)
        return -1;

    const auto& processorParameters = getParameters();
    for (int index = 0; index < processorParameters.size(); ++index)
        if (processorParameters.getUnchecked(index) == target)
            return index;
    return -1;
}

int DrumSamplerAudioProcessor::assignedAutomationSlotForTarget(const juce::String& parameterID) const
{
    for (int slot = 0; parameterID.isNotEmpty() && slot < automationSlotCount; ++slot)
        if (automationSlotTargets[static_cast<size_t>(slot)] == parameterID)
            return slot;
    return -1;
}

int DrumSamplerAudioProcessor::fxAutomationTargetCode(const juce::String& targetID)
{
    juce::StringArray tokens;
    tokens.addTokens(targetID, ".", {});
    if (tokens.size() != 5 || ! tokens[0].startsWith("pad")
        || ! tokens[1].startsWith("layer") || tokens[2] != "fx")
        return -1;

    const int padIndex = tokens[0].substring(3).getIntValue() - 1;
    const int layerIndex = tokens[1].substring(5).getIntValue() - 1;
    if (! juce::isPositiveAndBelow(padIndex, NUM_PADS)
        || ! juce::isPositiveAndBelow(layerIndex, MAX_LAYERS_PER_PAD))
        return -1;

    const auto typeName = tokens[3].toLowerCase();
    const auto parameter = tokens[4];
    int type = 0;
    int param = 0;
    if (typeName == "filter")
    {
        type = 1;
        if      (parameter == "bypass")      param = 1;
        else if (parameter == "hpEnabled")   param = 2;
        else if (parameter == "hpCutoff")    param = 3;
        else if (parameter == "hpSlope")     param = 4;
        else if (parameter == "hpResonance") param = 5;
        else if (parameter == "lpEnabled")   param = 6;
        else if (parameter == "lpCutoff")    param = 7;
        else if (parameter == "lpSlope")     param = 8;
        else if (parameter == "lpResonance") param = 9;
    }
    else if (typeName == "drive")
    {
        type = 2;
        if      (parameter == "bypass") param = 1;
        else if (parameter == "type")   param = 2;
        else if (parameter == "amount") param = 3;
        else if (parameter == "tone")   param = 4;
        else if (parameter == "mix")    param = 5;
        else if (parameter == "output") param = 6;
    }
    else if (typeName == "transient")
    {
        type = 3;
        if      (parameter == "bypass")  param = 1;
        else if (parameter == "attack")  param = 2;
        else if (parameter == "sustain") param = 3;
        else if (parameter == "output")  param = 4;
    }
    else if (typeName == "compressor")
    {
        type = 4;
        if      (parameter == "bypass")    param = 1;
        else if (parameter == "threshold") param = 2;
        else if (parameter == "ratio")     param = 3;
        else if (parameter == "attack")    param = 4;
        else if (parameter == "release")   param = 5;
        else if (parameter == "makeup")    param = 6;
        else if (parameter == "mix")       param = 7;
        else if (parameter == "output")    param = 8;
    }

    return type > 0 && param > 0
        ? (padIndex << 11) | (layerIndex << 8) | (type << 5) | param
        : -1;
}

juce::String DrumSamplerAudioProcessor::fxAutomationTargetName(const juce::String& targetID)
{
    if (fxAutomationTargetCode(targetID) < 0)
        return {};

    juce::StringArray tokens;
    tokens.addTokens(targetID, ".", {});
    const auto type = tokens[3].toUpperCase();
    const auto parameter = tokens[4];
    juce::String label = parameter;
    if      (parameter == "bypass")      label = "Bypass";
    else if (parameter == "type")        label = "Type";
    else if (parameter == "amount")      label = "Drive";
    else if (parameter == "tone")        label = "Tone";
    else if (parameter == "mix")         label = "Mix";
    else if (parameter == "output")      label = "Output";
    else if (parameter == "attack")      label = "Attack";
    else if (parameter == "sustain")     label = "Sustain";
    else if (parameter == "threshold")   label = "Threshold";
    else if (parameter == "ratio")       label = "Ratio";
    else if (parameter == "release")     label = "Release";
    else if (parameter == "makeup")      label = "Make Up";
    else if (parameter == "hpEnabled")   label = "High-Pass On";
    else if (parameter == "hpCutoff")    label = "High-Pass Frequency";
    else if (parameter == "hpSlope")     label = "High-Pass Slope";
    else if (parameter == "hpResonance") label = "High-Pass Resonance";
    else if (parameter == "lpEnabled")   label = "Low-Pass On";
    else if (parameter == "lpCutoff")    label = "Low-Pass Frequency";
    else if (parameter == "lpSlope")     label = "Low-Pass Slope";
    else if (parameter == "lpResonance") label = "Low-Pass Resonance";
    return tokens[0].replace("pad", "Pad ") + " "
         + tokens[1].replace("layer", "L") + " " + type + " " + label;
}

float DrumSamplerAudioProcessor::getFxAutomationTargetValue(int targetCode) const
{
    if (targetCode < 0) return 0.0f;
    const int padIndex = (targetCode >> 11) & 63;
    const int layerIndex = (targetCode >> 8) & 7;
    const int type = (targetCode >> 5) & 7;
    const int param = targetCode & 31;
    if (padIndex >= NUM_PADS) return 0.0f;
    const auto& pad = kit.pads[static_cast<size_t>(padIndex)];
    if (layerIndex >= pad.layerCount()) return 0.0f;
    const auto& chain = pad.layers[static_cast<size_t>(layerIndex)].fxChain;
    const LayerFxType wanted = type == 1 ? LayerFxType::Filter
                               : type == 2 ? LayerFxType::Drive
                               : type == 3 ? LayerFxType::Transient
                                           : LayerFxType::Compressor;
    const auto found = std::find_if(chain.begin(), chain.end(), [wanted] (const auto& slot) { return slot.type == wanted; });
    if (found == chain.end()) return 0.0f;
    if (param == 1) return found->bypassed ? 1.0f : 0.0f;
    const auto norm = [] (float value, float min, float max) { return juce::jlimit(0.0f, 1.0f, (value - min) / (max - min)); };
    if (type == 1)
    {
        const auto freqNorm = [] (float hz) { return std::log10(juce::jlimit(20.0f, 20000.0f, hz) / 20.0f) / 3.0f; };
        if (param == 2) return found->filter.hpEnabled ? 1.0f : 0.0f;
        if (param == 3) return freqNorm(found->filter.hpCutoff);
        if (param == 4) return found->filter.hpSlope >= 48 ? 1.0f : found->filter.hpSlope >= 24 ? 0.5f : 0.0f;
        if (param == 5) return norm(found->filter.hpResonance, 0.2f, 8.0f);
        if (param == 6) return found->filter.lpEnabled ? 1.0f : 0.0f;
        if (param == 7) return freqNorm(found->filter.lpCutoff);
        if (param == 8) return found->filter.lpSlope >= 48 ? 1.0f : found->filter.lpSlope >= 24 ? 0.5f : 0.0f;
        if (param == 9) return norm(found->filter.lpResonance, 0.2f, 8.0f);
    }
    if (type == 2)
    {
        if (param == 2) return norm(static_cast<float>(found->drive.type), 0.0f, 6.0f);
        if (param == 3) return found->drive.amount;
        if (param == 4) return found->drive.tone;
        if (param == 5) return found->drive.mix;
        if (param == 6) return norm(found->drive.outputDb, -24.0f, 12.0f);
    }
    if (type == 3)
    {
        if (param == 2) return norm(found->transient.attack, -1.0f, 1.0f);
        if (param == 3) return norm(found->transient.sustain, -1.0f, 1.0f);
        if (param == 4) return norm(found->transient.outputDb, -24.0f, 12.0f);
    }
    if (type == 4)
    {
        if (param == 2) return norm(found->compressor.threshold, -48.0f, 0.0f);
        if (param == 3) return norm(found->compressor.ratio, 1.0f, 20.0f);
        if (param == 4) return norm(found->compressor.attack, 1.0f, 80.0f);
        if (param == 5) return norm(found->compressor.release, 10.0f, 500.0f);
        if (param == 6) return norm(found->compressor.makeupDb, 0.0f, 24.0f);
        if (param == 7) return found->compressor.mix;
        if (param == 8) return norm(found->compressor.outputDb, -24.0f, 12.0f);
    }
    return 0.0f;
}

void DrumSamplerAudioProcessor::applyFxAutomationTargetValue(int targetCode, float normalizedValue)
{
    if (targetCode < 0) return;
    const int padIndex = (targetCode >> 11) & 63;
    const int layerIndex = (targetCode >> 8) & 7;
    const int type = (targetCode >> 5) & 7;
    const int param = targetCode & 31;
    if (padIndex >= NUM_PADS) return;
    auto& pad = kit.pads[static_cast<size_t>(padIndex)];
    if (layerIndex >= pad.layerCount()) return;
    auto& chain = pad.layers[static_cast<size_t>(layerIndex)].fxChain;
    const LayerFxType wanted = type == 1 ? LayerFxType::Filter
                               : type == 2 ? LayerFxType::Drive
                               : type == 3 ? LayerFxType::Transient
                                           : LayerFxType::Compressor;
    const auto found = std::find_if(chain.begin(), chain.end(), [wanted] (const auto& slot) { return slot.type == wanted; });
    if (found == chain.end()) return;
    const float n = juce::jlimit(0.0f, 1.0f, normalizedValue);
    const auto denorm = [n] (float min, float max) { return min + n * (max - min); };
    if (param == 1) { found->bypassed = n >= 0.5f; return; }
    if (type == 1)
    {
        const float freq = 20.0f * std::pow(1000.0f, n);
        if      (param == 2) found->filter.hpEnabled = n >= 0.5f;
        else if (param == 3) found->filter.hpCutoff = freq;
        else if (param == 4) found->filter.hpSlope = n >= 0.75f ? 48 : n >= 0.25f ? 24 : 12;
        else if (param == 5) found->filter.hpResonance = denorm(0.2f, 8.0f);
        else if (param == 6) found->filter.lpEnabled = n >= 0.5f;
        else if (param == 7) found->filter.lpCutoff = freq;
        else if (param == 8) found->filter.lpSlope = n >= 0.75f ? 48 : n >= 0.25f ? 24 : 12;
        else if (param == 9) found->filter.lpResonance = denorm(0.2f, 8.0f);
    }
    else if (type == 2)
    {
        if      (param == 2) found->drive.type = juce::jlimit(0, 6, juce::roundToInt(n * 6.0f));
        else if (param == 3) found->drive.amount = n;
        else if (param == 4) found->drive.tone = n;
        else if (param == 5) found->drive.mix = n;
        else if (param == 6) found->drive.outputDb = denorm(-24.0f, 12.0f);
    }
    else if (type == 3)
    {
        if      (param == 2) found->transient.attack = denorm(-1.0f, 1.0f);
        else if (param == 3) found->transient.sustain = denorm(-1.0f, 1.0f);
        else if (param == 4) found->transient.outputDb = denorm(-24.0f, 12.0f);
    }
    else if (type == 4)
    {
        if      (param == 2) found->compressor.threshold = denorm(-48.0f, 0.0f);
        else if (param == 3) found->compressor.ratio = denorm(1.0f, 20.0f);
        else if (param == 4) found->compressor.attack = denorm(1.0f, 80.0f);
        else if (param == 5) found->compressor.release = denorm(10.0f, 500.0f);
        else if (param == 6) found->compressor.makeupDb = denorm(0.0f, 24.0f);
        else if (param == 7) found->compressor.mix = n;
        else if (param == 8) found->compressor.outputDb = denorm(-24.0f, 12.0f);
    }
}

void DrumSamplerAudioProcessor::syncFxAutomationSlots()
{
    for (int slot = 0; slot < automationSlotCount; ++slot)
        if (automationSlotFxDirty[static_cast<size_t>(slot)].exchange(false, std::memory_order_acq_rel))
            applyFxAutomationTargetValue(
                automationSlotFxTargetCodes[static_cast<size_t>(slot)].load(std::memory_order_acquire),
                automationSlotFxValues[static_cast<size_t>(slot)].load(std::memory_order_acquire));
}

void DrumSamplerAudioProcessor::setAutomationSlotTarget(int slotIndex,
                                                        const juce::String& parameterID)
{
    if (! juce::isPositiveAndBelow(slotIndex, automationSlotCount))
        return;

    const int parameterIndex = parameterID.isNotEmpty() ? findParameterIndex(parameterID) : -1;
    const int fxTargetCode = parameterID.isNotEmpty() ? fxAutomationTargetCode(parameterID) : -1;
    if (parameterID.isNotEmpty()
        && ((parameterIndex < 0 && fxTargetCode < 0)
            || automationSlotIndexFromParameterID(parameterID) >= 0))
        return;

    // 1つの内部パラメータを複数Slotへ割り当てない。
    for (int slot = 0; slot < automationSlotCount; ++slot)
    {
        if (slot != slotIndex && automationSlotTargets[static_cast<size_t>(slot)] == parameterID)
        {
            automationSlotTargets[static_cast<size_t>(slot)].clear();
            automationSlotTargetIndices[static_cast<size_t>(slot)].store(-1, std::memory_order_release);
            automationSlotFxTargetCodes[static_cast<size_t>(slot)].store(-1, std::memory_order_release);
            automationSlotFxDirty[static_cast<size_t>(slot)].store(false, std::memory_order_release);
        }
    }

    automationSlotTargets[static_cast<size_t>(slotIndex)] = parameterID;
    automationSlotTargetIndices[static_cast<size_t>(slotIndex)].store(parameterIndex,
                                                                      std::memory_order_release);
    automationSlotFxTargetCodes[static_cast<size_t>(slotIndex)].store(fxTargetCode,
                                                                      std::memory_order_release);
    automationSlotFxDirty[static_cast<size_t>(slotIndex)].store(false, std::memory_order_release);

    if (parameterIndex >= 0)
    {
        auto* slotParameter = parameters.getParameter(automationSlotParameterID(slotIndex));
        const auto& processorParameters = getParameters();
        if (slotParameter != nullptr && parameterIndex < processorParameters.size())
        {
            suppressParameterCallbacks.store(true, std::memory_order_release);
            slotParameter->setValue(processorParameters.getUnchecked(parameterIndex)->getValue());
            suppressParameterCallbacks.store(false, std::memory_order_release);
        }
    }
    else if (fxTargetCode >= 0)
    {
        if (auto* slotParameter = parameters.getParameter(automationSlotParameterID(slotIndex)))
        {
            const float value = getFxAutomationTargetValue(fxTargetCode);
            automationSlotFxValues[static_cast<size_t>(slotIndex)].store(value, std::memory_order_release);
            suppressParameterCallbacks.store(true, std::memory_order_release);
            slotParameter->setValue(value);
            suppressParameterCallbacks.store(false, std::memory_order_release);
        }
    }

    automationLearnSlot.store(-1, std::memory_order_release);
    automationSlotsChanged.store(true, std::memory_order_release);
}

void DrumSamplerAudioProcessor::beginAutomationLearn(int slotIndex) noexcept
{
    if (! juce::isPositiveAndBelow(slotIndex, automationSlotCount))
        return;
    automationLearnSlot.store(slotIndex, std::memory_order_release);
    automationSlotsChanged.store(true, std::memory_order_release);
}

void DrumSamplerAudioProcessor::cancelAutomationLearn() noexcept
{
    automationLearnSlot.store(-1, std::memory_order_release);
    automationSlotsChanged.store(true, std::memory_order_release);
}

void DrumSamplerAudioProcessor::assignAutomationSlot(int slotIndex,
                                                      const juce::String& parameterID)
{
    setAutomationSlotTarget(slotIndex, parameterID);
}

void DrumSamplerAudioProcessor::clearAutomationSlot(int slotIndex)
{
    setAutomationSlotTarget(slotIndex, {});
}

juce::String DrumSamplerAudioProcessor::getAutomationSlotTargetID(int slotIndex) const
{
    return juce::isPositiveAndBelow(slotIndex, automationSlotCount)
        ? automationSlotTargets[static_cast<size_t>(slotIndex)]
        : juce::String{};
}

juce::String DrumSamplerAudioProcessor::getAutomationSlotTargetName(int slotIndex) const
{
    const int parameterIndex = juce::isPositiveAndBelow(slotIndex, automationSlotCount)
        ? automationSlotTargetIndices[static_cast<size_t>(slotIndex)].load(std::memory_order_acquire)
        : -1;
    const auto& processorParameters = getParameters();
    if (parameterIndex >= 0 && parameterIndex < processorParameters.size())
        return processorParameters.getUnchecked(parameterIndex)->getName(128);
    return juce::isPositiveAndBelow(slotIndex, automationSlotCount)
        ? fxAutomationTargetName(automationSlotTargets[static_cast<size_t>(slotIndex)])
        : juce::String{};
}

void DrumSamplerAudioProcessor::setFxAutomationTargetValue(const juce::String& targetID,
                                                            float normalizedValue,
                                                            bool notifyHost)
{
    const int targetCode = fxAutomationTargetCode(targetID);
    if (targetCode < 0) return;
    const float value = juce::jlimit(0.0f, 1.0f, normalizedValue);
    if (notifyHost)
        captureAutomationLearnTarget(targetID);

    const int slotIndex = notifyHost ? assignedAutomationSlotForTarget(targetID) : -1;
    if (slotIndex >= 0)
    {
        if (auto* slotParameter = parameters.getParameter(automationSlotParameterID(slotIndex)))
        {
            slotParameter->beginChangeGesture();
            suppressParameterCallbacks.store(true, std::memory_order_release);
            slotParameter->setValueNotifyingHost(value);
            suppressParameterCallbacks.store(false, std::memory_order_release);
            slotParameter->endChangeGesture();
            automationSlotFxValues[static_cast<size_t>(slotIndex)].store(value, std::memory_order_release);
        }
    }
    applyFxAutomationTargetValue(targetCode, value);
}

int DrumSamplerAudioProcessor::getAutomationLearnSlot() const noexcept
{
    return automationLearnSlot.load(std::memory_order_acquire);
}

bool DrumSamplerAudioProcessor::consumeAutomationSlotsChanged() noexcept
{
    return automationSlotsChanged.exchange(false, std::memory_order_acq_rel);
}

void DrumSamplerAudioProcessor::captureAutomationLearnTarget(const juce::String& parameterID)
{
    const int slotIndex = automationLearnSlot.exchange(-1, std::memory_order_acq_rel);
    if (slotIndex >= 0)
        setAutomationSlotTarget(slotIndex, parameterID);
}

void DrumSamplerAudioProcessor::setParameterValueFromUi(const juce::String& parameterID,
                                                        float normalizedValue,
                                                        bool notifyHost)
{
    auto* targetParameter = parameters.getParameter(parameterID);
    if (targetParameter == nullptr)
        return;

    const float normalized = juce::jlimit(0.0f, 1.0f, normalizedValue);
    if (notifyHost)
        captureAutomationLearnTarget(parameterID);

    const int slotIndex = notifyHost ? assignedAutomationSlotForTarget(parameterID) : -1;
    auto* hostParameter = slotIndex >= 0
        ? parameters.getParameter(automationSlotParameterID(slotIndex))
        : targetParameter;
    if (hostParameter == nullptr)
        hostParameter = targetParameter;

    if (notifyHost)
        hostParameter->beginChangeGesture();

    suppressParameterCallbacks.store(true, std::memory_order_release);
    if (hostParameter != targetParameter)
        targetParameter->setValueNotifyingHost(normalized);
    hostParameter->setValueNotifyingHost(normalized);
    suppressParameterCallbacks.store(false, std::memory_order_release);

    if (notifyHost)
        hostParameter->endChangeGesture();
}

void DrumSamplerAudioProcessor::registerParameterListeners()
{
    parameters.addParameterListener(kMasterVolumeParamId, this);

    for (int slot = 0; slot < automationSlotCount; ++slot)
        parameters.addParameterListener(automationSlotParameterID(slot), this);

    for (int padIndex = 0; padIndex < NUM_PADS; ++padIndex)
    {
        for (const auto& spec : PadParameterSpecs::all())
            parameters.addParameterListener(PadParameterSpecs::parameterID(padIndex, spec.param), this);

        for (int layerIndex = 0; layerIndex < MAX_LAYERS_PER_PAD; ++layerIndex)
            for (const auto& spec : LayerParameterSpecs::all())
                parameters.addParameterListener(LayerParameterSpecs::parameterID(padIndex, layerIndex, spec.param), this);
    }
}

void DrumSamplerAudioProcessor::removeParameterListeners()
{
    parameters.removeParameterListener(kMasterVolumeParamId, this);

    for (int slot = 0; slot < automationSlotCount; ++slot)
        parameters.removeParameterListener(automationSlotParameterID(slot), this);

    for (int padIndex = 0; padIndex < NUM_PADS; ++padIndex)
    {
        for (const auto& spec : PadParameterSpecs::all())
            parameters.removeParameterListener(PadParameterSpecs::parameterID(padIndex, spec.param), this);

        for (int layerIndex = 0; layerIndex < MAX_LAYERS_PER_PAD; ++layerIndex)
            for (const auto& spec : LayerParameterSpecs::all())
                parameters.removeParameterListener(LayerParameterSpecs::parameterID(padIndex, layerIndex, spec.param), this);
    }
}

void DrumSamplerAudioProcessor::parameterChanged(const juce::String& parameterID, float newValue)
{
    if (! suppressParameterCallbacks.load(std::memory_order_relaxed))
    {
        const int slotIndex = automationSlotIndexFromParameterID(parameterID);
        if (slotIndex >= 0)
        {
            const int targetIndex = automationSlotTargetIndices[static_cast<size_t>(slotIndex)]
                                        .load(std::memory_order_acquire);
            const auto& processorParameters = getParameters();
            if (targetIndex >= 0 && targetIndex < processorParameters.size())
            {
                suppressParameterCallbacks.store(true, std::memory_order_release);
                // APVTSのraw値も更新するため内部parameterにもlistener通知を通す。
                // suppress中なので再帰的なkit syncは起こらない。
                processorParameters.getUnchecked(targetIndex)->setValueNotifyingHost(newValue);
                suppressParameterCallbacks.store(false, std::memory_order_release);
            }
            else
            {
                const auto index = static_cast<size_t>(slotIndex);
                if (automationSlotFxTargetCodes[index].load(std::memory_order_acquire) >= 0)
                {
                    automationSlotFxValues[index].store(newValue, std::memory_order_release);
                    automationSlotFxDirty[index].store(true, std::memory_order_release);
                }
            }
        }

        parametersNeedSync.store(true, std::memory_order_release);
        // UI へ通知するためのフラグ — Timer がこれを拾って broadcastKitState を呼ぶ
        kitChangedByAutomation.store(true, std::memory_order_release);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// DAW Automation parameter layout
// ─────────────────────────────────────────────────────────────────────────────
juce::AudioProcessorValueTreeState::ParameterLayout
DrumSamplerAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    const size_t totalParams =
        static_cast<size_t>(NUM_PADS * PadParameterSpecs::numAutomatableParams)
      + static_cast<size_t>(NUM_PADS * MAX_LAYERS_PER_PAD * LayerParameterSpecs::numAutomatableParams)
      + static_cast<size_t>(automationSlotCount);
    params.reserve(totalParams);

    KitData defaultKit;

    // グローバル: マスター出力ボリューム (フェーダー位置 0..1、デフォルト 0.75 = 0 dB)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { kMasterVolumeParamId, 1 },
        "Master Volume",
        juce::NormalisableRange<float> { 0.0f, 1.0f },
        defaultKit.masterVolume,
        juce::AudioParameterFloatAttributes().withAutomatable(false)));

    for (int padIndex = 0; padIndex < NUM_PADS; ++padIndex)
    {
        const auto& pad = defaultKit.pads[static_cast<size_t>(padIndex)];

        for (const auto& spec : PadParameterSpecs::all())
        {
            // New parameters are appended after the complete legacy layout so
            // existing AU/VST3 parameter indices remain stable.
            if (spec.param == PadParameterSpecs::Param::PadVolume
                || spec.param == PadParameterSpecs::Param::PadPan
                || spec.param == PadParameterSpecs::Param::PadPitch
                || spec.param == PadParameterSpecs::Param::PadFine)
                continue;

            const auto id   = PadParameterSpecs::parameterID(padIndex, spec.param);
            const auto name = PadParameterSpecs::parameterName(padIndex, pad, spec.param);

            if (spec.isBoolean)
            {
                params.push_back(std::make_unique<juce::AudioParameterBool>(
                    juce::ParameterID { id, 1 },
                    name,
                    spec.defaultValue >= 0.5f,
                    juce::AudioParameterBoolAttributes().withAutomatable(false)));
            }
            else
            {
                params.push_back(std::make_unique<juce::AudioParameterFloat>(
                    juce::ParameterID { id, 1 },
                    name,
                    rangeFor(spec),
                    spec.defaultValue,
                    juce::AudioParameterFloatAttributes().withAutomatable(false)));
            }
        }

        // v7+: Layer 1..8 × 5 params (Volume, Pan, Pitch, VelMin, VelMax)
        for (int layerIndex = 0; layerIndex < MAX_LAYERS_PER_PAD; ++layerIndex)
        {
            for (const auto& spec : LayerParameterSpecs::all())
            {
                const auto id   = LayerParameterSpecs::parameterID(padIndex, layerIndex, spec.param);
                const auto name = LayerParameterSpecs::parameterName(padIndex, layerIndex, pad, spec.param);
                if (spec.isBoolean)
                {
                    params.push_back(std::make_unique<juce::AudioParameterBool>(
                        juce::ParameterID { id, 1 },
                        name,
                        spec.defaultValue >= 0.5f,
                        juce::AudioParameterBoolAttributes().withAutomatable(false)));
                }
                else
                {
                    params.push_back(std::make_unique<juce::AudioParameterFloat>(
                        juce::ParameterID { id, 1 },
                        name,
                        rangeFor(spec),
                        spec.defaultValue,
                        juce::AudioParameterFloatAttributes().withAutomatable(false)));
                }
            }
        }
    }

    for (const auto appendedParam : {
            PadParameterSpecs::Param::PadVolume,
            PadParameterSpecs::Param::PadPan,
            PadParameterSpecs::Param::PadPitch,
            PadParameterSpecs::Param::PadFine })
    {
        const auto& spec = PadParameterSpecs::specFor(appendedParam);
        for (int padIndex = 0; padIndex < NUM_PADS; ++padIndex)
        {
            const auto& pad = defaultKit.pads[static_cast<size_t>(padIndex)];
            params.push_back(std::make_unique<juce::AudioParameterFloat>(
                juce::ParameterID { PadParameterSpecs::parameterID(padIndex, spec.param), 1 },
                PadParameterSpecs::parameterName(padIndex, pad, spec.param),
                rangeFor(spec),
                spec.defaultValue,
                juce::AudioParameterFloatAttributes().withAutomatable(false)));
        }
    }

    // DAWにはこの24個だけをautomation対象として公開する。IDと順番は永久固定。
    for (int slot = 0; slot < automationSlotCount; ++slot)
    {
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { automationSlotParameterID(slot), 1 },
            "ASTER AUTO " + juce::String(slot + 1).paddedLeft('0', 2),
            juce::NormalisableRange<float> { 0.0f, 1.0f },
            0.0f,
            juce::AudioParameterFloatAttributes()
                .withAutomatable(true)
                .withMeta(true)));
    }

    return { params.begin(), params.end() };
}

float DrumSamplerAudioProcessor::getKitValueForParameter(int padIndex,
                                                         PadParameterSpecs::Param param) const
{
    if (padIndex < 0 || padIndex >= NUM_PADS) return 0.0f;
    const auto& pad = kit.pads[static_cast<size_t>(padIndex)];

    switch (param)
    {
        case PadParameterSpecs::Param::Volume:  return pad.volume;
        case PadParameterSpecs::Param::Pan:     return pad.pan;
        case PadParameterSpecs::Param::Pitch:   return pad.pitch;
        case PadParameterSpecs::Param::Attack:  return pad.attack;
        case PadParameterSpecs::Param::Release: return pad.release;
        case PadParameterSpecs::Param::Start:   return pad.startPosition;
        case PadParameterSpecs::Param::End:     return pad.endPosition;
        case PadParameterSpecs::Param::FadeIn:  return pad.fadeIn;
        case PadParameterSpecs::Param::FadeOut: return pad.fadeOut;
        case PadParameterSpecs::Param::Reverse: return pad.reverse ? 1.0f : 0.0f;
        case PadParameterSpecs::Param::Mute:    return pad.mute    ? 1.0f : 0.0f;
        case PadParameterSpecs::Param::Solo:    return pad.solo    ? 1.0f : 0.0f;
        case PadParameterSpecs::Param::Humanize: return pad.humanize;
        case PadParameterSpecs::Param::Velocity: return pad.velocitySens;
        case PadParameterSpecs::Param::PadVolume: return pad.padVolume;
        case PadParameterSpecs::Param::PadPan: return pad.padPan;
        case PadParameterSpecs::Param::PadPitch: return pad.padPitch;
        case PadParameterSpecs::Param::PadFine: return pad.padFine;
    }

    return 0.0f;
}

void DrumSamplerAudioProcessor::setKitValueFromParameter(int padIndex,
                                                         PadParameterSpecs::Param param,
                                                         float value)
{
    if (padIndex < 0 || padIndex >= NUM_PADS) return;
    auto& pad = kit.pads[static_cast<size_t>(padIndex)];
    const auto& spec = PadParameterSpecs::specFor(param);
    const float v = juce::jlimit(spec.minValue, spec.maxValue, value);

    switch (param)
    {
        case PadParameterSpecs::Param::Volume:  pad.volume = v; break;
        case PadParameterSpecs::Param::Pan:     pad.pan = v; break;
        case PadParameterSpecs::Param::Pitch:   pad.pitch = v; break;
        case PadParameterSpecs::Param::Attack:  pad.attack = v; break;
        case PadParameterSpecs::Param::Release: pad.release = v; break;
        case PadParameterSpecs::Param::Start:
            pad.startPosition = juce::jlimit(0.0f, pad.endPosition - kMinTrimGap, v);
            break;
        case PadParameterSpecs::Param::End:
            pad.endPosition = juce::jlimit(pad.startPosition + kMinTrimGap, 1.0f, v);
            break;
        case PadParameterSpecs::Param::FadeIn:
            pad.fadeIn = juce::jlimit(0.0f, juce::jmax(0.0f, 1.0f - pad.fadeOut), v);
            break;
        case PadParameterSpecs::Param::FadeOut:
            pad.fadeOut = juce::jlimit(0.0f, juce::jmax(0.0f, 1.0f - pad.fadeIn), v);
            break;
        case PadParameterSpecs::Param::Reverse: pad.reverse = v >= 0.5f; break;
        case PadParameterSpecs::Param::Mute:    pad.mute    = v >= 0.5f; break;
        case PadParameterSpecs::Param::Solo:    pad.solo    = v >= 0.5f; break;
        case PadParameterSpecs::Param::Humanize: pad.humanize     = v; break;
        case PadParameterSpecs::Param::Velocity: pad.velocitySens = v; break;
        case PadParameterSpecs::Param::PadVolume: pad.padVolume   = v; break;
        case PadParameterSpecs::Param::PadPan: pad.padPan         = v; break;
        case PadParameterSpecs::Param::PadPitch: pad.padPitch     = v; break;
        case PadParameterSpecs::Param::PadFine: pad.padFine       = v; break;
    }

    // Pad-level controls are separate stages after Layer processing. Legacy
    // flat parameters still mirror into Layer 0 for backwards compatibility.
    if (param != PadParameterSpecs::Param::PadVolume
        && param != PadParameterSpecs::Param::PadPan
        && param != PadParameterSpecs::Param::PadPitch
        && param != PadParameterSpecs::Param::PadFine)
        pad.syncLayer0FromFlat();
}

void DrumSamplerAudioProcessor::setAutomatablePadParameter(int padIndex,
                                                           PadParameterSpecs::Param param,
                                                           float value,
                                                           bool notifyHost)
{
    if (padIndex < 0 || padIndex >= NUM_PADS) return;
    const auto id = PadParameterSpecs::parameterID(padIndex, param);
    auto* parameter = parameters.getParameter(id);
    if (parameter == nullptr) return;

    const auto& spec = PadParameterSpecs::specFor(param);
    const float clamped = juce::jlimit(spec.minValue, spec.maxValue, value);
    const float currentValue = getKitValueForParameter(padIndex, param);

    setParameterValueFromUi(id, parameter->convertTo0to1(clamped), notifyHost);

    setKitValueFromParameter(padIndex, param, clamped);

    if (notifyHost && std::abs(currentValue - clamped) > 0.000001f)
        markKitDirty();
}

void DrumSamplerAudioProcessor::setAutomatableLayerParameter(int padIndex,
                                                             int layerIndex,
                                                             LayerParameterSpecs::Param param,
                                                             float value,
                                                             bool notifyHost)
{
    if (padIndex < 0 || padIndex >= NUM_PADS) return;
    auto& pad = kit.pads[static_cast<size_t>(padIndex)];
    if (layerIndex < 0 || layerIndex >= pad.layerCount()) return;

    const auto id = LayerParameterSpecs::parameterID(padIndex, layerIndex, param);
    auto* parameter = parameters.getParameter(id);
    if (parameter == nullptr) return;

    const auto& spec = LayerParameterSpecs::specFor(param);
    const float clamped = juce::jlimit(spec.minValue, spec.maxValue, value);
    const float currentValue = getLayerValueForParameter(padIndex, layerIndex,
                                                         static_cast<int>(param));

    setParameterValueFromUi(id, parameter->convertTo0to1(clamped), notifyHost);

    setLayerValueFromParameter(padIndex, layerIndex, static_cast<int>(param), clamped);

    if (notifyHost && std::abs(currentValue - clamped) > 0.000001f)
        markKitDirty();
}

void DrumSamplerAudioProcessor::setPadSampleTrim(int padIndex,
                                                 float startPosition,
                                                 float endPosition,
                                                 float fadeIn,
                                                 float fadeOut,
                                                 bool notifyHost)
{
    if (padIndex < 0 || padIndex >= NUM_PADS) return;

    auto& pad = kit.pads[static_cast<size_t>(padIndex)];
    const auto normalized = normalizeTrim(startPosition, endPosition, fadeIn, fadeOut);
    const bool changed =
        std::abs(pad.startPosition - normalized.start) > 0.000001f ||
        std::abs(pad.endPosition   - normalized.end) > 0.000001f ||
        std::abs(pad.fadeIn        - normalized.fadeIn) > 0.000001f ||
        std::abs(pad.fadeOut       - normalized.fadeOut) > 0.000001f;

    const auto setParameter = [&] (PadParameterSpecs::Param param, float value)
    {
        const auto id = PadParameterSpecs::parameterID(padIndex, param);
        auto* parameter = parameters.getParameter(id);
        if (parameter == nullptr) return;

        const auto& spec = PadParameterSpecs::specFor(param);
        const float clamped = juce::jlimit(spec.minValue, spec.maxValue, value);
        const float normalizedValue = parameter->convertTo0to1(clamped);

        setParameterValueFromUi(id, normalizedValue, notifyHost);
    };

    setParameter(PadParameterSpecs::Param::Start,   normalized.start);
    setParameter(PadParameterSpecs::Param::End,     normalized.end);
    setParameter(PadParameterSpecs::Param::FadeIn,  normalized.fadeIn);
    setParameter(PadParameterSpecs::Param::FadeOut, normalized.fadeOut);

    pad.startPosition = normalized.start;
    pad.endPosition   = normalized.end;
    pad.fadeIn        = normalized.fadeIn;
    pad.fadeOut       = normalized.fadeOut;
    pad.syncLayer0FromFlat();

    if (notifyHost && changed)
        markKitDirty();
}

float DrumSamplerAudioProcessor::getAutomatablePadParameter(int padIndex,
                                                            PadParameterSpecs::Param param) const
{
    return getKitValueForParameter(padIndex, param);
}

void DrumSamplerAudioProcessor::setMasterVolumeParameter(float position, bool notifyHost)
{
    auto* parameter = parameters.getParameter(kMasterVolumeParamId);
    if (parameter == nullptr) return;

    const float clamped    = juce::jlimit(0.0f, 1.0f, position);
    const float normalized = parameter->convertTo0to1(clamped);

    setParameterValueFromUi(kMasterVolumeParamId, normalized, notifyHost);

    kit.masterVolume = clamped;
    if (notifyHost)
        markKitDirty();
}

void DrumSamplerAudioProcessor::syncParametersFromKit()
{
    suppressParameterCallbacks.store(true, std::memory_order_release);

    if (auto* mv = parameters.getParameter(kMasterVolumeParamId))
        mv->setValueNotifyingHost(mv->convertTo0to1(juce::jlimit(0.0f, 1.0f, kit.masterVolume)));

    for (int padIndex = 0; padIndex < NUM_PADS; ++padIndex)
    {
        for (const auto& spec : PadParameterSpecs::all())
        {
            const auto id = PadParameterSpecs::parameterID(padIndex, spec.param);
            if (auto* parameter = parameters.getParameter(id))
            {
                const float value = getKitValueForParameter(padIndex, spec.param);
                parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
            }
        }

        // v7+: Layer params
        for (int layerIndex = 0; layerIndex < MAX_LAYERS_PER_PAD; ++layerIndex)
        {
            for (const auto& spec : LayerParameterSpecs::all())
            {
                const auto id = LayerParameterSpecs::parameterID(padIndex, layerIndex, spec.param);
                if (auto* parameter = parameters.getParameter(id))
                {
                    const float value = getLayerValueForParameter(padIndex, layerIndex,
                                                                  static_cast<int>(spec.param));
                    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
                }
            }
        }
    }

    suppressParameterCallbacks.store(false, std::memory_order_release);
    parametersNeedSync.store(false, std::memory_order_release);
}

void DrumSamplerAudioProcessor::syncKitFromParameters()
{
    if (const auto* mv = parameters.getRawParameterValue(kMasterVolumeParamId))
        kit.masterVolume = juce::jlimit(0.0f, 1.0f, mv->load());

    for (int padIndex = 0; padIndex < NUM_PADS; ++padIndex)
    {
        for (const auto& spec : PadParameterSpecs::all())
        {
            const auto id = PadParameterSpecs::parameterID(padIndex, spec.param);
            if (const auto* value = parameters.getRawParameterValue(id))
                setKitValueFromParameter(padIndex, spec.param, value->load());
        }

        // v7+: Layer params。Pad-level mirror (Layer 0 = flat) と矛盾しないよう
        // Pad-level を書いてから Layer を書く順序にしてある。Layer 0 を後から書くと
        // syncLayer0FromFlat (Pad-level write 内で呼ばれる) で上書きされた Layer 0
        // を、こちらが automation 値で再上書きすることになる — Layer 0 用 automation が
        // 別途存在するため意図通り。
        const auto layerCount = kit.pads[(size_t) padIndex].layerCount();
        for (int layerIndex = 0; layerIndex < MAX_LAYERS_PER_PAD; ++layerIndex)
        {
            if (layerIndex >= layerCount) continue; // 存在しない layer は kit 側スキップ
            for (const auto& spec : LayerParameterSpecs::all())
            {
                // MAIN has legacy Pad parameters for these same three values.
                // The UI writes those Pad parameters, so the duplicate Layer-0
                // parameters may still contain their defaults.  Letting them
                // win here resets MAIN to 0 dB / centre / zero pitch when a kit
                // is saved.  Keep the long-standing Pad parameters canonical.
                if (layerIndex == 0
                    && (spec.param == LayerParameterSpecs::Param::Volume
                        || spec.param == LayerParameterSpecs::Param::Pan
                        || spec.param == LayerParameterSpecs::Param::Pitch))
                    continue;

                const auto id = LayerParameterSpecs::parameterID(padIndex, layerIndex, spec.param);
                if (const auto* value = parameters.getRawParameterValue(id))
                    setLayerValueFromParameter(padIndex, layerIndex,
                                               static_cast<int>(spec.param), value->load());
            }
        }
    }
}

// ── v7+: Layer 単位 get/set ──────────────────────────────────────────
float DrumSamplerAudioProcessor::getLayerValueForParameter(int padIndex,
                                                           int layerIndex,
                                                           int paramRaw) const
{
    const auto param = static_cast<LayerParameterSpecs::Param>(paramRaw);
    if (padIndex < 0 || padIndex >= NUM_PADS)
        return LayerParameterSpecs::specFor(param).defaultValue;
    const auto& pad = kit.pads[static_cast<size_t>(padIndex)];
    if (layerIndex < 0 || layerIndex >= pad.layerCount())
        return LayerParameterSpecs::specFor(param).defaultValue;
    const auto& L = pad.layers[static_cast<size_t>(layerIndex)];

    switch (param)
    {
        case LayerParameterSpecs::Param::Volume: return L.volume;
        case LayerParameterSpecs::Param::Pan:    return L.pan;
        case LayerParameterSpecs::Param::Pitch:  return L.pitch;
        case LayerParameterSpecs::Param::Fine:   return L.fine;
        case LayerParameterSpecs::Param::VelMin: return static_cast<float>(L.velocityMin) / 127.0f;
        case LayerParameterSpecs::Param::VelMax: return static_cast<float>(L.velocityMax) / 127.0f;
        case LayerParameterSpecs::Param::EqBypass:      return L.eq.bypassed ? 1.0f : 0.0f;
        case LayerParameterSpecs::Param::EqLowMode:     return L.eq.lowMode == LayerEqEdgeMode::Cut ? 1.0f : 0.0f;
        case LayerParameterSpecs::Param::EqHighMode:    return L.eq.highMode == LayerEqEdgeMode::Cut ? 1.0f : 0.0f;
        case LayerParameterSpecs::Param::EqLowFreq:     return L.eq.low.freq;
        case LayerParameterSpecs::Param::EqLowGain:     return L.eq.low.gain;
        case LayerParameterSpecs::Param::EqLowQ:        return L.eq.low.q;
        case LayerParameterSpecs::Param::EqLowMidFreq:  return L.eq.lowMid.freq;
        case LayerParameterSpecs::Param::EqLowMidGain:  return L.eq.lowMid.gain;
        case LayerParameterSpecs::Param::EqLowMidQ:     return L.eq.lowMid.q;
        case LayerParameterSpecs::Param::EqHighMidFreq: return L.eq.highMid.freq;
        case LayerParameterSpecs::Param::EqHighMidGain: return L.eq.highMid.gain;
        case LayerParameterSpecs::Param::EqHighMidQ:    return L.eq.highMid.q;
        case LayerParameterSpecs::Param::EqHighFreq:    return L.eq.high.freq;
        case LayerParameterSpecs::Param::EqHighGain:    return L.eq.high.gain;
        case LayerParameterSpecs::Param::EqHighQ:       return L.eq.high.q;
    }
    return 0.0f;
}

void DrumSamplerAudioProcessor::setLayerValueFromParameter(int padIndex,
                                                           int layerIndex,
                                                           int paramRaw,
                                                           float value)
{
    if (padIndex < 0 || padIndex >= NUM_PADS) return;
    auto& pad = kit.pads[static_cast<size_t>(padIndex)];
    if (layerIndex < 0 || layerIndex >= pad.layerCount()) return;
    auto& L = pad.layers[static_cast<size_t>(layerIndex)];
    const auto param = static_cast<LayerParameterSpecs::Param>(paramRaw);
    const auto& spec = LayerParameterSpecs::specFor(param);
    const float v = juce::jlimit(spec.minValue, spec.maxValue, value);

    switch (param)
    {
        case LayerParameterSpecs::Param::Volume: L.volume = v; break;
        case LayerParameterSpecs::Param::Pan:    L.pan    = v; break;
        case LayerParameterSpecs::Param::Pitch:  L.pitch  = v; break;
        case LayerParameterSpecs::Param::Fine:   L.fine   = v; break;
        case LayerParameterSpecs::Param::VelMin:
        {
            const int mn = juce::jlimit(0, 127, static_cast<int>(std::round(v * 127.0f)));
            L.velocityMin = juce::jmin(mn, L.velocityMax);
            break;
        }
        case LayerParameterSpecs::Param::VelMax:
        {
            const int mx = juce::jlimit(0, 127, static_cast<int>(std::round(v * 127.0f)));
            L.velocityMax = juce::jmax(L.velocityMin, mx);
            break;
        }
        case LayerParameterSpecs::Param::EqBypass:      L.eq.bypassed = v >= 0.5f; break;
        case LayerParameterSpecs::Param::EqLowMode:     L.eq.lowMode  = v >= 0.5f ? LayerEqEdgeMode::Cut : LayerEqEdgeMode::Shelf; break;
        case LayerParameterSpecs::Param::EqHighMode:    L.eq.highMode = v >= 0.5f ? LayerEqEdgeMode::Cut : LayerEqEdgeMode::Shelf; break;
        case LayerParameterSpecs::Param::EqLowFreq:     L.eq.low.freq = v; break;
        case LayerParameterSpecs::Param::EqLowGain:     L.eq.low.gain = v; break;
        case LayerParameterSpecs::Param::EqLowQ:        L.eq.low.q = v; break;
        case LayerParameterSpecs::Param::EqLowMidFreq:  L.eq.lowMid.freq = v; break;
        case LayerParameterSpecs::Param::EqLowMidGain:  L.eq.lowMid.gain = v; break;
        case LayerParameterSpecs::Param::EqLowMidQ:     L.eq.lowMid.q = v; break;
        case LayerParameterSpecs::Param::EqHighMidFreq: L.eq.highMid.freq = v; break;
        case LayerParameterSpecs::Param::EqHighMidGain: L.eq.highMid.gain = v; break;
        case LayerParameterSpecs::Param::EqHighMidQ:    L.eq.highMid.q = v; break;
        case LayerParameterSpecs::Param::EqHighFreq:    L.eq.high.freq = v; break;
        case LayerParameterSpecs::Param::EqHighGain:    L.eq.high.gain = v; break;
        case LayerParameterSpecs::Param::EqHighQ:       L.eq.high.q = v; break;
    }

    // Layer 0 を書いた場合は flat fields に反映 (Pad-level 表示と一致させる)。
    if (layerIndex == 0)
        pad.syncFlatFromLayer0();
}

// ─────────────────────────────────────────────────────────────────────────────
// エディタ（UI）の生成
// ─────────────────────────────────────────────────────────────────────────────
juce::AudioProcessorEditor* DrumSamplerAudioProcessor::createEditor()
{
   #ifndef DRUM_SAMPLER_USE_LEGACY_UI
    return new WebViewEditor(*this);
   #else
    return new DrumSamplerAudioProcessorEditor(*this);
   #endif
}

// ─────────────────────────────────────────────────────────────────────────────
// JUCE が必要とするファクトリ関数（この関数名・シグネチャは変えないこと）
// ─────────────────────────────────────────────────────────────────────────────
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DrumSamplerAudioProcessor();
}
