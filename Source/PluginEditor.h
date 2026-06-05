#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "PadDetailEditor.h"
#include "MixerView.h"
#include "DrumSamplerLookAndFeel.h"

// ─────────────────────────────────────────────────────────────────────────────
// PadButton  ─  4×4 グリッドの 1 マス（Phase 2 から変更なし）
// ─────────────────────────────────────────────────────────────────────────────
class PadButton : public juce::Component,
                  public juce::FileDragAndDropTarget
{
public:
    PadButton(int padIndex, DrumSamplerAudioProcessor& processor);

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseUp  (const juce::MouseEvent& e) override;

    // 右クリック（または Ctrl+クリック）で表示するコンテキストメニュー
    void showContextMenu();

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray& files)              override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    void refresh();

    int  padIndex   { 0 };
    bool isSelected { false };

private:
    DrumSamplerAudioProcessor& processor;
    bool isDragOver { false };
    bool isPressed  { false };
    float triggerGlow { 0.0f };
    int   dropSuccessTicks { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PadButton)
};

class PageSelectorComponent : public juce::Component
{
public:
    PageSelectorComponent();

    std::function<void(int)> onPageSelected;

    void setCurrentPage(int page);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void enablementChanged() override;

private:
    class PageButton : public juce::Button
    {
    public:
        explicit PageButton(const juce::String& text);

        void paintButton(juce::Graphics& g,
                         bool shouldDrawButtonAsHighlighted,
                         bool shouldDrawButtonAsDown) override;

    private:
        juce::String label;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PageButton)
    };

    PageButton pageA { "A" };
    PageButton pageB { "B" };
    PageButton pageC { "C" };
    int currentPage { 0 };

    void selectPage(int page, juce::NotificationType notification);
    void updateButtonStates();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PageSelectorComponent)
};

// ─────────────────────────────────────────────────────────────────────────────
// DrumSamplerAudioProcessorEditor  ─  メイン UI（960×640）
//
// Phase 3 レイアウト:
//   ┌──────────────────────960px──────────────────────┐
//   │  TopBar（44px）: タイトル   [PADS] [MIXER]      │
//   ├─────────────────────────────────────────────────┤
//   │  PADS タブ:                                     │
//   │    左420px: ページボタン + 4×4パッドグリッド      │
//   │    右540px: PadDetailEditor                     │
//   │                                                 │
//   │  MIXER タブ:                                    │
//   │    全幅: MixerView（16ch × A/B/Cページ）         │
//   └─────────────────────────────────────────────────┘
//
// タイマー（30Hz）:
//   - MIXER 表示中: レベルメーター更新 + 値の同期
//   - PADS 表示中: Mixer 側で変更された値を Detail Editor に同期
// ─────────────────────────────────────────────────────────────────────────────
class DrumSamplerAudioProcessorEditor : public juce::AudioProcessorEditor,
                                        private juce::Timer
{
public:
    explicit DrumSamplerAudioProcessorEditor(DrumSamplerAudioProcessor&);
    ~DrumSamplerAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized()              override;
    void mouseUp(const juce::MouseEvent& e) override;

    // 選択パッドを変更（PadButton から呼ばれる）
    void selectPad(int padIndex);

    // 全パッドボタンの表示を更新
    void refreshAllPads();

private:
    DrumSamplerAudioProcessor& audioProcessor;

    // ── グローバル LookAndFeel（全子コンポーネントに伝播） ──────────────
    DrumSamplerLookAndFeel laf;

    // ── タブ ──────────────────────────────────────────────────────────────
    enum class ActiveTab { Pads, Mixer };
    ActiveTab activeTab { ActiveTab::Pads };

    juce::TextButton tabPads  { "PADS"  };
    juce::TextButton tabMixer { "MIXER" };

    void switchTab(ActiveTab tab);

    // ── Pads タブ: ページセレクター + パッドグリッド + 詳細エディタ ─────
    PageSelectorComponent pageSelector;
    int currentPage { 0 };

    std::array<std::unique_ptr<PadButton>, 16> padButtons;
    PadDetailEditor detailEditor;

    int selectedPadIndex { 0 };

    void setPage(int page);
    void updatePageButtons();

    // ── Mixer タブ ────────────────────────────────────────────────────────
    MixerView mixerView;

    // ── タイマー（30Hz: レベルメーター & 値同期） ─────────────────────────
    void timerCallback() override;
    juce::Rectangle<int> getKitBoxBounds() const;
    juce::Rectangle<int> getSaveButtonBounds() const;
    juce::Rectangle<int> getLoadButtonBounds() const;
    juce::Rectangle<int> getMenuButtonBounds() const;
    juce::Rectangle<int> getPrevKitBounds() const;
    juce::Rectangle<int> getNextKitBounds() const;
    void showKitMenu();
    void showSaveMenu();
    void showSettingsMenu();
    void runSaveAs();
    void runLoadKit();
    void cycleRecentKit(int delta);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumSamplerAudioProcessorEditor)
};
