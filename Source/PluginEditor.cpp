#include "PluginEditor.h"
#include "ColorPalette.h"
#include "Typography.h"
#include "Spacing.h"

// ═════════════════════════════════════════════════════════════════════════════
//  PadButton
// ═════════════════════════════════════════════════════════════════════════════

PadButton::PadButton(int idx, DrumSamplerAudioProcessor& p)
    : padIndex(idx), processor(p)
{
    setRepaintsOnMouseActivity(true);
}

// ── 描画 ─────────────────────────────────────────────────────────────────────
void PadButton::paint(juce::Graphics& g)
{
    const auto& pad  = processor.getKit().pads[static_cast<size_t>(padIndex)];
    const auto  area = getLocalBounds().reduced(1);
    const auto  inner = area.reduced(14, 12);
    const auto  rf = area.toFloat();

    juce::Colour bgColour;
    if (isDragOver)
        bgColour = ColorPalette::padDragOver();
    else if (isSelected)
        bgColour = ColorPalette::padSelected().brighter(0.030f);
    else if (pad.hasSample())
        bgColour = ColorPalette::padHasSample();
    else
        bgColour = ColorPalette::padEmpty();

    if (pad.mute)
        bgColour = bgColour.darker(0.5f);

    if (isMouseOver())
        bgColour = bgColour.brighter(isSelected ? 0.06f : 0.04f);

    if (isPressed)
        bgColour = bgColour.darker(0.10f);

    const auto radius = Spacing::radiusCard;

    g.setColour(ColorPalette::padShadow().withAlpha(0.62f));
    g.fillRoundedRectangle(rf.translated(0.0f, isPressed ? 1.0f : 2.5f), radius);

    if (isSelected)
    {
        for (int i = 3; i >= 1; --i)
        {
            const float expand = static_cast<float>(i) * Spacing::glowSpreadSmall;
            g.setColour(ColorPalette::iceGlow().withAlpha(0.060f / static_cast<float>(i)));
            g.drawRoundedRectangle(rf.expanded(expand), radius + expand, 1.0f);
        }
    }

    if (triggerGlow > 0.01f)
    {
        for (int i = 3; i >= 1; --i)
        {
            const float expand = static_cast<float>(i) * (Spacing::glowSpreadSmall + 0.8f);
            const float alpha = (0.12f + triggerGlow * 0.16f) / static_cast<float>(i);
            g.setColour(ColorPalette::iceGlow().withAlpha(alpha));
            g.drawRoundedRectangle(rf.expanded(expand), radius + expand, 1.0f);
        }
    }

    juce::ColourGradient bg(bgColour.brighter(0.06f), rf.getCentreX(), rf.getY(),
                            bgColour.darker(isSelected ? 0.03f : 0.15f), rf.getCentreX(), rf.getBottom(), false);
    g.setGradientFill(bg);
    g.fillRoundedRectangle(rf, radius);

    g.setColour(ColorPalette::textPrim().withAlpha(isSelected ? 0.08f : 0.045f));
    g.drawHorizontalLine(area.getY() + 1, rf.getX() + 4.0f, rf.getRight() - 4.0f);

    g.setColour(ColorPalette::innerShadow().withAlpha(0.18f));
    g.drawRoundedRectangle(rf.reduced(1.0f), radius - 1.0f, 1.0f);

    if (pad.hasSample())
    {
        auto strip = rf.withTop(rf.getBottom() - 2.0f);
        g.setColour(ColorPalette::iceBlue().withAlpha(isSelected ? 0.28f : 0.09f));
        g.fillRoundedRectangle(strip, 1.0f);
    }

    g.setColour(isSelected ? ColorPalette::iceBlue().withAlpha(0.92f)
                           : (isMouseOver() ? ColorPalette::borderLight().withAlpha(0.70f)
                                            : ColorPalette::borderSoft().withAlpha(0.96f)));
    g.drawRoundedRectangle(rf, radius, isSelected ? 1.05f : 0.75f);

    if (isDragOver)
    {
        g.setColour(ColorPalette::iceBlueLight().withAlpha(0.95f));
        g.drawRoundedRectangle(rf.expanded(1.0f), radius + 1.0f, 1.4f);
    }

    g.setColour(ColorPalette::padNumber());
    g.setFont(Typography::bodyMedium(11.0f));
    g.drawText(juce::String(padIndex + 1),
               inner.withHeight(16),
               juce::Justification::topLeft, false);

    {
        const bool clipped = processor.getVoiceManager().getPadClipLatched(padIndex);
        const bool playing = triggerGlow > 0.08f;

        juce::String statusText;
        juce::Colour statusColour;

        if (clipped)
        {
            statusText = "CLIP";
            statusColour = ColorPalette::danger();
        }
        else if (pad.sampleMissing)
        {
            statusText = "MISS";
            statusColour = ColorPalette::danger().withMultipliedAlpha(0.92f);
        }
        else if (pad.solo)
        {
            statusText = "SOLO";
            statusColour = ColorPalette::iceBlueLight();
        }
        else if (pad.mute)
        {
            statusText = "MUTE";
            statusColour = ColorPalette::textMuted().brighter(0.18f);
        }
        else if (playing)
        {
            statusText = "PLAY";
            statusColour = ColorPalette::iceBlue();
        }
        else if (pad.hasSample())
        {
            statusText = "LOAD";
            statusColour = ColorPalette::textMuted().brighter(0.08f);
        }
        else
        {
            statusText = "EMPTY";
            statusColour = ColorPalette::textMuted().withAlpha(0.56f);
        }

        auto badge = inner.withWidth(44).withHeight(15);
        badge.setX(inner.getRight() - badge.getWidth());
        badge.setY(inner.getY());

        g.setColour(statusColour.withAlpha(clipped || playing || pad.solo ? 0.14f : 0.065f));
        g.fillRoundedRectangle(badge.toFloat(), 4.0f);
        g.setColour(statusColour.withAlpha(clipped || playing || pad.solo ? 0.60f : 0.34f));
        g.drawRoundedRectangle(badge.toFloat(), 4.0f, 0.7f);
        g.setColour(statusColour.withAlpha(0.90f));
        g.setFont(Typography::sampleName(7.2f));
        g.drawText(statusText, badge, juce::Justification::centred, false);
    }

    g.setColour(isSelected ? ColorPalette::textMain().brighter(0.08f) : ColorPalette::textMain());
    g.setFont(Typography::padName(14.2f));
    g.drawText(pad.padName,
               inner.withTrimmedTop(34).withHeight(23),
               juce::Justification::centredLeft, true);

    const juce::String noteName = getMidiNoteName(pad.midiNote);
    g.setColour(isSelected ? ColorPalette::iceBlueLight().withAlpha(0.86f)
                           : ColorPalette::textSub().withAlpha(0.78f));
    g.setFont(Typography::midiNote(10.0f));
    g.drawText(noteName,
               inner.withTrimmedTop(62).withHeight(17),
               juce::Justification::centredLeft, false);

    if (pad.hasSample())
    {
        g.setColour(ColorPalette::sampleText());
        g.setFont(Typography::sampleName(9.0f));
        juce::String fname = pad.sampleFileName;
        if (fname.length() > 24)
            fname = fname.substring(0, 21) + "...";
        g.drawText(fname,
                   inner.withTrimmedTop(84).withHeight(16),
                   juce::Justification::centredLeft, false);
    }
    else if (pad.sampleMissing)
    {
        g.setColour(ColorPalette::danger().brighter(0.10f));
        g.setFont(Typography::sampleName(9.0f));
        g.drawText("MISSING",
                   inner.withTrimmedTop(84).withHeight(16),
                   juce::Justification::centredLeft, false);
    }
    else
    {
        g.setColour(ColorPalette::textMuted().withAlpha(0.38f));
        g.setFont(Typography::sampleName(9.0f));
        g.drawText(isDragOver ? "Drop Sample" : "drop sample",
                   inner.withTrimmedTop(84).withHeight(16),
                   juce::Justification::centredLeft, false);
    }

    if (dropSuccessTicks > 0)
    {
        const float alpha = juce::jmap((float) dropSuccessTicks, 0.0f, 10.0f, 0.0f, 0.22f);
        g.setColour(ColorPalette::iceBlueLight().withAlpha(alpha));
        g.fillRoundedRectangle(rf.reduced(2.0f), radius - 2.0f);
    }
}

// ── クリック処理（左 = 選択 + 試聴、右 / Ctrl+クリック = コンテキストメニュー）
void PadButton::mouseDown(const juce::MouseEvent& e)
{
    // 右クリック or Ctrl+クリック → コンテキストメニュー
    if (e.mods.isPopupMenu())
    {
        showContextMenu();
        return;
    }

    isPressed = true;
    repaint();

    // 通常クリック → このパッドを選択 + サンプル試聴
    if (auto* editor = findParentComponentOfClass<DrumSamplerAudioProcessorEditor>())
        editor->selectPad(padIndex);

    // ベロシティ: クリック位置の Y で軽く変動（上 = 強、下 = 弱）。
    // シンプルに全クリック velocity = 0.9 でも OK だが、表現できると気持ち良い。
    const float yNorm = juce::jlimit(0.0f, 1.0f,
                                     1.0f - static_cast<float>(e.position.y) / juce::jmax(1.0f, (float)getHeight()));
    const float vel   = juce::jmap(yNorm, 0.0f, 1.0f, 0.55f, 1.0f);

    if (processor.getKit().previewOnPadClick)
        processor.auditionPadOn(padIndex, vel);
}

void PadButton::mouseUp(const juce::MouseEvent& e)
{
    // 右クリックは試聴していないのでスキップ
    if (e.mods.isPopupMenu()) return;

    isPressed = false;
    repaint();

    // Gate モードならフェードアウトを開始する（OneShot は無視される）
    if (processor.getKit().previewOnPadClick)
        processor.auditionPadOff(padIndex);
}

// ═════════════════════════════════════════════════════════════════════════════
//  PageSelectorComponent
// ═════════════════════════════════════════════════════════════════════════════

PageSelectorComponent::PageButton::PageButton(const juce::String& text)
    : juce::Button("Page " + text), label(text)
{
    setRepaintsOnMouseActivity(true);
    setWantsKeyboardFocus(true);
}

void PageSelectorComponent::PageButton::paintButton(juce::Graphics& g,
                                                    bool shouldDrawButtonAsHighlighted,
                                                    bool shouldDrawButtonAsDown)
{
    DrumSamplerLookAndFeel::drawPageSelectorButton(g,
                                                   getLocalBounds().toFloat(),
                                                   label,
                                                   getToggleState(),
                                                   shouldDrawButtonAsHighlighted,
                                                   shouldDrawButtonAsDown,
                                                   isEnabled());
}

PageSelectorComponent::PageSelectorComponent()
{
    for (auto* button : { &pageA, &pageB, &pageC })
        addAndMakeVisible(button);

    pageA.onClick = [this] { selectPage(0, juce::sendNotification); };
    pageB.onClick = [this] { selectPage(1, juce::sendNotification); };
    pageC.onClick = [this] { selectPage(2, juce::sendNotification); };

    setCurrentPage(0);
}

void PageSelectorComponent::setCurrentPage(int page)
{
    selectPage(page, juce::dontSendNotification);
}

void PageSelectorComponent::selectPage(int page, juce::NotificationType notification)
{
    const int clampedPage = juce::jlimit(0, NUM_PAGES - 1, page);
    const bool changed = (currentPage != clampedPage);

    currentPage = clampedPage;
    updateButtonStates();

    if (changed && notification == juce::sendNotification && onPageSelected != nullptr)
        onPageSelected(currentPage);
}

void PageSelectorComponent::updateButtonStates()
{
    pageA.setToggleState(currentPage == 0, juce::dontSendNotification);
    pageB.setToggleState(currentPage == 1, juce::dontSendNotification);
    pageC.setToggleState(currentPage == 2, juce::dontSendNotification);
}

void PageSelectorComponent::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    DrumSamplerLookAndFeel::drawPageSelectorPanel(g, bounds);

    constexpr float referenceW = 620.0f;
    constexpr float referenceH = 132.0f;
    constexpr float panelPaddingLeft = 28.0f;
    constexpr float pageLabelW = 84.0f;
    constexpr float pageLabelSize = 18.0f;

    const float scale = juce::jmin(bounds.getWidth() / referenceW, bounds.getHeight() / referenceH);
    const float contentX = (bounds.getWidth() - referenceW * scale) * 0.5f;
    const float labelY = bounds.getCentreY() - 15.0f * scale;
    const auto labelBounds = juce::Rectangle<float>(contentX + panelPaddingLeft * scale,
                                                    labelY,
                                                    pageLabelW * scale,
                                                    28.0f * scale).toNearestInt();

    g.setColour(juce::Colour(0xffe4e8ee).withAlpha(0.80f * 0.92f));
    g.setFont(Typography::semiBold(juce::jmax(12.0f, pageLabelSize * scale)));
    g.drawText("PAGE", labelBounds, juce::Justification::centredLeft, false);
}

void PageSelectorComponent::resized()
{
    constexpr float referenceW = 620.0f;
    constexpr float referenceH = 132.0f;
    constexpr float panelPaddingLeft = 28.0f;
    constexpr float labelToButtons = 110.0f;
    constexpr float buttonSizeRef = 94.0f;
    constexpr float gapRef = 36.0f;

    const auto bounds = getLocalBounds().toFloat();
    const float scale = juce::jmin(bounds.getWidth() / referenceW, bounds.getHeight() / referenceH);
    const float contentX = (bounds.getWidth() - referenceW * scale) * 0.5f;
    const int buttonSize = juce::roundToInt(buttonSizeRef * scale);
    const int gap = juce::roundToInt(gapRef * scale);
    int x = juce::roundToInt(contentX + (panelPaddingLeft + labelToButtons) * scale);
    const int y = juce::roundToInt(bounds.getCentreY() - static_cast<float>(buttonSize) * 0.5f);

    pageA.setBounds(x, y, buttonSize, buttonSize); x += buttonSize + gap;
    pageB.setBounds(x, y, buttonSize, buttonSize); x += buttonSize + gap;
    pageC.setBounds(x, y, buttonSize, buttonSize);
}

void PageSelectorComponent::enablementChanged()
{
    const bool shouldEnableButtons = isEnabled();
    pageA.setEnabled(shouldEnableButtons);
    pageB.setEnabled(shouldEnableButtons);
    pageC.setEnabled(shouldEnableButtons);
    repaint();
}

// ─────────────────────────────────────────────────────────────────────────────
// コンテキストメニュー
//   Copy / Paste / Clear / Reset / Swap ▶ / Replace Sample
//
// メニュー ID:
//    1 Copy Pad
//    2 Paste Pad
//    3 Clear Pad
//    4 Reset Pad Settings
//    5 Replace Sample…
//    6 Relink Sample…
//    1001〜1048  Swap with Pad N   (1000 + 1〜48)
// ─────────────────────────────────────────────────────────────────────────────
void PadButton::showContextMenu()
{
    // クリックされた瞬間にもパッドを選択しておくと、メニュー操作後に
    // PadDetailEditor の表示が一致してわかりやすい
    if (auto* editor = findParentComponentOfClass<DrumSamplerAudioProcessorEditor>())
        editor->selectPad(padIndex);

    const auto& pad = processor.getKit().pads[static_cast<size_t>(padIndex)];

    juce::PopupMenu menu;

    // ── ヘッダー（操作対象のパッド情報） ─────────────────────────────────
    menu.addSectionHeader("Pad " + juce::String(padIndex + 1) + "  —  " + pad.padName);

    // Copy
    menu.addItem(1, "Copy Pad");

    // Paste（クリップボードがない場合は無効化）
    menu.addItem(2, "Paste Pad", processor.hasPadClipboard());

    menu.addSeparator();

    // Replace Sample
    menu.addItem(5, "Replace Sample…");
    menu.addItem(6, "Relink Sample…", pad.hasSampleReference());

    // Clear / Reset
    menu.addItem(3, "Clear Pad",            true);  // サンプル + 設定をリセット
    menu.addItem(4, "Reset Pad Settings",   true);  // サンプルは残し設定だけリセット

    menu.addSeparator();

    // ── Swap with ▶ ───────────────────────────────────────────────────────
    // 48 パッドは多いので Page A/B/C の階層メニューにする
    juce::PopupMenu swapRoot;
    const auto& kit = processor.getKit();

    for (int page = 0; page < NUM_PAGES; ++page)
    {
        juce::PopupMenu pageMenu;
        const int firstIdx = KitData::firstPadOnPage(page);

        for (int i = 0; i < PADS_PER_PAGE; ++i)
        {
            const int targetIdx = firstIdx + i;
            const juce::String label = "Pad " + juce::String(targetIdx + 1)
                                     + "  ("  + kit.pads[static_cast<size_t>(targetIdx)].padName + ")";
            const int menuId = 1001 + targetIdx;   // 1001..1048

            // 自分自身は無効化
            pageMenu.addItem(menuId, label, targetIdx != padIndex);
        }

        swapRoot.addSubMenu("Page " + KitData::pageLabel(page), pageMenu);
    }

    menu.addSubMenu("Swap with", swapRoot);

    // ── 表示 ──────────────────────────────────────────────────────────────
    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(this),
        [safeThis = juce::Component::SafePointer<PadButton>(this)](int result)
        {
            if (safeThis == nullptr || result == 0) return;

            auto& proc      = safeThis->processor;
            const int padIx = safeThis->padIndex;

            switch (result)
            {
                case 1:  // Copy
                    proc.copyPad(padIx);
                    break;

                case 2:  // Paste
                    proc.pastePad(padIx);
                    break;

                case 3:  // Clear
                    proc.clearPadFull(padIx);
                    break;

                case 4:  // Reset Settings
                    proc.resetPadSettings(padIx);
                    break;

                case 5:  // Replace Sample
                {
                    auto chooser = std::make_shared<juce::FileChooser>(
                        "Replace sample for Pad " + juce::String(padIx + 1),
                        juce::File{},
                        "*.wav;*.aiff;*.aif");

                    auto flags = juce::FileBrowserComponent::openMode
                               | juce::FileBrowserComponent::canSelectFiles;

                    chooser->launchAsync(flags,
                        [chooser, &proc, padIx, safeThis](const juce::FileChooser& fc)
                        {
                            const auto file = fc.getResult();
                            if (file == juce::File{}) return;

                            proc.replacePadSample(padIx, file);

                            if (safeThis != nullptr)
                            {
                                if (auto* ed = safeThis->findParentComponentOfClass<DrumSamplerAudioProcessorEditor>())
                                {
                                    ed->refreshAllPads();
                                    ed->selectPad(padIx);  // PadDetailEditor を再ロード
                                }
                            }
                        });
                    return;  // refresh はコールバック内で行う
                }

                case 6:  // Relink Sample
                {
                    const auto& relinkPad = proc.getKit().pads[static_cast<size_t>(padIx)];
                    juce::File initialFile;
                    if (relinkPad.sampleFilePath.isNotEmpty())
                        initialFile = juce::File(relinkPad.sampleFilePath);

                    juce::File initialLocation = initialFile;
                    if (! initialLocation.exists())
                        initialLocation = initialFile.getParentDirectory();
                    if (! initialLocation.exists())
                        initialLocation = juce::File::getSpecialLocation(juce::File::userHomeDirectory);

                    auto chooser = std::make_shared<juce::FileChooser>(
                        "Relink sample for Pad " + juce::String(padIx + 1),
                        initialLocation,
                        "*.wav;*.aiff;*.aif");

                    auto flags = juce::FileBrowserComponent::openMode
                               | juce::FileBrowserComponent::canSelectFiles;

                    chooser->launchAsync(flags,
                        [chooser, &proc, padIx, safeThis](const juce::FileChooser& fc)
                        {
                            const auto file = fc.getResult();
                            if (file == juce::File{}) return;

                            proc.relinkPadSample(padIx, file);

                            if (safeThis != nullptr)
                            {
                                if (auto* ed = safeThis->findParentComponentOfClass<DrumSamplerAudioProcessorEditor>())
                                {
                                    ed->refreshAllPads();
                                    ed->selectPad(padIx);
                                }
                            }
                        });
                    return;  // refresh はコールバック内で行う
                }

                default:  // Swap with Pad N
                    if (result >= 1001 && result <= 1000 + NUM_PADS)
                    {
                        const int otherIdx = result - 1001;
                        proc.swapPads(padIx, otherIdx);
                    }
                    break;
            }

            // UI 更新（共通）: Replace Sample 以外はここを通る
            if (auto* ed = safeThis->findParentComponentOfClass<DrumSamplerAudioProcessorEditor>())
            {
                ed->refreshAllPads();
                ed->selectPad(padIx);  // 詳細エディタを最新値で再ロード
            }
        });
}

// ── ドラッグ&ドロップ ─────────────────────────────────────────────────────────
bool PadButton::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& f : files)
    {
        const auto ext = juce::File(f).getFileExtension().toLowerCase();
        if (ext == ".wav" || ext == ".aiff" || ext == ".aif") return true;
    }
    return false;
}

void PadButton::fileDragEnter(const juce::StringArray& /*files*/, int, int)
{
    isDragOver = true;
    repaint();
}

void PadButton::fileDragExit(const juce::StringArray& /*files*/)
{
    isDragOver = false;
    repaint();
}

void PadButton::filesDropped(const juce::StringArray& files, int, int)
{
    isDragOver = false;
    if (files.isEmpty()) return;

    const juce::File f(files[0]);
    const auto ext = f.getFileExtension().toLowerCase();
    if (ext != ".wav" && ext != ".aiff" && ext != ".aif") return;

    // サンプルを読み込む（padName は変えない）
    if (processor.loadSampleForPad(padIndex, f))
    {
        dropSuccessTicks = 10;
        refresh();
        if (auto* editor = findParentComponentOfClass<DrumSamplerAudioProcessorEditor>())
            editor->selectPad(padIndex);  // 右パネルも更新
    }
    repaint();
}

void PadButton::refresh()
{
    const float impulse = processor.consumePadTriggerLevel(padIndex);
    triggerGlow = juce::jmax(triggerGlow * 0.82f, impulse);
    if (dropSuccessTicks > 0)
        --dropSuccessTicks;

    repaint();
}


// ═════════════════════════════════════════════════════════════════════════════
//  DrumSamplerAudioProcessorEditor
// ═════════════════════════════════════════════════════════════════════════════

DrumSamplerAudioProcessorEditor::DrumSamplerAudioProcessorEditor(
    DrumSamplerAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p), detailEditor(p), mixerView(p)
{
    // グローバル LookAndFeel を適用（全子コンポーネントに自動伝播）
    setLookAndFeel(&laf);

    // 注: setSize() は全ての子コンポーネントを作り終えた最後で呼ぶ。
    // setSize() は内部で resized() を即座にトリガーするため、
    // padButtons[i] などがまだ null だとクラッシュする。

    // ── タブボタン（PADS / MIXER）─ 色はグローバル LAF ──────────────────
    for (auto* btn : { &tabPads, &tabMixer })
    {
        btn->setClickingTogglesState(true);
        addAndMakeVisible(btn);
    }
    tabPads.setRadioGroupId(10);
    tabMixer.setRadioGroupId(10);
    tabPads.setToggleState(true, juce::dontSendNotification);

    tabPads .onClick = [this] { switchTab(ActiveTab::Pads);  };
    tabMixer.onClick = [this] { switchTab(ActiveTab::Mixer); };

    // ── ページ切り替え（A / B / C）─ Pads タブ専用 ───────────────────────
    pageSelector.onPageSelected = [this] (int page) { setPage(page); };
    addAndMakeVisible(pageSelector);

    // ── 16 個のパッドボタン ───────────────────────────────────────────────
    for (int i = 0; i < 16; ++i)
    {
        const auto iu = static_cast<size_t>(i);
        padButtons[iu] = std::make_unique<PadButton>(i, audioProcessor);
        addAndMakeVisible(padButtons[iu].get());
    }

    // ── 詳細エディタ ─────────────────────────────────────────────────────
    addAndMakeVisible(detailEditor);

    // ── ミキサービュー ────────────────────────────────────────────────────
    addAndMakeVisible(mixerView);

    // ── 初期状態 ─────────────────────────────────────────────────────────
    setPage(0);
    selectPad(0);
    switchTab(ActiveTab::Pads);  // 初期は Pads タブ（visibility を確定させる）

    // ── ウィンドウサイズ（最後に呼ぶ。resized() を安全にトリガー） ─────
    setSize(1600, 860);

    // ── 30Hz タイマー開始 ─────────────────────────────────────────────────
    startTimerHz(30);
}

DrumSamplerAudioProcessorEditor::~DrumSamplerAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);  // laf 破棄前に解除
}

juce::Rectangle<int> DrumSamplerAudioProcessorEditor::getKitBoxBounds() const
{
    const int headerControlY = 28;
    const int headerControlH = 40;
    const int kitW = 300;
    const int kitX = juce::jmax(430, getWidth() / 2 - kitW / 2 - 20);
    return { kitX, headerControlY, kitW, headerControlH };
}

juce::Rectangle<int> DrumSamplerAudioProcessorEditor::getPrevKitBounds() const
{
    return getKitBoxBounds().translated(getKitBoxBounds().getWidth() + 18, 0).withWidth(44);
}

juce::Rectangle<int> DrumSamplerAudioProcessorEditor::getNextKitBounds() const
{
    return getPrevKitBounds().translated(54, 0);
}

juce::Rectangle<int> DrumSamplerAudioProcessorEditor::getLoadButtonBounds() const
{
    const int headerControlY = 28;
    const int headerControlH = 40;
    const int menuX = getWidth() - 58;
    return { menuX - 110, headerControlY, 84, headerControlH };
}

juce::Rectangle<int> DrumSamplerAudioProcessorEditor::getSaveButtonBounds() const
{
    return getLoadButtonBounds().translated(-102, 0);
}

juce::Rectangle<int> DrumSamplerAudioProcessorEditor::getMenuButtonBounds() const
{
    const int headerControlY = 28;
    const int menuX = getWidth() - 58;
    return { menuX - 6, headerControlY + 2, 34, 30 };
}

void DrumSamplerAudioProcessorEditor::runSaveAs()
{
    auto chooser = std::make_shared<juce::FileChooser>(
        "Save kit as",
        audioProcessor.getCurrentKitFile() == juce::File{}
            ? DrumSamplerAudioProcessor::getUserKitsDirectory().getChildFile("My Kit.asterkit")
            : audioProcessor.getCurrentKitFile(),
        "*.asterkit");

    auto flags = juce::FileBrowserComponent::saveMode
               | juce::FileBrowserComponent::canSelectFiles
               | juce::FileBrowserComponent::warnAboutOverwriting;

    chooser->launchAsync(flags, [safe = juce::Component::SafePointer<DrumSamplerAudioProcessorEditor>(this), chooser]
    (const juce::FileChooser& fc)
    {
        if (safe == nullptr) return;
        const auto file = fc.getResult();
        if (file == juce::File{}) return;
        safe->audioProcessor.saveKitToFile(file);
        safe->repaint();
    });
}

void DrumSamplerAudioProcessorEditor::runLoadKit()
{
    auto chooser = std::make_shared<juce::FileChooser>(
        "Load kit",
        audioProcessor.getCurrentKitFile() == juce::File{}
            ? DrumSamplerAudioProcessor::getUserKitsDirectory()
            : audioProcessor.getCurrentKitFile().getParentDirectory(),
        "*.asterkit;*.drumkit");

    auto flags = juce::FileBrowserComponent::openMode
               | juce::FileBrowserComponent::canSelectFiles;

    chooser->launchAsync(flags, [safe = juce::Component::SafePointer<DrumSamplerAudioProcessorEditor>(this), chooser]
    (const juce::FileChooser& fc)
    {
        if (safe == nullptr) return;
        const auto file = fc.getResult();
        if (file == juce::File{}) return;
        if (safe->audioProcessor.loadKitFromFile(file))
        {
            safe->setPage(0);
            safe->selectPad(0);
            safe->refreshAllPads();
            safe->mixerView.refreshAllFromKit();
            safe->repaint();
        }
    });
}

void DrumSamplerAudioProcessorEditor::showSaveMenu()
{
    juce::PopupMenu menu;
    menu.addItem(1, "Save Kit",
                 audioProcessor.getCurrentKitFile() != juce::File{}
                 && ! DrumSamplerAudioProcessor::isDefaultKitName(audioProcessor.getKit().kitName));
    menu.addItem(2, "Save Kit As...");

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(getSaveButtonBounds()),
                       [safe = juce::Component::SafePointer<DrumSamplerAudioProcessorEditor>(this)](int result)
    {
        if (safe == nullptr || result == 0) return;
        if (result == 1)
            safe->audioProcessor.saveKitToCurrentFile();
        else if (result == 2)
            safe->runSaveAs();
        safe->repaint();
    });
}

void DrumSamplerAudioProcessorEditor::showKitMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader(audioProcessor.getKit().kitName);
    menu.addItem(1, "New Kit");

    juce::PopupMenu recentMenu;
    const auto recentPaths = audioProcessor.getRecentKitPaths();
    for (int i = 0; i < recentPaths.size(); ++i)
    {
        const auto file = juce::File(recentPaths[i]);
        recentMenu.addItem(100 + i,
                           file.getFileNameWithoutExtension().isNotEmpty()
                               ? file.getFileNameWithoutExtension()
                               : recentPaths[i]);
    }
    if (recentPaths.isEmpty())
        recentMenu.addItem(999, "(no recent kits)", false);

    menu.addSubMenu("Recent Kits", recentMenu);

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(getKitBoxBounds()),
                       [safe = juce::Component::SafePointer<DrumSamplerAudioProcessorEditor>(this)](int result)
    {
        if (safe == nullptr || result == 0) return;

        if (result == 1)
        {
            safe->audioProcessor.newKit();
            safe->setPage(0);
            safe->selectPad(0);
        }
        else if (result >= 100)
        {
            if (safe->audioProcessor.loadRecentKit(result - 100))
            {
                safe->setPage(0);
                safe->selectPad(0);
            }
        }

        safe->refreshAllPads();
        safe->mixerView.refreshAllFromKit();
        safe->repaint();
    });
}

void DrumSamplerAudioProcessorEditor::showSettingsMenu()
{
    const auto& kit = audioProcessor.getKit();
    juce::PopupMenu menu;
    menu.addItem(1, "Smart Trim on Sample Load", true, kit.smartTrimOnSampleLoad);
    menu.addItem(2, "Auto Fade on Trim", true, kit.autoFadeOnTrim);
    menu.addItem(3, "Preview on Pad Click", true, kit.previewOnPadClick);
    menu.addItem(5, "Output Name follows Pad Name", true, kit.outputNameFollowsPadName);

    juce::PopupMenu outputModeMenu;
    outputModeMenu.addItem(20, "Stereo",  true, kit.defaultOutputMode == OutputMode::Stereo);
    outputModeMenu.addItem(21, "16 Outs", true, kit.defaultOutputMode == OutputMode::Outs16);
    outputModeMenu.addItem(22, "32 Outs", true, kit.defaultOutputMode == OutputMode::Outs32);
    outputModeMenu.addItem(23, "48 Outs", true, kit.defaultOutputMode == OutputMode::Outs48);
    menu.addSubMenu("Default Output Mode", outputModeMenu);

    juce::PopupMenu fadeMenu;
    fadeMenu.addItem(30, "0 ms",  true, std::abs(kit.defaultFadeOutMs - 0.0f) < 0.01f);
    fadeMenu.addItem(31, "3 ms",  true, std::abs(kit.defaultFadeOutMs - 3.0f) < 0.01f);
    fadeMenu.addItem(32, "5 ms",  true, std::abs(kit.defaultFadeOutMs - 5.0f) < 0.01f);
    fadeMenu.addItem(33, "10 ms", true, std::abs(kit.defaultFadeOutMs - 10.0f) < 0.01f);
    fadeMenu.addItem(34, "20 ms", true, std::abs(kit.defaultFadeOutMs - 20.0f) < 0.01f);
    menu.addSubMenu("Default Fade Out", fadeMenu);

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(getMenuButtonBounds()),
                       [safe = juce::Component::SafePointer<DrumSamplerAudioProcessorEditor>(this)](int result)
    {
        if (safe == nullptr || result == 0) return;

        auto& nextKit = safe->audioProcessor.getKit();
        switch (result)
        {
            case 1: nextKit.smartTrimOnSampleLoad = ! nextKit.smartTrimOnSampleLoad; break;
            case 2: nextKit.autoFadeOnTrim = ! nextKit.autoFadeOnTrim; break;
            case 3: nextKit.previewOnPadClick = ! nextKit.previewOnPadClick; break;
            case 5: nextKit.outputNameFollowsPadName = ! nextKit.outputNameFollowsPadName; break;
            case 20: nextKit.defaultOutputMode = OutputMode::Stereo; break;
            case 21: nextKit.defaultOutputMode = OutputMode::Outs16; break;
            case 22: nextKit.defaultOutputMode = OutputMode::Outs32; break;
            case 23: nextKit.defaultOutputMode = OutputMode::Outs48; break;
            case 30: nextKit.defaultFadeOutMs = 0.0f; break;
            case 31: nextKit.defaultFadeOutMs = 3.0f; break;
            case 32: nextKit.defaultFadeOutMs = 5.0f; break;
            case 33: nextKit.defaultFadeOutMs = 10.0f; break;
            case 34: nextKit.defaultFadeOutMs = 20.0f; break;
            default: break;
        }

        safe->audioProcessor.markKitDirty();
        safe->detailEditor.refresh();
        safe->mixerView.refreshAllFromKit();
        safe->repaint();
    });
}

void DrumSamplerAudioProcessorEditor::cycleRecentKit(int delta)
{
    const auto recent = audioProcessor.getRecentKitPaths();
    if (recent.isEmpty())
        return;

    int currentIdx = -1;
    const auto currentPath = audioProcessor.getCurrentKitFile().getFullPathName();
    for (int i = 0; i < recent.size(); ++i)
        if (recent[i] == currentPath)
            currentIdx = i;

    const int base = currentIdx >= 0 ? currentIdx : 0;
    const int next = juce::jlimit(0, recent.size() - 1, base + delta);
    if (audioProcessor.loadRecentKit(next))
    {
        setPage(0);
        selectPad(0);
        refreshAllPads();
        mixerView.refreshAllFromKit();
        repaint();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// タブ切り替え
// ─────────────────────────────────────────────────────────────────────────────
void DrumSamplerAudioProcessorEditor::switchTab(ActiveTab tab)
{
    activeTab = tab;

    const bool showPads  = (tab == ActiveTab::Pads);
    const bool showMixer = (tab == ActiveTab::Mixer);

    // Pads タブ専用コンポーネントの表示切り替え
    pageSelector.setVisible(showPads);
    for (int i = 0; i < 16; ++i)
        padButtons[static_cast<size_t>(i)]->setVisible(showPads);
    detailEditor.setVisible(showPads);

    // Mixer タブ専用コンポーネントの表示切り替え
    mixerView.setVisible(showMixer);

    // タブ切り替え時に値を同期
    if (showMixer)
        mixerView.refreshAllFromKit();
    else
        detailEditor.refresh();

    resized();
    repaint();
}

// ─────────────────────────────────────────────────────────────────────────────
// タイマーコールバック（30Hz）
// ─────────────────────────────────────────────────────────────────────────────
void DrumSamplerAudioProcessorEditor::timerCallback()
{
    repaint(0, 0, getWidth(), 104);

    if (activeTab == ActiveTab::Mixer)
    {
        // レベルメーター更新 + ストリップ値の同期
        mixerView.refreshLevels();
        // Pads 側での変更（D&D や詳細エディタ）を Mixer に反映
        mixerView.refreshAllFromKit();
    }
    else
    {
        // Mixer タブで変更された Volume/Pan/Mute/Solo を詳細エディタに同期
        detailEditor.refresh();
        // パッドボタンの表示も更新（ミュート状態の色変化など）
        refreshAllPads();
    }
}

// ── 描画 ─────────────────────────────────────────────────────────────────────
void DrumSamplerAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(ColorPalette::bg());

    constexpr int topBarH = 104;
    constexpr int tabY = 64;

    juce::ColourGradient top(ColorPalette::bgDeep().brighter(0.025f), 0.0f, 0.0f,
                             ColorPalette::bg(), 0.0f, static_cast<float>(topBarH), false);
    g.setGradientFill(top);
    g.fillRect(0, 0, getWidth(), topBarH);

    // Small reference-like logo mark.
    const float logoCx = 42.0f;
    const float logoCy = 36.0f;
    g.setColour(ColorPalette::champagne());
    for (int i = 0; i < 12; ++i)
    {
        const float a = juce::MathConstants<float>::twoPi * static_cast<float>(i) / 12.0f;
        const float r0 = (i % 2 == 0) ? 8.0f : 4.0f;
        const float r1 = 22.0f;
        g.drawLine(logoCx + std::cos(a) * r0,
                   logoCy + std::sin(a) * r0,
                   logoCx + std::cos(a) * r1,
                   logoCy + std::sin(a) * r1,
                   1.0f);
    }

    g.setFont(Typography::display(21.0f));
    g.setColour(ColorPalette::textPrim());
    g.drawText("DRUM", 80, 18, 96, 30, juce::Justification::centredLeft, false);
    g.setColour(ColorPalette::champagne());
    g.drawText("SAMPLER", 174, 18, 170, 30, juce::Justification::centredLeft, false);

    g.setColour(ColorPalette::textDim());
    g.setFont(Typography::label(10.0f));
    g.drawText("48 PADS  /  CLEAN SAMPLE NAMES", 81, 48, 260, 16, juce::Justification::left);

    g.setColour(ColorPalette::borderSoft().withAlpha(0.95f));
    g.drawHorizontalLine(topBarH - 1, 22.0f, static_cast<float>(getWidth() - 22));

    const int headerControlY = 28;
    const int headerControlH = 40;
    const auto kitBounds = getKitBoxBounds();
    const int kitX = kitBounds.getX();
    const int kitW = kitBounds.getWidth();
    const auto kitBox = kitBounds.toFloat();
    juce::ColourGradient kitBg(ColorPalette::surfaceHigh().withAlpha(0.62f), kitBox.getCentreX(), kitBox.getY(),
                               ColorPalette::bgDeep().withAlpha(0.72f),      kitBox.getCentreX(), kitBox.getBottom(),
                               false);
    g.setGradientFill(kitBg);
    g.fillRoundedRectangle(kitBox, 4.0f);
    g.setColour(ColorPalette::border());
    g.drawRoundedRectangle(kitBox, 4.0f, 1.0f);
    g.setColour(ColorPalette::textSec());
    g.setFont(Typography::label(10.0f));
    g.drawText("KIT", kitX - 48, headerControlY, 36, headerControlH, juce::Justification::centredRight);
    g.setColour(ColorPalette::textPrim());
    g.setFont(Typography::ui(12.5f));
    const auto kitLabel = audioProcessor.getKit().kitName + (audioProcessor.isKitDirty() ? " *" : "");
    g.drawText(kitLabel, kitX + 20, headerControlY, kitW - 58, headerControlH,
               juce::Justification::centredLeft);

    for (auto r : { getPrevKitBounds(), getNextKitBounds(), getSaveButtonBounds(), getLoadButtonBounds() })
    {
        auto rf = r.toFloat();
        juce::ColourGradient bg(ColorPalette::surfaceHigh().withAlpha(0.55f), rf.getCentreX(), rf.getY(),
                                ColorPalette::bgDeep().withAlpha(0.62f),      rf.getCentreX(), rf.getBottom(),
                                false);
        g.setGradientFill(bg);
        g.fillRoundedRectangle(rf, 4.0f);
        g.setColour(ColorPalette::border());
        g.drawRoundedRectangle(rf, 4.0f, 1.0f);
    }

    g.setColour(ColorPalette::textPrim());
    g.setFont(Typography::label(10.5f));
    g.drawText("SAVE", getSaveButtonBounds(), juce::Justification::centred);
    g.drawText("LOAD", getLoadButtonBounds(), juce::Justification::centred);

    g.drawText("<", getPrevKitBounds(), juce::Justification::centred);
    g.drawText(">", getNextKitBounds(), juce::Justification::centred);

    g.setColour(ColorPalette::textSub().withAlpha(0.86f));
    const auto menuRect = getMenuButtonBounds();
    const int menuX = menuRect.getX() + 6;
    for (int i = 0; i < 3; ++i)
        g.drawLine(static_cast<float>(menuX), static_cast<float>(headerControlY + 8 + i * 9),
                   static_cast<float>(menuX + 22), static_cast<float>(headerControlY + 8 + i * 9),
                   1.4f);

    const int activeTabX = (activeTab == ActiveTab::Pads) ? tabPads.getX() : tabMixer.getX();
    const int activeTabW = (activeTab == ActiveTab::Pads) ? tabPads.getWidth() : tabMixer.getWidth();
    g.setColour(ColorPalette::iceGlow().withAlpha(0.16f));
    g.fillRoundedRectangle(static_cast<float>(activeTabX) - 1.0f, static_cast<float>(tabY + 30),
                           static_cast<float>(activeTabW) + 2.0f, 5.0f, 2.0f);
    g.setColour(ColorPalette::iceBlue());
    g.fillRoundedRectangle(static_cast<float>(activeTabX), static_cast<float>(tabY + 32),
                           static_cast<float>(activeTabW), 2.0f, 1.0f);

    if (activeTab == ActiveTab::Pads)
    {
        auto content = juce::Rectangle<int>(12, topBarH + 10, getWidth() - 24, getHeight() - topBarH - 22);
        const int leftPanelW = juce::jlimit(420, 575,
                                            juce::roundToInt(static_cast<float>(content.getWidth()) * 0.36f));
        auto leftPanel = content.removeFromLeft(leftPanelW);

        auto leftRf = leftPanel.toFloat();
        DrumSamplerLookAndFeel::drawPanelBackground(g, leftRf, true);

    }
}

void DrumSamplerAudioProcessorEditor::mouseUp(const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        return;

    const auto pos = e.getPosition();
    if (getSaveButtonBounds().contains(pos))
    {
        showSaveMenu();
        return;
    }
    if (getLoadButtonBounds().contains(pos))
    {
        runLoadKit();
        return;
    }
    if (getMenuButtonBounds().contains(pos))
    {
        showSettingsMenu();
        return;
    }
    if (getKitBoxBounds().contains(pos))
    {
        showKitMenu();
        return;
    }
    if (getPrevKitBounds().contains(pos))
    {
        cycleRecentKit(1);
        return;
    }
    if (getNextKitBounds().contains(pos))
    {
        cycleRecentKit(-1);
        return;
    }
}

// ── レイアウト ────────────────────────────────────────────────────────────────
void DrumSamplerAudioProcessorEditor::resized()
{
    const int topBarH = 104;

    {
        const int tabH = 38;
        const int tabYPos = 62;
        int tabX = 24;

        tabPads .setBounds(tabX, tabYPos, 180, tabH);  tabX += 190;
        tabMixer.setBounds(tabX, tabYPos, 170, tabH);
    }

    juce::Rectangle<int> content { 12, topBarH + 10, getWidth() - 24, getHeight() - topBarH - 22 };

    if (activeTab == ActiveTab::Mixer)
    {
        mixerView.setBounds(content);
    }
    else
    {
        const int panelGap = juce::jlimit(8, 14, getWidth() / 140);
        const int leftPanelW = juce::jlimit(420, 575,
                                            juce::roundToInt(static_cast<float>(content.getWidth()) * 0.36f));

        auto leftPanel = content.removeFromLeft(leftPanelW);
        auto rightPanel = content.withTrimmedLeft(panelGap);

        detailEditor.setBounds(rightPanel);

        auto left = leftPanel.reduced(juce::jlimit(10, 18, leftPanelW / 32),
                                      juce::jlimit(10, 17, getHeight() / 50));

        const int pageSelectorH = juce::jlimit(58, 76, left.getHeight() / 7);
        pageSelector.setBounds(left.removeFromTop(pageSelectorH));

        left.removeFromTop(juce::jlimit(8, 13, getHeight() / 84));

        const int gap = juce::jlimit(6, 10, left.getWidth() / 52);
        const int padW = (left.getWidth()  - gap * 3) / 4;
        const int padH = (left.getHeight() - gap * 3) / 4;

        for (int row = 0; row < 4; ++row)
            for (int col = 0; col < 4; ++col)
            {
                const int idx = row * 4 + col;
                padButtons[static_cast<size_t>(idx)]->setBounds(
                    left.getX() + col * (padW + gap),
                    left.getY() + row * (padH + gap),
                    padW,
                    padH);
            }
    }
}

// ── ページ切り替え ────────────────────────────────────────────────────────────
void DrumSamplerAudioProcessorEditor::setPage(int page)
{
    currentPage = juce::jlimit(0, NUM_PAGES - 1, page);
    const int base = currentPage * PADS_PER_PAGE;

    for (int i = 0; i < 16; ++i)
    {
        const auto iu = static_cast<size_t>(i);
        padButtons[iu]->padIndex   = base + i;
        padButtons[iu]->isSelected = (base + i == selectedPadIndex);
        padButtons[iu]->repaint();
    }

    updatePageButtons();
}

void DrumSamplerAudioProcessorEditor::updatePageButtons()
{
    pageSelector.setCurrentPage(currentPage);
}

// ── パッド選択 ────────────────────────────────────────────────────────────────
void DrumSamplerAudioProcessorEditor::selectPad(int padIndex)
{
    selectedPadIndex = padIndex;

    for (int i = 0; i < 16; ++i)
    {
        const auto iu = static_cast<size_t>(i);
        padButtons[iu]->isSelected = (padButtons[iu]->padIndex == padIndex);
        padButtons[iu]->repaint();
    }

    detailEditor.loadPad(padIndex);
}

// ── 全パッド表示更新 ──────────────────────────────────────────────────────────
void DrumSamplerAudioProcessorEditor::refreshAllPads()
{
    for (int i = 0; i < 16; ++i)
        padButtons[static_cast<size_t>(i)]->repaint();
}
