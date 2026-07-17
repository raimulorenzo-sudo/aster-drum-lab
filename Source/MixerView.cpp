#include "MixerView.h"
#include "ColorPalette.h"
#include "RoutingPresets.h"
#include "Typography.h"

namespace
{
bool allPadsMatchOutput(const KitData& kit, int outputAssign)
{
    for (const auto& pad : kit.pads)
        if (pad.outputAssign != outputAssign)
            return false;

    return true;
}

bool individualOutputsMatch(const KitData& kit)
{
    for (int i = 0; i < NUM_PADS; ++i)
        if (kit.pads[static_cast<size_t>(i)].outputAssign != i)
            return false;

    return true;
}
}

// ─────────────────────────────────────────────────────────────────────────────
// コンストラクタ
// ─────────────────────────────────────────────────────────────────────────────
MixerView::MixerView(DrumSamplerAudioProcessor& processor)
    : proc(processor)
{
    // ── ページボタン（色はグローバル LAF に委譲） ─────────────────────
    for (auto* btn : { &pageBtnA, &pageBtnB, &pageBtnC })
    {
        btn->setClickingTogglesState(true);
        addAndMakeVisible(btn);
    }
    pageBtnA.setRadioGroupId(2);
    pageBtnB.setRadioGroupId(2);
    pageBtnC.setRadioGroupId(2);
    pageBtnA.setToggleState(true, juce::dontSendNotification);

    pageBtnA.onClick = [this] { setPage(0); };
    pageBtnB.onClick = [this] { setPage(1); };
    pageBtnC.onClick = [this] { setPage(2); };

    // ── Output Mode セレクタ（色はグローバル LAF） ────────────────────
    // Ice Blue でアクセントしたいときだけ textColourId だけ上書き
    modeBox.setColour(juce::ComboBox::textColourId, ColorPalette::iceBlue());
    modeBox.addItem("Stereo",  static_cast<int>(OutputMode::Stereo) + 1);
    modeBox.addItem("16 Outs", static_cast<int>(OutputMode::Outs16) + 1);
    modeBox.addItem("32 Outs", static_cast<int>(OutputMode::Outs32) + 1);
    modeBox.addItem("48 Outs", static_cast<int>(OutputMode::Outs48) + 1);
    modeBox.setSelectedId(static_cast<int>(proc.getKit().outputMode) + 1,
                          juce::dontSendNotification);
    modeBox.onChange = [this] { onOutputModeChanged(); };
    addAndMakeVisible(modeBox);

    // ── Routing Preset セレクタ（Champagne テキスト以外は LAF） ───────
    presetBox.setColour(juce::ComboBox::textColourId, ColorPalette::champagne());
    presetBox.setTextWhenNothingSelected("Routing…");
    presetBox.addItem("All to Main",          static_cast<int>(RoutingPresets::Preset::AllToMain)          + 1);
    presetBox.addItem("Individual Outs",      static_cast<int>(RoutingPresets::Preset::IndividualOuts)     + 1);
    presetBox.onChange = [this] { onPresetChosen(); };
    addAndMakeVisible(presetBox);

    routingStatusLabel.setText("INDIVIDUAL", juce::dontSendNotification);
    routingStatusLabel.setJustificationType(juce::Justification::centred);
    routingStatusLabel.setColour(juce::Label::textColourId, ColorPalette::iceBlue());
    routingStatusLabel.setColour(juce::Label::backgroundColourId, ColorPalette::panelInset().withAlpha(0.86f));
    routingStatusLabel.setColour(juce::Label::outlineColourId, ColorPalette::iceBlue().withAlpha(0.32f));
    routingStatusLabel.setFont(Typography::buttonText(9.4f));
    addAndMakeVisible(routingStatusLabel);

    saveCustomRoutingButton.setColour(juce::TextButton::buttonColourId, ColorPalette::bgDeep().withAlpha(0.82f));
    saveCustomRoutingButton.setColour(juce::TextButton::textColourOffId, ColorPalette::champagne());
    saveCustomRoutingButton.onClick = [this] { saveCustomRouting(); };
    addAndMakeVisible(saveCustomRoutingButton);

    recallCustomRoutingButton.setColour(juce::TextButton::buttonColourId, ColorPalette::bgDeep().withAlpha(0.82f));
    recallCustomRoutingButton.setColour(juce::TextButton::textColourOffId, ColorPalette::textDim());
    recallCustomRoutingButton.onClick = [this] { recallCustomRouting(); };
    recallCustomRoutingButton.setEnabled(false);
    addAndMakeVisible(recallCustomRoutingButton);

    utilityButton.setColour(juce::TextButton::buttonColourId, ColorPalette::bgDeep().withAlpha(0.82f));
    utilityButton.setColour(juce::TextButton::textColourOffId, ColorPalette::textDim());
    utilityButton.onClick = [this] { showUtilityMenu(); };
    addAndMakeVisible(utilityButton);

    // ── チャンネルストリップ ──────────────────────────────────────────────
    for (int i = 0; i < 16; ++i)
    {
        strips[static_cast<size_t>(i)] = std::make_unique<MixerChannelStrip>(i, processor);
        addAndMakeVisible(strips[static_cast<size_t>(i)].get());
    }

    setPage(0);
    updateRoutingStatus();
}

// ─────────────────────────────────────────────────────────────────────────────
// レベルメーター更新（タイマー 30Hz）
// ─────────────────────────────────────────────────────────────────────────────
void MixerView::refreshLevels()
{
    updateRoutingStatus();

    const int base = currentPage * PADS_PER_PAGE;
    for (int i = 0; i < 16; ++i)
    {
        const float level = proc.getVoiceManager().getPadLevel(base + i);
        const bool clip = proc.getVoiceManager().getPadClipLatched(base + i);
        strips[static_cast<size_t>(i)]->setLevel(level, clip);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 全ストリップを kit と同期
// ─────────────────────────────────────────────────────────────────────────────
void MixerView::refreshAllFromKit()
{
    for (int i = 0; i < 16; ++i)
        strips[static_cast<size_t>(i)]->refreshFromKit();

    // Mode 表示も同期（外部から outputMode が変えられた場合に備える）
    const int desiredModeId = static_cast<int>(proc.getKit().outputMode) + 1;
    if (modeBox.getSelectedId() != desiredModeId)
        modeBox.setSelectedId(desiredModeId, juce::dontSendNotification);

    updateRoutingStatus();
}

// ─────────────────────────────────────────────────────────────────────────────
// 描画
// ─────────────────────────────────────────────────────────────────────────────
void MixerView::paint(juce::Graphics& g)
{
    g.fillAll(ColorPalette::bg());

    // ヘッダー行
    juce::ColourGradient header(ColorPalette::surfaceHigh(), 0.0f, 0.0f,
                                ColorPalette::surface(),     0.0f, 40.0f, false);
    g.setGradientFill(header);
    g.fillRect(0, 0, getWidth(), 40);

    g.setColour(ColorPalette::border());
    g.drawHorizontalLine(40, 0.0f, static_cast<float>(getWidth()));

    // タイトル
    g.setColour(ColorPalette::champagne());
    g.setFont(Typography::title(13.0f));
    g.drawText("MIXER", 14, 0, 76, 40, juce::Justification::centredLeft);

    // ページ表示
    g.setColour(ColorPalette::textDim());
    g.setFont(Typography::label(10.0f));
    g.drawText("Page " + juce::String(char('A' + currentPage)),
               84, 0, 64, 40, juce::Justification::centredLeft);

    // Mode ラベル
    g.setColour(ColorPalette::textDim());
    g.setFont(Typography::label(9.0f));
    g.drawText("Mode", modeBox.getX() - 40, 0, 36, 40,
               juce::Justification::centredRight);
}

// ─────────────────────────────────────────────────────────────────────────────
// レイアウト
// ─────────────────────────────────────────────────────────────────────────────
void MixerView::resized()
{
    const int w = getWidth();
    const int h = getHeight();

    // ── ヘッダー行（32px） ────────────────────────────────────────────────
    const int btnY = 6;
    const int btnH = 28;

    // 右側: ページボタン (A/B/C)
    {
        const int btnW = 34;
        int btnX = w - (btnW * 3 + 12);
        pageBtnA.setBounds(btnX, btnY, btnW, btnH);  btnX += btnW + 2;
        pageBtnB.setBounds(btnX, btnY, btnW, btnH);  btnX += btnW + 2;
        pageBtnC.setBounds(btnX, btnY, btnW, btnH);
    }

    // 中央右: Routing Preset + Custom一時保存
    const int presetW = 176;
    const int presetX = w - (34 * 3 + 12) - presetW - 10;
    presetBox.setBounds(presetX, btnY, presetW, btnH);

    const int recallW = 72;
    const int recallX = presetX - recallW - 8;
    recallCustomRoutingButton.setBounds(recallX, btnY, recallW, btnH);

    const int saveW = 112;
    const int saveX = recallX - saveW - 8;
    saveCustomRoutingButton.setBounds(saveX, btnY, saveW, btnH);

    const int statusW = 92;
    const int statusX = saveX - statusW - 8;
    routingStatusLabel.setBounds(statusX, btnY, statusW, btnH);

    const int utilityW = 88;
    const int utilityX = statusX - utilityW - 8;
    utilityButton.setBounds(utilityX, btnY, utilityW, btnH);

    // 中央左: Mode (84px)
    const int modeW = 96;
    const int modeX = utilityX - modeW - 10;
    modeBox.setBounds(modeX, btnY, modeW, btnH);

    // ── チャンネルストリップ ──────────────────────────────────────────────
    const int stripTop = 40;
    const int stripH   = h - stripTop;
    const int stripW   = w / 16;

    for (int i = 0; i < 16; ++i)
        strips[static_cast<size_t>(i)]->setBounds(i * stripW, stripTop, stripW, stripH);
}

// ─────────────────────────────────────────────────────────────────────────────
// ページ切り替え
// ─────────────────────────────────────────────────────────────────────────────
void MixerView::setPage(int page)
{
    currentPage = juce::jlimit(0, NUM_PAGES - 1, page);
    const int base = currentPage * PADS_PER_PAGE;

    for (int i = 0; i < 16; ++i)
    {
        const auto iu = static_cast<size_t>(i);
        removeChildComponent(strips[iu].get());
        strips[iu] = std::make_unique<MixerChannelStrip>(base + i, proc);
        addAndMakeVisible(strips[iu].get());
    }

    updatePageButtons();
    resized();
    repaint();
}

void MixerView::updatePageButtons()
{
    pageBtnA.setToggleState(currentPage == 0, juce::dontSendNotification);
    pageBtnB.setToggleState(currentPage == 1, juce::dontSendNotification);
    pageBtnC.setToggleState(currentPage == 2, juce::dontSendNotification);
}

// ─────────────────────────────────────────────────────────────────────────────
// Output Mode 変更
// ─────────────────────────────────────────────────────────────────────────────
void MixerView::onOutputModeChanged()
{
    const int id = modeBox.getSelectedId();
    if (id <= 0) return;
    proc.getKit().outputMode = static_cast<OutputMode>(id - 1);

    // すべてのチャンネルストリップの ComboBox を再構築
    refreshAllFromKit();
}

// ─────────────────────────────────────────────────────────────────────────────
// Routing Preset 適用
// ─────────────────────────────────────────────────────────────────────────────
void MixerView::onPresetChosen()
{
    const int id = presetBox.getSelectedId();
    if (id <= 0) return;

    auto preset = static_cast<RoutingPresets::Preset>(id - 1);
    RoutingPresets::apply(preset, proc.getKit());

    // 選択状態をリセット（次回も同じプリセットを選び直せるように）
    presetBox.setSelectedId(0, juce::dontSendNotification);

    // すべてのチャンネルストリップを更新
    refreshAllFromKit();
    updateRoutingStatus();
}

void MixerView::saveCustomRouting()
{
    const auto& kit = proc.getKit();
    for (int i = 0; i < NUM_PADS; ++i)
        customRoutingSnapshot[static_cast<size_t>(i)] = kit.pads[static_cast<size_t>(i)].outputAssign;

    hasCustomRoutingSnapshot = true;
    recallCustomRoutingButton.setEnabled(true);
    recallCustomRoutingButton.setColour(juce::TextButton::textColourOffId, ColorPalette::champagne());
}

void MixerView::recallCustomRouting()
{
    if (! hasCustomRoutingSnapshot)
        return;

    auto& kit = proc.getKit();
    for (int i = 0; i < NUM_PADS; ++i)
        kit.pads[static_cast<size_t>(i)].outputAssign = customRoutingSnapshot[static_cast<size_t>(i)];

    refreshAllFromKit();
}

void MixerView::updateRoutingStatus()
{
    const auto& kit = proc.getKit();
    juce::String status;

    if (allPadsMatchOutput(kit, 0))
        status = "ALL MAIN";
    else if (individualOutputsMatch(kit))
        status = "INDIVIDUAL";
    else
        status = "CUSTOM";

    if (routingStatusLabel.getText() != status)
    {
        routingStatusLabel.setText(status, juce::dontSendNotification);
        const bool isCustom = status == "CUSTOM";
        routingStatusLabel.setColour(juce::Label::textColourId,
                                     isCustom ? ColorPalette::champagne() : ColorPalette::iceBlue());
        routingStatusLabel.setColour(juce::Label::outlineColourId,
                                     (isCustom ? ColorPalette::champagne() : ColorPalette::iceBlue()).withAlpha(0.32f));
    }
}

void MixerView::showUtilityMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader("Mixer Utility");
    menu.addItem(1, "Clear All Solo");
    menu.addItem(2, "Clear All Mute");
    menu.addItem(3, "Clear Clips");
    menu.addSeparator();
    menu.addItem(4, "Reset Mixer Page " + juce::String(char('A' + currentPage)));

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&utilityButton),
                       [this](int result)
    {
        switch (result)
        {
            case 1:
                proc.clearAllSolo();
                break;

            case 2:
                proc.clearAllMute();
                break;

            case 3:
                proc.clearAllPadClipIndicators();
                break;

            case 4:
                proc.resetMixerPage(currentPage);
                break;

            default:
                return;
        }

        refreshAllFromKit();
        repaint();
    });
}
