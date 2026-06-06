#include "KitData.h"

// ── デフォルトのパッド名（仕様通り） ─────────────────────────────────────────
static const char* kDefaultPadNames[NUM_PADS] =
{
    // Page A (pads 0〜15)
    "Kick",       "Rim",      "Snare",     "Clap",
    "Closed Hat", "Open Hat", "Snap",      "Shaker",
    "Floor Tom",  "Low Tom",  "Mid Tom",   "Hi Tom",
    "Ride",       "Crash",    "Perc 1",    "FX",

    // Page B (pads 16〜31)
    "808",        "Kick 2",   "Snare 2",   "Clap 2",
    "Hat 2",      "Hat 3",    "Perc 2",    "Stick",
    "Tamb",       "Cabasa",   "Cowbell",   "Bell",
    "Perc 3",     "Perc 4",   "Noise",     "FX 2",

    // Page C (pads 32〜47)
    "Vox 1",      "Vox 2",    "Chop 1",    "Chop 2",
    "Hit 1",      "Hit 2",    "Impact",    "Riser",
    "Down FX",    "Sweep",    "Texture",   "Noise 2",
    "Fill 1",     "Fill 2",   "Extra 1",   "Extra 2"
};

// ── デフォルトの MIDI ノート番号 ──────────────────────────────────────────────
// Logic 表記: MIDI 36 = C1, MIDI 60 = C3 (middle C)
// Pad 1 Kick = C1 から半音ずつ上がるクロマチック配置。
// 例: Pad 1 Kick=C1, Pad 2 Snare=C#1, Pad 3 Clap=D1...
static const int kDefaultMidiNotes[NUM_PADS] =
{
    // Page A: C1 - D#2
    36, 37, 38, 39,
    40, 41, 42, 43,
    44, 45, 46, 47,
    48, 49, 50, 51,

    // Page B: E2 - G3
    52, 53, 54, 55,
    56, 57, 58, 59,
    60, 61, 62, 63,
    64, 65, 66, 67,

    // Page C: G#3 - B4
    68, 69, 70, 71,
    72, 73, 74, 75,
    76, 77, 78, 79,
    80, 81, 82, 83
};

// 16-colour palette repeated across all 3 pages (padIndex % 16).
// Matches DEFAULT_PAD_COLORS in ui-prototype/src/data/padData.ts.
static juce::uint32 kDefaultPadColours[NUM_PADS] =
{
    // Page A  (0-15)
    0xff3f5870, 0xffa8501f, 0xff2f4f35, 0xff8a3f5c,
    0xffb57919, 0xff6b4b63, 0xff3d6f78, 0xff8f3838,
    0xff245c5c, 0xffa65a3e, 0xff342b49, 0xff5c6f2f,
    0xff8a4650, 0xff526a7a, 0xff7a4f1f, 0xff4f5558,
    // Page B  (16-31)
    0xff3f5870, 0xffa8501f, 0xff2f4f35, 0xff8a3f5c,
    0xffb57919, 0xff6b4b63, 0xff3d6f78, 0xff8f3838,
    0xff245c5c, 0xffa65a3e, 0xff342b49, 0xff5c6f2f,
    0xff8a4650, 0xff526a7a, 0xff7a4f1f, 0xff4f5558,
    // Page C  (32-47)
    0xff3f5870, 0xffa8501f, 0xff2f4f35, 0xff8a3f5c,
    0xffb57919, 0xff6b4b63, 0xff3d6f78, 0xff8f3838,
    0xff245c5c, 0xffa65a3e, 0xff342b49, 0xff5c6f2f,
    0xff8a4650, 0xff526a7a, 0xff7a4f1f, 0xff4f5558,
};

// ═════════════════════════════════════════════════════════════════════════════
//  OutputSlot
// ═════════════════════════════════════════════════════════════════════════════
juce::ValueTree OutputSlot::toValueTree(int idx) const
{
    juce::ValueTree vt { "Output" };
    vt.setProperty("index",         idx,            nullptr);
    vt.setProperty("customName",    customName,     nullptr);
    vt.setProperty("followPadIndex",followPadIndex, nullptr);
    return vt;
}

void OutputSlot::fromValueTree(const juce::ValueTree& vt)
{
    customName     = vt.getProperty("customName",     customName);
    followPadIndex = vt.getProperty("followPadIndex", followPadIndex);
}

// ═════════════════════════════════════════════════════════════════════════════
//  KitData
// ═════════════════════════════════════════════════════════════════════════════
juce::uint32 KitData::defaultPadColourForIndex(int padIndex) noexcept
{
    const auto wrapped = juce::jlimit(0, NUM_PADS - 1, padIndex);
    return kDefaultPadColours[wrapped];
}

int KitData::defaultMidiNoteForIndex(int padIndex) noexcept
{
    const auto wrapped = juce::jlimit(0, NUM_PADS - 1, padIndex);
    return kDefaultMidiNotes[wrapped];
}

// ── デフォルト値にリセット ─────────────────────────────────────────────────────
void KitData::resetToDefaults()
{
    for (int i = 0; i < NUM_PADS; ++i)
    {
        const auto idx = static_cast<size_t>(i);
        pads[idx] = PadData{};
        pads[idx].padName      = kDefaultPadNames[i];
        pads[idx].midiNote     = kDefaultMidiNotes[i];
        pads[idx].padColourARGB = kDefaultPadColours[i];

        // New/empty kits start with every pad routed to Main.
        pads[idx].outputAssign = 0;
        if (i == 4 || i == 5 || i == 20 || i == 21)
            pads[idx].chokeGroup = 1;

        pads[idx].sampleFileName.clear();
        pads[idx].sampleFilePath.clear();

        // Layer 0 を flat fields にミラー（VoiceManager は layers[] を読む）
        pads[idx].syncLayer0FromFlat();
    }

    // 出力スロットを初期化（全部「自分のインデックスの Pad に追従」）
    for (int i = 0; i < NUM_OUTPUTS; ++i)
    {
        outputs[static_cast<size_t>(i)] = OutputSlot{};
    }

    kitName    = "Default";
    kitVersion = CURRENT_KIT_VERSION;
    pluginVersion = JucePlugin_VersionString;
    outputMode = OutputMode::Outs48;
    masterVolume = 0.75f;
    smartTrimOnSampleLoad = true;
    autoFadeOnTrim = true;
    previewOnPadClick = true;
    preservePadNameOnSampleLoad = true;
    outputNameFollowsPadName = true;
    defaultOutputMode = OutputMode::Outs48;
    defaultFadeOutMs = 5.0f;
}

// ── 出力表示名 ────────────────────────────────────────────────────────────────
juce::String KitData::getOutputDisplayName(int outIdx) const
{
    if (outIdx < 0 || outIdx >= NUM_OUTPUTS) return "OUT ?";

    const auto& slot = outputs[static_cast<size_t>(outIdx)];
    if (slot.customName.isNotEmpty())
        return slot.customName;

    if (! outputNameFollowsPadName)
        return "OUT " + juce::String(outIdx + 1);

    // followPadIndex が無効なら自分のインデックスをそのまま参照
    int padIdx = (slot.followPadIndex >= 0 && slot.followPadIndex < NUM_PADS)
                   ? slot.followPadIndex
                   : outIdx;

    if (padIdx < 0 || padIdx >= NUM_PADS) padIdx = 0;

    const auto& pad = pads[static_cast<size_t>(padIdx)];
    return pad.padName.toUpperCase() + " OUT";
}

// ── ComboBox 用の短いラベル: "OUT 1 — Kick OUT" のように番号付き ─────────────
juce::String KitData::getOutputShortLabel(int outIdx) const
{
    return "OUT " + juce::String(outIdx + 1) + " — " + getOutputDisplayName(outIdx);
}

// ── ValueTree への書き出し ─────────────────────────────────────────────────────
juce::ValueTree KitData::toValueTree() const
{
    juce::ValueTree tree { "Kit" };
    tree.setProperty("kitName",    kitName,                 nullptr);
    tree.setProperty("version",    CURRENT_KIT_VERSION,     nullptr);
    tree.setProperty("kitVersion", kitVersion,              nullptr);
    tree.setProperty("pluginVersion", pluginVersion,        nullptr);
    tree.setProperty("outputMode", static_cast<int>(outputMode), nullptr);
    tree.setProperty("masterVolume", masterVolume,               nullptr);
    tree.setProperty("smartTrim",  smartTrimOnSampleLoad,        nullptr);
    tree.setProperty("autoFadeOnTrim", autoFadeOnTrim,           nullptr);
    tree.setProperty("previewOnPadClick", previewOnPadClick,     nullptr);
    tree.setProperty("preservePadNameOnSampleLoad", preservePadNameOnSampleLoad, nullptr);
    tree.setProperty("outputNameFollowsPadName", outputNameFollowsPadName, nullptr);
    tree.setProperty("defaultOutputMode", static_cast<int>(defaultOutputMode), nullptr);
    tree.setProperty("defaultFadeOutMs", defaultFadeOutMs,       nullptr);

    // パッド
    for (int i = 0; i < NUM_PADS; ++i)
        tree.addChild(pads[static_cast<size_t>(i)].toValueTree(), -1, nullptr);

    // 出力スロット（"Outputs" 子ノードにまとめる）
    juce::ValueTree outsTree { "Outputs" };
    for (int i = 0; i < NUM_OUTPUTS; ++i)
        outsTree.addChild(outputs[static_cast<size_t>(i)].toValueTree(i), -1, nullptr);
    tree.addChild(outsTree, -1, nullptr);

    return tree;
}

// ── ValueTree から読み込み ─────────────────────────────────────────────────────
void KitData::fromValueTree(const juce::ValueTree& vt)
{
    if (!vt.hasType("Kit")) return;

    const int version = static_cast<int>(vt.getProperty("kitVersion",
                               static_cast<int>(vt.getProperty("version", 1))));

    kitName    = vt.getProperty("kitName", "Default");
    kitVersion = version;
    pluginVersion = vt.getProperty("pluginVersion", JucePlugin_VersionString).toString();
    outputMode = static_cast<OutputMode>(
        static_cast<int>(vt.getProperty("outputMode", static_cast<int>(OutputMode::Outs48))));
    masterVolume = juce::jlimit(0.0f, 1.0f,
        static_cast<float>(static_cast<double>(vt.getProperty("masterVolume", 0.75))));
    smartTrimOnSampleLoad  = vt.getProperty("smartTrim", true);
    autoFadeOnTrim = vt.getProperty("autoFadeOnTrim", true);
    previewOnPadClick = vt.getProperty("previewOnPadClick", true);
    preservePadNameOnSampleLoad = vt.getProperty("preservePadNameOnSampleLoad", true);
    outputNameFollowsPadName = vt.getProperty("outputNameFollowsPadName", true);
    defaultOutputMode = static_cast<OutputMode>(
        static_cast<int>(vt.getProperty("defaultOutputMode", static_cast<int>(outputMode))));
    defaultFadeOutMs = vt.getProperty("defaultFadeOutMs", 5.0f);

    // パッド読み込み（"Pad" 子ノードのみを対象に）
    int padCount = 0;
    for (int i = 0; i < vt.getNumChildren(); ++i)
    {
        const auto child = vt.getChild(i);
        if (child.hasType("Pad") && padCount < NUM_PADS)
        {
            pads[static_cast<size_t>(padCount)].fromValueTree(child);

            // 後方互換: 旧 outputAssign (-1 = main, 0-7 = aux) を新形式 (0-47) に変換
            auto& pd = pads[static_cast<size_t>(padCount)];
            if (pd.outputAssign < 0 || pd.outputAssign >= NUM_OUTPUTS)
                pd.outputAssign = 0;  // Invalid legacy assignments fall back to Main.
            if (pd.padColourARGB == 0)
                pd.padColourARGB = kDefaultPadColours[padCount];

            ++padCount;
        }
    }

    // v3 migration:
    // 旧プロトタイプでは GM ドラムマップ寄りのバラバラな割り当てだった。
    // 以後は Logic 表記で Pad 1 = C1 から半音階で固定する。
    if (version < 3)
    {
        for (int i = 0; i < NUM_PADS; ++i)
            pads[static_cast<size_t>(i)].midiNote = kDefaultMidiNotes[i];
    }

    if (version < 5)
    {
        for (int i = 0; i < NUM_PADS; ++i)
        {
            auto& pad = pads[static_cast<size_t>(i)];
            if (pad.padColourARGB == 0)
                pad.padColourARGB = kDefaultPadColours[i];
            if (i == 3 || i == 4 || i == 19 || i == 20)
                pad.chokeGroup = 1;
        }
    }

    // 出力スロット読み込み
    auto outsTree = vt.getChildWithName("Outputs");
    if (outsTree.isValid())
    {
        for (int i = 0; i < outsTree.getNumChildren() && i < NUM_OUTPUTS; ++i)
            outputs[static_cast<size_t>(i)].fromValueTree(outsTree.getChild(i));
    }
}
