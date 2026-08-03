#pragma once
#include <JuceHeader.h>
#include "KitData.h"
#include "AudioFileManager.h"
#include "VoiceManager.h"
#include "PadParameterSpecs.h"
#include "LayerParameterSpecs.h"

// ─────────────────────────────────────────────────────────────────────────────
// DrumSamplerAudioProcessor  ─  プラグインのメインクラス（バックエンド）
//
// このクラスが以下を担当します:
//   - MIDI を受け取って VoiceManager に発音を指示
//   - VoiceManager を使ってオーディオをレンダリング
//   - Kit の保存 / 読み込み（DAW のプリセット / セッション保存）
//   - サンプルの読み込み（UI からの要求を受けて AudioFileManager に転送）
// ─────────────────────────────────────────────────────────────────────────────
class DrumSamplerAudioProcessor : public juce::AudioProcessor,
                                  private juce::AudioProcessorValueTreeState::Listener
{
public:
    DrumSamplerAudioProcessor();
    ~DrumSamplerAudioProcessor() override;

    // ── AudioProcessor 必須メソッド ───────────────────────────────────────
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    // Phase 4: マルチアウトのバスレイアウト（48 ステレオ）
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    // ── エディタ（UI）の生成 ──────────────────────────────────────────────
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    // ── プラグイン情報 ────────────────────────────────────────────────────
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi()  const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }  // リリース分の余韻

    // ── プログラム（プリセット）管理 ──────────────────────────────────────
    // Kitは独自管理する。ホストのプログラムリストは公開しない。
    int  getNumPrograms()   override { return 0; }
    int  getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    // ── Kit 保存 / 読み込み（DAW セッション保存に使われる） ───────────────
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // ── UI から呼ばれる公開 API ───────────────────────────────────────────

    // サンプルを読み込む。padName は絶対に変更しない。
    bool loadSampleForPad(int padIndex, const juce::File& file);
    // 任意 Layer にサンプルを読み込む。layerIndex==0 は loadSampleForPad と等価。
    bool loadSampleForLayer(int padIndex, int layerIndex, const juce::File& file);

    // パッドのサンプルを消去（padName は残す）
    void clearPadSample(int padIndex);
    // 任意 Layer のサンプルだけ消去。layerIndex==0 で flat fields もクリア。
    void clearLayerSample(int padIndex, int layerIndex);

    // ── Pad 操作（コンテキストメニューから呼ばれる） ─────────────────────
    // Copy: 全パラメータをクリップボードへ（midiNote は除く）
    void copyPad(int padIndex);
    // Paste: クリップボードのパラメータを適用 + パスがあればサンプルを再ロード
    void pastePad(int padIndex);
    // クリップボードに有効データがあるか
    bool hasPadClipboard() const noexcept { return clipboardValid; }

    // Clear: サンプル + 設定をリセット（padName / midiNote は保持）
    void clearPadFull(int padIndex);
    // Reset Settings: サンプルは残し、パラメータだけリセット（padName / midiNote は保持）
    void resetPadSettings(int padIndex);
    // Swap: 2 パッドの内容を交換（midiNote は位置に固定で交換しない）
    void swapPads(int a, int b);
    // Replace Sample: サンプルだけ差し替え（padName は変更しない）
    bool replacePadSample(int padIndex, const juce::File& file);
    bool relinkPadSample(int padIndex, const juce::File& file);

    // Smart Trim を現在のサンプルに対して再実行（smartTrimOnSampleLoad の値に関係なく実行）
    // Re-Analyze ボタンから呼ばれる。サンプルがなければ何もしない。
    void reanalyzePad(int padIndex);
    void applyAutoFadeOnTrim(int padIndex);

    // ── Kit Management ────────────────────────────────────────────────────
    struct KitLoadOptions
    {
        bool samples { true };
        bool padNamesAndColours { true };
        bool padParameters { true };
        bool mixerSettings { true };
        bool routing { true };
    };

    void newKit();
    bool applyDefaultKit(const KitLoadOptions& options);
    static juce::File getUserKitsDirectory();
    static bool isDefaultKitName(const juce::String& name);
    static juce::Array<juce::File> getSavedKitFiles();
    bool saveKitToFile(const juce::File& file);
    bool saveKitToCurrentFile();
    bool loadKitFromFile(const juce::File& file);
    bool loadKitFromFile(const juce::File& file, const KitLoadOptions& options);
    bool loadRecentKit(int recentIndex);
    juce::File getCurrentKitFile() const { return currentKitFile; }
    juce::StringArray getRecentKitPaths() const { return recentKitPaths; }

    // ── 試聴（Pad クリックから呼ばれる） ────────────────────────────────
    // UI スレッドから呼ぶ。read lock を取って VoiceManager::noteOn を発火する。
    void auditionPadOn(int padIndex, float velocity = 1.0f);
    void auditionLayerOn(int padIndex, int layerIndex, float velocity = 1.0f);
    void auditionPadOff(int padIndex);

    // パッドのデータへのアクセス
    KitData&       getKit()            { return kit; }
    const KitData& getKit()    const   { return kit; }

    AudioFileManager& getFileManager() { return fileManager; }

    // レベルメーター用（UI スレッドから呼ぶ）
    const VoiceManager& getVoiceManager() const noexcept { return voiceManager; }
    VoiceManager&       getVoiceManager()       noexcept { return voiceManager; }

    // マスター出力ピーク（オーディオスレッドが書き込み、UI スレッドが取り出す）
    // exchange で読み取り & リセットを原子的に行う
    float takeMasterPeakL() noexcept { return masterPeakL.exchange(0.0f, std::memory_order_relaxed); }
    float takeMasterPeakR() noexcept { return masterPeakR.exchange(0.0f, std::memory_order_relaxed); }
    float getAudioProcessLoadPercent() const noexcept;
    bool hasRecentAudioActivity(double holdSeconds = 0.35) const noexcept;
    std::uint64_t getLoadedSampleBytes() const noexcept { return fileManager.getTotalSampleBytes(); }
    void clearPadClipIndicator(int padIndex) noexcept { voiceManager.clearPadClip(padIndex); }
    void clearAllPadClipIndicators() noexcept { voiceManager.clearAllPadClips(); }
    void clearAllSolo();
    void clearAllMute();
    void resetMixerPage(int page);
    int setPadMidiNote(int padIndex, int midiNote);
    void beginMidiLearnForPad(int padIndex) noexcept;
    void cancelMidiLearn() noexcept;
    bool consumeLearnedMidiNote(int& padIndex, int& midiNote) noexcept;
    float consumePadTriggerLevel(int padIndex) noexcept { return voiceManager.consumePadTriggerLevel(padIndex); }
    float consumeLayerTriggerLevel(int padIndex, int layerIndex) noexcept { return voiceManager.consumeLayerTriggerLevel(padIndex, layerIndex); }
    float getLayerLevel(int padIndex, int layerIndex) const noexcept { return voiceManager.getLayerLevel(padIndex, layerIndex); }
    bool getPadPlayheadPosition(int padIndex, float& outPosition) const noexcept
    {
        return voiceManager.getPadPlayheadPosition(padIndex, outPosition);
    }
    bool isKitDirty() const noexcept { return kitDirty; }
    void markKitDirty() noexcept { kitDirty = true; }
    void clearKitDirty() noexcept { kitDirty = false; }

    // ホストのサンプルレート
    double getHostSampleRate() const noexcept { return hostSampleRate; }

    // MIDI マップを再構築（パッドの MIDI ノートを変更した後に呼ぶ）
    void rebuildMidiMap();

    // ── DAW Automation ────────────────────────────────────────────────
    // UI から automatable parameter を変更する場合は PadData 直接書き換えではなく
    // これを通す。ホストへ change gesture / automation を通知し、PadData も同期する。
    void setAutomatablePadParameter(int padIndex,
                                    PadParameterSpecs::Param param,
                                    float value,
                                    bool notifyHost = true);
    void setAutomatableLayerParameter(int padIndex,
                                      int layerIndex,
                                      LayerParameterSpecs::Param param,
                                      float value,
                                      bool notifyHost = true);
    void setPadSampleTrim(int padIndex,
                          float startPosition,
                          float endPosition,
                          float fadeIn,
                          float fadeOut,
                          bool notifyHost = true);
    float getAutomatablePadParameter(int padIndex,
                                     PadParameterSpecs::Param param) const;
    // マスター出力ボリュームを APVTS パラメータ経由で設定 (ホストに gesture / automation
    // を通知し、kit.masterVolume も同期)。UI ノブからはこれを通す。
    void setMasterVolumeParameter(float position, bool notifyHost = true);
    void syncParametersFromKit();
    void syncKitFromParameters();

    // ── User-assignable DAW automation slots ─────────────────────────
    static constexpr int automationSlotCount = 24;
    void beginAutomationLearn(int slotIndex) noexcept;
    void cancelAutomationLearn() noexcept;
    void clearAutomationSlot(int slotIndex);
    juce::String getAutomationSlotTargetID(int slotIndex) const;
    juce::String getAutomationSlotTargetName(int slotIndex) const;
    int getAutomationLearnSlot() const noexcept;
    bool consumeAutomationSlotsChanged() noexcept;

private:
    // Phase 4: バスレイアウトを構築（コンストラクタ初期化子で使う）
    static BusesProperties buildBuses();
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void setKitValueFromParameter(int padIndex, PadParameterSpecs::Param param, float value);
    float getKitValueForParameter(int padIndex, PadParameterSpecs::Param param) const;

    // v7+: Layer 単位 automation
    void setLayerValueFromParameter(int padIndex, int layerIndex,
                                    int param /* cast from LayerParameterSpecs::Param */,
                                    float value);
    float getLayerValueForParameter(int padIndex, int layerIndex,
                                    int param /* cast from LayerParameterSpecs::Param */) const;
    void reloadSamplesFromCurrentKit();
    bool relinkPadSampleOnly(int padIndex, const juce::File& file, bool updateSampleFileName);
    void hydrateRuntimeFromKit(bool restoreFromParametersTree);
    void resetSampleDependentParameters(int padIndex);
    void updateAudioProcessLoad(int numSamples, int64 elapsedTicks, int64 processEndTicks) noexcept;
    void addRecentKitPath(const juce::File& file);
    void loadRecentKitPaths();
    void saveRecentKitPaths() const;
    static juce::File getRecentKitStoreFile();
    void markAudioActivity(int64 ticks = juce::Time::getHighResolutionTicks()) noexcept;
    void registerParameterListeners();
    void removeParameterListeners();
    void parameterChanged(const juce::String& parameterID, float newValue) override;
    static juce::String automationSlotParameterID(int slotIndex);
    static int automationSlotIndexFromParameterID(const juce::String& parameterID);
    int findParameterIndex(const juce::String& parameterID) const;
    int assignedAutomationSlotForTarget(const juce::String& parameterID) const;
    void setAutomationSlotTarget(int slotIndex, const juce::String& parameterID);
    void captureAutomationLearnTarget(const juce::String& parameterID);
    void setParameterValueFromUi(const juce::String& parameterID,
                                 float normalizedValue,
                                 bool notifyHost);

    KitData          kit;
    juce::AudioProcessorValueTreeState parameters;
    AudioFileManager fileManager;
    VoiceManager     voiceManager;

    double hostSampleRate { 44100.0 };

    // MIDI ノート番号 → パッドインデックスのテーブル
    // midiNoteTopad[note] = padIndex (0〜47), または -1（未割り当て）
    std::array<int, 128> midiNoteTopad;

    // ── Pad クリップボード（Copy/Paste 用） ────────────────────────────
    PadData padClipboard {};
    bool    clipboardValid { false };
    bool    kitDirty { false };
    juce::File currentKitFile;
    juce::StringArray recentKitPaths;

    // マスター出力ボリュームの平滑化ゲイン（オーディオスレッド専用。
    // ブロック間で applyGainRamp して zipper noise を防ぐ）
    float masterGainSmoothed { 1.0f };

    // マスター出力ピーク（オーディオスレッドが書き込む atomic float）
    std::atomic<float> masterPeakL { 0.0f };
    std::atomic<float> masterPeakR { 0.0f };
    std::atomic<float> audioProcessLoadPercent { 0.0f };
    std::atomic<int64> lastAudioProcessTicks { 0 };
    std::atomic<int64> lastAudioActivityTicks { 0 };
    std::atomic<int> midiLearnTargetPad { -1 };
    std::atomic<int> learnedMidiPad { -1 };
    std::atomic<int> learnedMidiNote { -1 };
    std::atomic<bool> parametersNeedSync { false };
    std::atomic<bool> suppressParameterCallbacks { false };
    std::array<juce::String, automationSlotCount> automationSlotTargets {};
    std::array<std::atomic<int>, automationSlotCount> automationSlotTargetIndices {};
    std::atomic<int> automationLearnSlot { -1 };
    std::atomic<bool> automationSlotsChanged { false };
    // v7+: DAW automation (=parameterChanged path) で kit を書いた後、UI へ
    // 状態を push する必要があることを示すフラグ。Timer がこれを消費する。
    std::atomic<bool> kitChangedByAutomation { false };

public:
    /** WebView Editor 側の Timer から消費する: DAW automation 起因の kit 変化があるか。 */
    bool consumeKitChangedByAutomation() noexcept
    {
        return kitChangedByAutomation.exchange(false, std::memory_order_acq_rel);
    }

private:

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumSamplerAudioProcessor)
};
