#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

// ─────────────────────────────────────────────────────────────────────────────
// WebViewEditor  ─  HTML/CSS/JS UI を JUCE WebBrowserComponent でホストする
//
// 設計方針:
//   - オーディオ / MIDI / PadData には触らない
//   - C++ ↔ JS は JUCE 8 の WebBrowserComponent::Options::withNativeIntegrationEnabled()
//     を使った双方向 JSON メッセージング
//   - WebUI/*.html, *.css, *.js は CMake で BinaryData にバンドルされ、
//     ResourceProvider 経由で配信される
// ─────────────────────────────────────────────────────────────────────────────
class WebViewEditor : public juce::AudioProcessorEditor,
                      public juce::FileDragAndDropTarget,
                      private juce::Timer
{
public:
    explicit WebViewEditor(DrumSamplerAudioProcessor& processor);
    ~WebViewEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray& files, int x, int y) override;
    void fileDragMove(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

private:
    DrumSamplerAudioProcessor& audioProcessor;

    int currentPage     { 0 };
    int selectedPadIndex{ 0 };
    // 各 Pad で現在 UI が選択している Layer インデックス。
    // JS の selectLayer メッセージで更新する。ネイティブ DnD 時に使用。
    std::array<int, NUM_PADS> selectedLayerIndices {};

    enum class ActiveWebTab
    {
        Pads,
        Mixer,
        Missing
    };

    ActiveWebTab activeWebTab { ActiveWebTab::Pads };

    juce::WebBrowserComponent webView;
    bool initialKitSent { false };
    bool fastMeterTimerActive { false };
    bool metersWereActive { false };
    int fastTimerTick { 0 };
    double lastStatsBroadcastMs { 0.0 };

    struct PendingSampleByteDrop
    {
        juce::String transferId;
        juce::String fileName;
        int padIndex { -1 };
        int layerIndex { 0 };
        int expectedChunks { 0 };
        int receivedChunks { 0 };
        int64 expectedBytes { 0 };
        juce::MemoryBlock data;
        bool active { false };
    };

    PendingSampleByteDrop pendingSampleByteDrop;

    // C++ → JS: 現在の Kit を JSON でブロードキャスト
    void broadcastKitState();
    void broadcastAutomationSlots();
    void broadcastKitList();
    void broadcastPadUpdate(int padIndex);
    juce::var padToWebVar(int padIndex) const;
    juce::var kitToWebVar() const;

    // C++ → JS: 表示中 UI に必要な範囲だけレベルメーターデータ送信
    void broadcastPadTriggers();
    void broadcastLevelData();
    void broadcastSystemStats();
    int padIndexForDropPosition(int x, int y) const;
    bool loadDroppedFileForPad(int padIndex, const juce::File& file, const juce::String& displayFileName = {});
    bool loadDroppedFileForLayer(int padIndex, int layerIndex, const juce::File& file, const juce::String& displayFileName = {});
    bool loadDroppedBytesForPad(int padIndex, const juce::String& fileName, const void* data, size_t size);
    bool loadDroppedBytesForLayer(int padIndex, int layerIndex, const juce::String& fileName, const void* data, size_t size);
    juce::File getDroppedSampleCacheDirectory() const;
    static bool isSupportedAudioFile(const juce::File& file);

    // WebView リソースプロバイダ (バンドルされた HTML/CSS/JS を返す)
    juce::WebBrowserComponent::Resource fetchWebResource(const juce::String& url);

    // JS → C++ メッセージハンドラ
    void handleUiMessage(const juce::var& message);

    // タイマー:
    //   Phase 1: 120ms 遅延で kitData を初回送信
    //   Phase 2: 音が動いている間だけ 15fps、アイドル時は低頻度
    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WebViewEditor)
};
