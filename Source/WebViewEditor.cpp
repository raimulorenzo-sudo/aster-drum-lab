#include "WebViewEditor.h"
#include "PadDataJson.h"
#include "LayerParameterSpecs.h"
#include "PadParameterSpecs.h"
#include "SmartTrim.h"
#include "WebUIBinaryData.h"

// ─────────────────────────────────────────────────────────────────────────────
// 静的: BinaryData から MIME と本体を引く
// ─────────────────────────────────────────────────────────────────────────────
namespace
{
    constexpr int baseEditorWidth = 1400;
    constexpr int baseEditorHeight = 852;
    constexpr int minEditorWidth = 700;
    constexpr int minEditorHeight = 426;
    constexpr int maxEditorWidth = 2800;
    constexpr int maxEditorHeight = 1704;

    juce::String mimeForPath(const juce::String& path)
    {
        if (path.endsWithIgnoreCase(".html")) return "text/html";
        if (path.endsWithIgnoreCase(".css"))  return "text/css";
        if (path.endsWithIgnoreCase(".js")
         || path.endsWithIgnoreCase(".mjs"))  return "application/javascript";
        if (path.endsWithIgnoreCase(".svg"))  return "image/svg+xml";
        if (path.endsWithIgnoreCase(".png"))  return "image/png";
        if (path.endsWithIgnoreCase(".jpg")
         || path.endsWithIgnoreCase(".jpeg")) return "image/jpeg";
        if (path.endsWithIgnoreCase(".webp")) return "image/webp";
        if (path.endsWithIgnoreCase(".woff")) return "font/woff";
        if (path.endsWithIgnoreCase(".woff2"))return "font/woff2";
        if (path.endsWithIgnoreCase(".ttf"))  return "font/ttf";
        if (path.endsWithIgnoreCase(".otf"))  return "font/otf";
        if (path.endsWithIgnoreCase(".json")) return "application/json";
        if (path.endsWithIgnoreCase(".map"))  return "application/json";
        return "application/octet-stream";
    }

    /** Original filename → BinaryData resource name 解決。
     *  JUCE は filename を symbol 化する際に '-' を削除し、'.' を '_' に置換するなど
     *  独自ルールを使うので、namedResourceList / originalFilenames を線形探索する。
     */
    const char* findBinaryDataFor(const juce::String& fileName, int& outSize)
    {
        for (int i = 0; i < WebUIBinaryData::namedResourceListSize; ++i)
        {
            const auto* originalName = WebUIBinaryData::originalFilenames[i];
            if (originalName != nullptr && fileName == juce::String::fromUTF8(originalName))
            {
                return WebUIBinaryData::getNamedResource(WebUIBinaryData::namedResourceList[i], outSize);
            }
        }
        outSize = 0;
        return nullptr;
    }

    juce::Array<juce::var> buildWaveformPeaks(const juce::AudioBuffer<float>* buffer,
                                               int targetPoints = 600)
    {
        juce::Array<juce::var> peaks;

        if (buffer == nullptr || buffer->getNumSamples() <= 0 || buffer->getNumChannels() <= 0)
            return peaks;

        const int numSamples = buffer->getNumSamples();
        const int numChannels = buffer->getNumChannels();
        const int points = juce::jlimit(1, targetPoints, numSamples);
        peaks.ensureStorageAllocated(points);

        float maxPeak = 0.0f;
        juce::Array<float> raw;
        raw.ensureStorageAllocated(points);

        for (int i = 0; i < points; ++i)
        {
            const int start = (int) ((int64) i * numSamples / points);
            const int end = juce::jmax(start + 1, (int) ((int64) (i + 1) * numSamples / points));

            float peak = 0.0f;
            for (int sample = start; sample < end; ++sample)
            {
                for (int channel = 0; channel < numChannels; ++channel)
                    peak = juce::jmax(peak, std::abs(buffer->getSample(channel, sample)));
            }

            raw.add(peak);
            maxPeak = juce::jmax(maxPeak, peak);
        }

        const float invMax = maxPeak > 0.0f ? 1.0f / maxPeak : 0.0f;
        for (float peak : raw)
            peaks.add((double) juce::jlimit(0.0f, 1.0f, peak * invMax));

        return peaks;
    }

    juce::String filesForLog(const juce::StringArray& files)
    {
        juce::String result;
        for (int i = 0; i < files.size(); ++i)
        {
            if (i > 0) result << ", ";
            result << files[i];
        }
        return result;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────
WebViewEditor::WebViewEditor(DrumSamplerAudioProcessor& p)
    : juce::AudioProcessorEditor(&p),
      audioProcessor(p),
      webView(juce::WebBrowserComponent::Options()
                  .withBackend(juce::WebBrowserComponent::Options::Backend::webview2)
                  .withWinWebView2Options(juce::WebBrowserComponent::Options::WinWebView2()
                                              .withUserDataFolder(juce::File::getSpecialLocation(juce::File::tempDirectory)))
                  .withNativeIntegrationEnabled()
                  .withResourceProvider([this](const auto& url) { return this->fetchWebResource(url); })
                  .withNativeFunction("uiMessage",
                      [this](const juce::Array<juce::var>& args,
                             juce::WebBrowserComponent::NativeFunctionCompletion completion)
                      {
                          if (args.size() > 0)
                              this->handleUiMessage(args[0]);
                          completion(juce::var());
                      })
                  .withEventListener("uiMessage",
                      [this](juce::var data)
                      {
                          this->handleUiMessage(data);
                      })
              )
{
    setSize(baseEditorWidth, baseEditorHeight);
    setResizable(true, true);
    setResizeLimits(minEditorWidth, minEditorHeight, maxEditorWidth, maxEditorHeight);

    addAndMakeVisible(webView);

    // BinaryData の index.html を WebView に読み込む
    // JUCE 8 の resource provider は固定 origin (juce://juce.backend/ など) を使うので
    // getResourceProviderRoot() から組み立てる
    webView.goToURL(juce::WebBrowserComponent::getResourceProviderRoot() + "index.html");

    // JS 側で kitData リスナを設定する時間を待ってから送る
    startTimer(120);
}

WebViewEditor::~WebViewEditor()
{
    stopTimer();
}

// ─────────────────────────────────────────────────────────────────────────────
void WebViewEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff0f0e0c));
}

void WebViewEditor::resized()
{
    webView.setBounds(getLocalBounds());
}

bool WebViewEditor::isSupportedAudioFile(const juce::File& file)
{
    const auto ext = file.getFileExtension().toLowerCase();
    return ext == ".wav" || ext == ".aif" || ext == ".aiff"
        || ext == ".mp3" || ext == ".flac";
}

bool WebViewEditor::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& path : files)
    {
        if (isSupportedAudioFile(juce::File(path)))
            return true;
    }

    return false;
}

void WebViewEditor::fileDragEnter(const juce::StringArray& files, int x, int y)
{
    juce::Logger::writeToLog("[ASTER DND] native dragover fired enter files="
                             + juce::String(files.size())
                             + " x=" + juce::String(x)
                             + " y=" + juce::String(y)
                             + " paths=" + filesForLog(files));
}

void WebViewEditor::fileDragMove(const juce::StringArray& files, int x, int y)
{
    juce::ignoreUnused(files);
    juce::Logger::writeToLog("[ASTER DND] native dragover fired move targetPadIndex="
                             + juce::String(padIndexForDropPosition(x, y)));
}

void WebViewEditor::fileDragExit(const juce::StringArray& files)
{
    juce::Logger::writeToLog("[ASTER DND] native drag exit files=" + juce::String(files.size()));
}

void WebViewEditor::filesDropped(const juce::StringArray& files, int x, int y)
{
    juce::Logger::writeToLog("[ASTER DND] native drop fired files count=" + juce::String(files.size()));
    juce::Logger::writeToLog("[ASTER DND] native dropped file path or available file info "
                             + filesForLog(files));

    int targetPad = padIndexForDropPosition(x, y);
    if ((targetPad < 0 || targetPad >= NUM_PADS) && activeWebTab == ActiveWebTab::Pads)
    {
        // Native macOS file D&D does not go through the browser's HTML5 drop
        // handlers, so waveform/editor drops do not have a pad-cell target.
        // In that case, treat the current editor as the drop target.
        targetPad = juce::jlimit(0, NUM_PADS - 1, selectedPadIndex);
    }
    if (targetPad < 0 || targetPad >= NUM_PADS)
    {
        juce::Logger::writeToLog("[ASTER DND] native bridge call error: no pad under drop position");
        return;
    }

    // ドロップ先 Pad が現在 UI で選択中の Pad と一致する場合のみ
    // 選択中 Layer に向ける。別 Pad へのドロップは MAIN (Layer 0) に載せる。
    const int rawLayer   = (targetPad == selectedPadIndex)
                             ? selectedLayerIndices[static_cast<size_t>(targetPad)]
                             : 0;
    const auto& padRef   = audioProcessor.getKit().pads[static_cast<size_t>(targetPad)];
    const int targetLayer = juce::jlimit(0, padRef.layerCount() - 1, rawLayer);

    juce::Logger::writeToLog("[ASTER DND] native target padIndex=" + juce::String(targetPad)
                             + " selectedLayerIndex=" + juce::String(targetLayer));

    for (const auto& path : files)
    {
        const juce::File file(path);
        if (! isSupportedAudioFile(file))
            continue;

        loadDroppedFileForLayer(targetPad, targetLayer, file);
        return;
    }

    juce::Logger::writeToLog("[ASTER DND] native bridge call error: no supported audio file");
}

int WebViewEditor::padIndexForDropPosition(int x, int y) const
{
    const double scale = juce::jmax(0.0001, juce::jmin((double) getWidth() / (double) baseEditorWidth,
                                                       (double) getHeight() / (double) baseEditorHeight));
    const double bx = (double) x / scale;
    const double by = (double) y / scale;

    constexpr double gridX = 30.0;
    constexpr double gridY = 219.0;
    constexpr double cellW = 117.0;
    constexpr double cellH = 128.0;
    constexpr double gapX = 8.0;
    constexpr double gapY = 8.0;

    const int col = (int) std::floor((bx - gridX) / (cellW + gapX));
    const int row = (int) std::floor((by - gridY) / (cellH + gapY));
    if (col < 0 || col >= 4 || row < 0 || row >= 4)
        return -1;

    const double cellX = gridX + (double) col * (cellW + gapX);
    const double cellY = gridY + (double) row * (cellH + gapY);
    if (bx < cellX || bx > cellX + cellW || by < cellY || by > cellY + cellH)
        return -1;

    return currentPage * PADS_PER_PAGE + row * 4 + col;
}

bool WebViewEditor::loadDroppedFileForPad(int padIndex, const juce::File& file, const juce::String& displayFileName)
{
    juce::Logger::writeToLog("[ASTER DND] C++ loadSample start padIndex="
                             + juce::String(padIndex)
                             + " file=" + file.getFullPathName());

    if (padIndex < 0 || padIndex >= NUM_PADS)
    {
        juce::Logger::writeToLog("[ASTER DND] C++ loadSample error: invalid pad index");
        return false;
    }

    if (! file.existsAsFile())
    {
        juce::Logger::writeToLog("[ASTER DND] C++ loadSample error: file does not exist");
        return false;
    }

    if (! isSupportedAudioFile(file))
    {
        juce::Logger::writeToLog("[ASTER DND] C++ loadSample error: unsupported extension "
                                 + file.getFileExtension());
        return false;
    }

    if (! audioProcessor.loadSampleForPad(padIndex, file))
    {
        juce::Logger::writeToLog("[ASTER DND] C++ loadSample error: AudioFileManager rejected file");
        return false;
    }

    if (displayFileName.isNotEmpty())
    {
        auto& pad = audioProcessor.getKit().pads[(size_t) padIndex];
        pad.sampleFileName = displayFileName;
        audioProcessor.markKitDirty();
    }

    juce::Logger::writeToLog("[ASTER DND] C++ loadSample success padIndex="
                             + juce::String(padIndex)
                             + " fileName=" + (displayFileName.isNotEmpty() ? displayFileName : file.getFileName()));
    juce::Logger::writeToLog("[ASTER DND] pad state updated padIndex="
                             + juce::String(padIndex)
                             + " samplePath="
                             + audioProcessor.getKit().pads[(size_t) padIndex].sampleFilePath);
    broadcastPadUpdate(padIndex);
    broadcastKitState();
    return true;
}

// 任意 Layer に file を読み込む。layerIndex==0 は pad-level path にディスパッチ。
bool WebViewEditor::loadDroppedFileForLayer(int padIndex, int layerIndex, const juce::File& file, const juce::String& displayFileName)
{
    juce::Logger::writeToLog("[ASTER LOAD] loadDroppedFileForLayer padIndex=" + juce::String(padIndex)
                             + " layerIndex=" + juce::String(layerIndex)
                             + " file=" + file.getFileName());

    if (layerIndex <= 0)
        return loadDroppedFileForPad(padIndex, file, displayFileName);

    if (padIndex < 0 || padIndex >= NUM_PADS) return false;
    if (! file.existsAsFile()) return false;
    if (! isSupportedAudioFile(file)) return false;

    auto& pad = audioProcessor.getKit().pads[(size_t) padIndex];
    if (layerIndex >= pad.layerCount())
    {
        juce::Logger::writeToLog("[ASTER LOAD] error: layerIndex=" + juce::String(layerIndex)
                                 + " >= layerCount=" + juce::String(pad.layerCount()));
        return false;
    }

    if (! audioProcessor.loadSampleForLayer(padIndex, layerIndex, file))
    {
        juce::Logger::writeToLog("[ASTER LOAD] error: loadSampleForLayer failed");
        return false;
    }

    if (displayFileName.isNotEmpty())
    {
        pad.layers[(size_t) layerIndex].sampleFileName = displayFileName;
        audioProcessor.markKitDirty();
    }

    const auto& bufRef = audioProcessor.getFileManager();
    const bool bufLoaded = bufRef.hasSample(padIndex, layerIndex);
    juce::Logger::writeToLog("[ASTER LOAD] success padIndex=" + juce::String(padIndex)
                             + " layerIndex=" + juce::String(layerIndex)
                             + " bufferLoaded=" + (bufLoaded ? "true" : "false")
                             + " fileName=" + (displayFileName.isNotEmpty() ? displayFileName : file.getFileName()));

    broadcastPadUpdate(padIndex);
    broadcastKitState();
    return true;
}

juce::File WebViewEditor::getDroppedSampleCacheDirectory() const
{
    return DrumSamplerAudioProcessor::getUserKitsDirectory()
        .getParentDirectory()
        .getChildFile("Dropped Samples");
}

bool WebViewEditor::loadDroppedBytesForPad(int padIndex,
                                           const juce::String& fileName,
                                           const void* data,
                                           size_t size)
{
    juce::Logger::writeToLog("[ASTER DND] C++ loadSample bytes start padIndex="
                             + juce::String(padIndex)
                             + " fileName=" + fileName
                             + " bytes=" + juce::String((int64) size));

    if (padIndex < 0 || padIndex >= NUM_PADS)
    {
        juce::Logger::writeToLog("[ASTER DND] C++ loadSample bytes error: invalid pad index");
        return false;
    }

    if (data == nullptr || size == 0)
    {
        juce::Logger::writeToLog("[ASTER DND] C++ loadSample bytes error: empty data");
        return false;
    }

    const auto legalName = juce::File::createLegalFileName(fileName).trim();
    const auto safeName = legalName.isNotEmpty() ? legalName : juce::String("Dropped Sample.wav");
    const juce::File probe(safeName);
    if (! isSupportedAudioFile(probe))
    {
        juce::Logger::writeToLog("[ASTER DND] C++ loadSample bytes error: unsupported extension "
                                 + probe.getFileExtension());
        return false;
    }

    auto directory = getDroppedSampleCacheDirectory();
    if (! directory.createDirectory())
    {
        juce::Logger::writeToLog("[ASTER DND] C++ loadSample bytes error: could not create cache directory "
                                 + directory.getFullPathName());
        return false;
    }

    auto target = directory.getChildFile(safeName);
    if (target.exists())
        target = directory.getNonexistentChildFile(probe.getFileNameWithoutExtension(),
                                                   probe.getFileExtension(),
                                                   false);

    if (! target.replaceWithData(data, size))
    {
        juce::Logger::writeToLog("[ASTER DND] C++ loadSample bytes error: could not write cache file "
                                 + target.getFullPathName());
        return false;
    }

    juce::Logger::writeToLog("[ASTER DND] C++ loadSample bytes cached path="
                             + target.getFullPathName());
    return loadDroppedFileForPad(padIndex, target, safeName);
}

// Layer 指定版 — ファイルキャッシュは同じディレクトリに置き、loadDroppedFileForLayer
// にディスパッチする。layerIndex==0 は pad-level wrapper にディスパッチされる。
bool WebViewEditor::loadDroppedBytesForLayer(int padIndex,
                                              int layerIndex,
                                              const juce::String& fileName,
                                              const void* data,
                                              size_t size)
{
    if (layerIndex <= 0)
        return loadDroppedBytesForPad(padIndex, fileName, data, size);

    if (padIndex < 0 || padIndex >= NUM_PADS) return false;
    if (data == nullptr || size == 0) return false;

    const auto legalName = juce::File::createLegalFileName(fileName).trim();
    const auto safeName = legalName.isNotEmpty() ? legalName : juce::String("Dropped Sample.wav");
    const juce::File probe(safeName);
    if (! isSupportedAudioFile(probe)) return false;

    auto directory = getDroppedSampleCacheDirectory();
    if (! directory.createDirectory()) return false;

    auto target = directory.getChildFile(safeName);
    if (target.exists())
        target = directory.getNonexistentChildFile(probe.getFileNameWithoutExtension(),
                                                   probe.getFileExtension(),
                                                   false);

    if (! target.replaceWithData(data, size)) return false;
    return loadDroppedFileForLayer(padIndex, layerIndex, target, safeName);
}

// ─────────────────────────────────────────────────────────────────────────────
// 起動直後の遅延送信
// ─────────────────────────────────────────────────────────────────────────────
void WebViewEditor::timerCallback()
{
    if (! initialKitSent)
    {
        broadcastKitState();
        broadcastSystemStats();
        lastStatsBroadcastMs = juce::Time::getMillisecondCounterHiRes();
        initialKitSent = true;
        startTimer(100);
        return;
    }

    const bool hasAudioActivity = audioProcessor.hasRecentAudioActivity(0.75);
    broadcastPadTriggers();

    // v7+: DAW automation 起因の kit 変化があれば UI に push する。
    // (UI 内のノブ位置が DAW automation 再生に追従するために必要)
    if (audioProcessor.consumeKitChangedByAutomation())
        broadcastKitState();

    int learnedPad = -1;
    int learnedNote = -1;
    if (audioProcessor.consumeLearnedMidiNote(learnedPad, learnedNote))
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty("index", learnedPad);
        obj->setProperty("note", learnedNote);
        webView.emitEventIfBrowserIsVisible("midiLearned", juce::var(obj));
    }

    if (hasAudioActivity)
    {
        if (! fastMeterTimerActive)
        {
            fastMeterTimerActive = true;
            fastTimerTick = 0;
            // 60 Hz: 滑らかなメーター更新レートを確保。1 イベント数十バイトの JSON なので、
            // 60Hz でも WebView IPC は十分軽い。
            startTimerHz(60);
        }

        // 旧実装は (% 2) で 15Hz に間引いていたが、それが「カクつき」「遅延」の
        // 主因だったため除去。毎 tick = 60Hz でメーター/マスターピークを送る。
        ++fastTimerTick;
        broadcastLevelData();
        metersWereActive = true;
    }
    else
    {
        if (metersWereActive)
        {
            // Send one final zero-ish frame so JS meters can settle, then stop
            // high-rate visual traffic while the instrument is idle.
            broadcastLevelData();
            metersWereActive = false;
        }

        if (fastMeterTimerActive)
        {
            fastMeterTimerActive = false;
            fastTimerTick = 0;
            startTimer(100);
        }
    }

    const auto nowMs = juce::Time::getMillisecondCounterHiRes();
    if (nowMs - lastStatsBroadcastMs >= 500.0)
    {
        lastStatsBroadcastMs = nowMs;
        broadcastSystemStats();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// C++ → JS: Pad 発音トリガー
// ─────────────────────────────────────────────────────────────────────────────
void WebViewEditor::broadcastPadTriggers()
{
    if (activeWebTab == ActiveWebTab::Missing)
        return;

    juce::Array<juce::var> triggers;
    triggers.ensureStorageAllocated(NUM_PADS);

    for (int padIndex = 0; padIndex < NUM_PADS; ++padIndex)
    {
        const float level = audioProcessor.consumePadTriggerLevel(padIndex);
        if (level <= 0.001f)
            continue;

        auto* item = new juce::DynamicObject();
        item->setProperty("index", padIndex);
        item->setProperty("velocity", (double) juce::jlimit(0.0f, 1.0f, level));
        triggers.add(juce::var(item));
    }

    if (triggers.isEmpty())
        return;

    auto* obj = new juce::DynamicObject();
    obj->setProperty("triggers", triggers);
    webView.emitEventIfBrowserIsVisible("padTriggers", juce::var(obj));

    // ── Layer 発音トリガー（Pads タブで選択中 Pad のみ）─────────────────────
    // 常に consume して値をクリアする（タブが違う場合も stale を残さない）。
    // Pads タブ表示中のみ JS へ emit する。
    if (selectedPadIndex >= 0 && selectedPadIndex < NUM_PADS)
    {
        const auto& selPad = audioProcessor.getKit().pads[static_cast<size_t>(selectedPadIndex)];
        const int layerCount = selPad.layerCount();
        juce::Array<juce::var> layerVels;
        bool anyFired = false;

        for (int li = 0; li < layerCount; ++li)
        {
            const float lv = audioProcessor.consumeLayerTriggerLevel(selectedPadIndex, li);
            if (activeWebTab == ActiveWebTab::Pads)
            {
                layerVels.add((double) lv);
                if (lv > 0.001f) anyFired = true;
            }
        }

        if (anyFired)
        {
            auto* lo = new juce::DynamicObject();
            lo->setProperty("padIndex", selectedPadIndex);
            lo->setProperty("layers",   layerVels);
            webView.emitEventIfBrowserIsVisible("layerTriggers", juce::var(lo));
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// C++ → JS: レベルメーターデータ
// ─────────────────────────────────────────────────────────────────────────────
void WebViewEditor::broadcastLevelData()
{
    if (activeWebTab == ActiveWebTab::Missing)
        return;

    auto* obj = new juce::DynamicObject();

    // 表示中のタブに必要な Pad だけ送る。
    // Mixer: 現在ページの 16Pad / Pads: 選択中 Pad のみ
    int startPad = selectedPadIndex;
    int count = 1;
    if (activeWebTab == ActiveWebTab::Mixer)
    {
        startPad = currentPage * PADS_PER_PAGE;
        count = PADS_PER_PAGE;
    }

    juce::Array<juce::var> padArr;
    padArr.ensureStorageAllocated(count);
    const auto& vm = audioProcessor.getVoiceManager();
    for (int i = 0; i < count; ++i)
        padArr.add((double) vm.getPadLevel(startPad + i));

    obj->setProperty("start", startPad);
    obj->setProperty("pads", padArr);

    // Per-pad clip latched 状態 (Mixer タブのみで使用、Pads タブ時は count=1)
    {
        juce::Array<juce::var> clipArr;
        clipArr.ensureStorageAllocated(count);
        for (int i = 0; i < count; ++i)
            clipArr.add(vm.getPadClipLatched(startPad + i));
        obj->setProperty("padClips", clipArr);
    }

    // マスター出力ピーク（読み取り & リセット）
    obj->setProperty("masterL", (double) audioProcessor.takeMasterPeakL());
    obj->setProperty("masterR", (double) audioProcessor.takeMasterPeakR());

    // ── Layer レベル（Pads タブで選択中 Pad のみ）────────────────────────────
    if (activeWebTab == ActiveWebTab::Pads
        && selectedPadIndex >= 0 && selectedPadIndex < NUM_PADS)
    {
        const auto& selPad = audioProcessor.getKit().pads[static_cast<size_t>(selectedPadIndex)];
        const int layerCount = selPad.layerCount();
        juce::Array<juce::var> layerArr;
        layerArr.ensureStorageAllocated(layerCount);
        for (int li = 0; li < layerCount; ++li)
            layerArr.add((double) audioProcessor.getLayerLevel(selectedPadIndex, li));
        obj->setProperty("layerPad",    selectedPadIndex);
        obj->setProperty("layerLevels", layerArr);

        // ── Compressor GR (選択 Layer の FX スロット位置ごと) ───────────────
        const int selLayer = juce::jlimit(0, layerCount - 1,
            selectedLayerIndices[static_cast<size_t>(selectedPadIndex)]);
        const auto& fxChain = selPad.layers[static_cast<size_t>(selLayer)].fxChain;
        juce::Array<juce::var> grArr;
        grArr.ensureStorageAllocated((int) fxChain.size());
        for (size_t s = 0; s < fxChain.size(); ++s)
            grArr.add((double) audioProcessor.getVoiceManager().getCompReductionDb(selectedPadIndex, selLayer, (int) s));
        obj->setProperty("compReduction", grArr);
    }

    webView.emitEventIfBrowserIsVisible("levelData", juce::var(obj));
}

void WebViewEditor::broadcastSystemStats()
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty("cpuPercent", (double) audioProcessor.getAudioProcessLoadPercent());
    obj->setProperty("sampleBytes", static_cast<double>(audioProcessor.getLoadedSampleBytes()));
    webView.emitEventIfBrowserIsVisible("systemStats", juce::var(obj));
}

// ─────────────────────────────────────────────────────────────────────────────
// Resource provider — JS が "/index.html", "/styles.css", "/app.js" を要求
// ─────────────────────────────────────────────────────────────────────────────
juce::WebBrowserComponent::Resource WebViewEditor::fetchWebResource(const juce::String& url)
{
    juce::WebBrowserComponent::Resource res;

    // URL は "juce://juce.backend/index.html" や
    // "juce://juce.backend/assets/index-BPVpUR0R.js" のような形式。
    // ファイル名だけ取り出す（クエリ/フラグメントは除去）。
    auto path = url;
    auto queryIdx = path.indexOfChar('?');
    if (queryIdx >= 0) path = path.substring(0, queryIdx);
    auto fragIdx = path.indexOfChar('#');
    if (fragIdx >= 0)  path = path.substring(0, fragIdx);

    auto slash = path.lastIndexOfChar('/');
    if (slash >= 0) path = path.substring(slash + 1);
    if (path.isEmpty()) path = "index.html";

    int dataSize = 0;
    const char* data = findBinaryDataFor(path, dataSize);

    if (data != nullptr && dataSize > 0)
    {
        res.data = std::vector<std::byte>(reinterpret_cast<const std::byte*>(data),
                                          reinterpret_cast<const std::byte*>(data) + dataSize);
        res.mimeType = mimeForPath(path).toStdString();
    }
    else
    {
        // 404 として空を返す（ログにも出す）
        juce::Logger::writeToLog("WebViewEditor: resource not found: " + url + " (file=" + path + ")");
        juce::String body = "Not found: " + path;
        res.data = std::vector<std::byte>(reinterpret_cast<const std::byte*>(body.toRawUTF8()),
                                          reinterpret_cast<const std::byte*>(body.toRawUTF8()) + (size_t) body.getNumBytesAsUTF8());
        res.mimeType = "text/plain";
    }

    return res;
}

// ─────────────────────────────────────────────────────────────────────────────
// C++ → JS: KitData ブロードキャスト
// ─────────────────────────────────────────────────────────────────────────────
juce::var WebViewEditor::padToWebVar(int padIndex) const
{
    if (padIndex < 0 || padIndex >= NUM_PADS)
        return {};

    // Step 1 hook: keep Layer 0 mirrored from the flat fields. The UI handlers
    // mutate the flat fields directly (e.g. setVolume → pad.volume = x), so we
    // re-sync Layer 0 every time we serialize the pad. This guarantees the JS
    // side sees a consistent view and the audio side (which now reads layers[])
    // also sees the latest values on the next noteOn.
    const_cast<PadData&>(audioProcessor.getKit().pads[(size_t) padIndex])
        .syncLayer0FromFlat();

    auto v = PadDataJson::padToVar(audioProcessor.getKit().pads[(size_t) padIndex]);

    if (auto* obj = v.getDynamicObject())
    {
        auto& fileManager = audioProcessor.getFileManager();
        juce::ScopedReadLock rl(fileManager.getReadWriteLock());

        // ── トップレベル: Layer 0 バッファ（後方互換用） ────────────────────
        const auto* buf0 = fileManager.getBufferNoLock(padIndex, 0);
        const double sr0  = fileManager.getSampleRate(padIndex, 0);
        const double len0Ms = (buf0 != nullptr && buf0->getNumSamples() > 0 && sr0 > 0.0)
                               ? (static_cast<double>(buf0->getNumSamples()) / sr0) * 1000.0
                               : 0.0;
        obj->setProperty("sampleLengthMs", len0Ms);
        obj->setProperty("waveformPeaks",  buildWaveformPeaks(buf0));

        // ── per-Layer: 各 Layer のバッファから sampleLengthMs / waveformPeaks を生成 ──
        // composePadView() が選択中 Layer の値を flat に上書きするため、
        // Layer ごとに正確な長さ・波形を持っていないと L2+ の波形が
        // MAIN の波形のまま変わらない / トリムが狂う問題が起きる。
        const auto& pad = audioProcessor.getKit().pads[(size_t) padIndex];
        auto layersVar = obj->getProperty("layers");
        if (auto* layerArr = layersVar.getArray())
        {
            const int count = juce::jmin((int) layerArr->size(), pad.layerCount());
            for (int li = 0; li < count; ++li)
            {
                if (auto* lo = (*layerArr)[li].getDynamicObject())
                {
                    const auto* buf  = fileManager.getBufferNoLock(padIndex, li);
                    const double sr  = fileManager.getSampleRate(padIndex, li);
                    const double len = (buf != nullptr && buf->getNumSamples() > 0 && sr > 0.0)
                                       ? (static_cast<double>(buf->getNumSamples()) / sr) * 1000.0
                                       : 0.0;
                    lo->setProperty("sampleLengthMs", len);
                    lo->setProperty("waveformPeaks",  buildWaveformPeaks(buf));

                    juce::Logger::writeToLog(
                        "[ASTER WFM] padToWebVar padIndex=" + juce::String(padIndex)
                        + " layerIndex=" + juce::String(li)
                        + " bufferFound=" + (buf != nullptr && buf->getNumSamples() > 0 ? "true" : "false")
                        + " sampleLengthMs=" + juce::String(len, 1));
                }
            }
        }
    }

    return v;
}

juce::var WebViewEditor::kitToWebVar() const
{
    const auto& kit = audioProcessor.getKit();
    auto* obj = new juce::DynamicObject();

    juce::Array<juce::var> padArr;
    padArr.ensureStorageAllocated(NUM_PADS);
    for (int i = 0; i < NUM_PADS; ++i)
        padArr.add(padToWebVar(i));

    // Single source of truth for the displayed kit identity is currentKitFile.
    // When no file is backing the kit we are on the Empty Kit, regardless of
    // whatever kit.kitName happens to hold (it can survive a DAW state restore
    // that cleared currentKitFile, which would otherwise desync the header
    // label from the dropdown selection).
    const auto currentFile = audioProcessor.getCurrentKitFile();
    const bool onDefaultKit = (currentFile == juce::File{});
    const juce::String displayKitName = onDefaultKit
        ? juce::String("Empty Kit")
        : currentFile.getFileNameWithoutExtension();

    obj->setProperty("pads", padArr);
    obj->setProperty("page", currentPage);
    obj->setProperty("selectedIndex", selectedPadIndex);
    obj->setProperty("kitName", displayKitName);
    obj->setProperty("kitDirty", audioProcessor.isKitDirty());
    obj->setProperty("kitReadOnly", onDefaultKit);
    obj->setProperty("currentKitPath", currentFile.getFullPathName());

    const char* modeStr = "48Outs";
    switch (kit.outputMode)
    {
        case OutputMode::Stereo: modeStr = "Stereo"; break;
        case OutputMode::Outs16: modeStr = "16Outs"; break;
        case OutputMode::Outs32: modeStr = "32Outs"; break;
        case OutputMode::Outs48: modeStr = "48Outs"; break;
    }
    obj->setProperty("outputMode", juce::String(modeStr));
    obj->setProperty("masterVolume", (double) kit.masterVolume);

    return juce::var(obj);
}

void WebViewEditor::broadcastKitState()
{
    webView.emitEventIfBrowserIsVisible("kitData", kitToWebVar());
    broadcastKitList();
}

void WebViewEditor::broadcastKitList()
{
    auto* obj = new juce::DynamicObject();
    juce::Array<juce::var> items;

    auto* defaultItem = new juce::DynamicObject();
    defaultItem->setProperty("name", "Empty Kit");
    defaultItem->setProperty("path", "");
    defaultItem->setProperty("isDefault", true);
    items.add(juce::var(defaultItem));

    const auto currentFile = audioProcessor.getCurrentKitFile();
    const auto currentPath = currentFile.getFullPathName();
    obj->setProperty("currentKitPath", currentPath);

    bool currentInList = false;
    for (const auto& file : DrumSamplerAudioProcessor::getSavedKitFiles())
    {
        auto* item = new juce::DynamicObject();
        item->setProperty("name", file.getFileNameWithoutExtension());
        item->setProperty("path", file.getFullPathName());
        item->setProperty("isDefault", false);
        items.add(juce::var(item));

        if (file == currentFile) currentInList = true;
    }

    // If the active kit file isn't under the user kits directory (or was
    // removed externally), append it so the dropdown's checked entry always
    // matches the header label.
    if (currentFile != juce::File{} && ! currentInList)
    {
        auto* item = new juce::DynamicObject();
        item->setProperty("name", currentFile.getFileNameWithoutExtension());
        item->setProperty("path", currentPath);
        item->setProperty("isDefault", false);
        items.add(juce::var(item));
    }

    obj->setProperty("items", items);
    webView.emitEventIfBrowserIsVisible("kitList", juce::var(obj));
}

void WebViewEditor::broadcastPadUpdate(int padIndex)
{
    if (padIndex < 0 || padIndex >= NUM_PADS) return;
    auto* obj = new juce::DynamicObject();
    obj->setProperty("index", padIndex);
    obj->setProperty("pad", padToWebVar(padIndex));
    webView.emitEventIfBrowserIsVisible("padUpdated", juce::var(obj));
}

// ─────────────────────────────────────────────────────────────────────────────
// JS → C++: メッセージ処理
// ─────────────────────────────────────────────────────────────────────────────
void WebViewEditor::handleUiMessage(const juce::var& message)
{
    if (! message.isObject()) return;

    const auto type = message.getProperty("type", juce::var()).toString();
    const auto payload = message.getProperty("payload", juce::var());

    const auto getIndex = [&]() -> int
    {
        return (int) payload.getProperty("index", -1);
    };
    const auto getFloat = [&]() -> float
    {
        return (float) (double) payload.getProperty("value", 0.0);
    };
    const auto getBool = [&]() -> bool
    {
        return (bool) payload.getProperty("value", false);
    };

    if (type == "ready")
    {
        broadcastKitState();
    }
    else if (type == "selectPad")
    {
        selectedPadIndex = juce::jlimit(0, NUM_PADS - 1, getIndex());
        currentPage = selectedPadIndex / PADS_PER_PAGE;
    }
    else if (type == "selectLayer")
    {
        // JS がレイヤータブを切り替えたときに通知してくる。
        // ネイティブ DnD 時にどの Layer に向けるかを決めるために保存する。
        const int padIdx   = getIndex();
        const int layerIdx = (int) payload.getProperty("layerIndex", 0);
        if (padIdx >= 0 && padIdx < NUM_PADS)
        {
            selectedLayerIndices[static_cast<size_t>(padIdx)] =
                juce::jlimit(0, MAX_LAYERS_PER_PAD - 1, layerIdx);
            juce::Logger::writeToLog("[ASTER LAYER] selectLayer padIndex=" + juce::String(padIdx)
                                     + " layerIndex=" + juce::String(layerIdx));
        }
    }
    else if (type == "setPage")
    {
        currentPage = juce::jlimit(0, NUM_PAGES - 1, (int) payload.getProperty("page", 0));
    }
    else if (type == "setTab")
    {
        const auto tab = payload.getProperty("tab", "PADS").toString();
        if (tab == "MIXER")
            activeWebTab = ActiveWebTab::Mixer;
        else if (tab == "MISSING")
            activeWebTab = ActiveWebTab::Missing;
        else
            activeWebTab = ActiveWebTab::Pads;
    }
    else if (type == "setUiScale")
    {
        const auto scale = juce::jlimit(0.5, 2.0, (double) payload.getProperty("scale", 1.0));
        setSize(juce::roundToInt((double) baseEditorWidth * scale),
                juce::roundToInt((double) baseEditorHeight * scale));
    }
    else if (type == "setOutputMode")
    {
        const auto mode = payload.getProperty("value", "48Outs").toString();
        if (mode == "Stereo")
            audioProcessor.getKit().outputMode = OutputMode::Stereo;
        else if (mode == "16Outs")
            audioProcessor.getKit().outputMode = OutputMode::Outs16;
        else if (mode == "32Outs")
            audioProcessor.getKit().outputMode = OutputMode::Outs32;
        else
            audioProcessor.getKit().outputMode = OutputMode::Outs48;

        audioProcessor.markKitDirty();
        broadcastKitState();
    }
    else if (type == "setMasterVolume")
    {
        // フェーダー位置 [0..1]。APVTS パラメータ経由で設定し、ホストへ automation /
        // change gesture を通知する (kit.masterVolume も同期される)。
        const float pos = juce::jlimit(0.0f, 1.0f,
            (float) (double) payload.getProperty("value", 0.75));
        audioProcessor.setMasterVolumeParameter(pos, true);
    }
    else if (type == "audition")
    {
        const float velocity = (float) (double) payload.getProperty("velocity", 0.9);
        audioProcessor.auditionPadOn(getIndex(), velocity);
    }
    else if (type == "auditionOff")
    {
        audioProcessor.auditionPadOff(getIndex());
    }
    // ── 連続パラメータ（ドラッグで送られてくる） ─────────────────────────
    // JS 側は楽観的にローカル state を既に更新済み。
    // C++ → JS への echo (broadcastPadUpdate) は同じ値で setPads を発火させ、
    // App 全体の再レンダーを誘発するので 60fps のドラッグ中は致命的に重い。
    // → 連続パラメータでは echo を出さない。離散値（mute/solo 等）だけ echo する。
    else if (type == "setVolume")
    {
        audioProcessor.setAutomatablePadParameter(getIndex(),
                                                  PadParameterSpecs::Param::Volume,
                                                  getFloat());
    }
    else if (type == "setPadVolume")
    {
        audioProcessor.setAutomatablePadParameter(getIndex(),
                                                  PadParameterSpecs::Param::PadVolume,
                                                  getFloat());
    }
    else if (type == "setPan")
    {
        audioProcessor.setAutomatablePadParameter(getIndex(),
                                                  PadParameterSpecs::Param::Pan,
                                                  getFloat());
    }
    else if (type == "setPitch")
    {
        audioProcessor.setAutomatablePadParameter(getIndex(),
                                                  PadParameterSpecs::Param::Pitch,
                                                  getFloat());
    }
    else if (type == "setMute")
    {
        const int idx = getIndex();
        if (idx >= 0 && idx < NUM_PADS)
        {
            audioProcessor.getKit().pads[(size_t) idx].mute = getBool();
            audioProcessor.markKitDirty();
            broadcastPadUpdate(idx);
        }
    }
    else if (type == "setSolo")
    {
        const int idx = getIndex();
        if (idx >= 0 && idx < NUM_PADS)
        {
            const bool v = getBool();
            audioProcessor.getKit().pads[(size_t) idx].solo = v;
            if (v) audioProcessor.getKit().pads[(size_t) idx].mute = false;
            audioProcessor.markKitDirty();
            broadcastPadUpdate(idx);
        }
    }
    else if (type == "setReverse")
    {
        const int idx = getIndex();
        if (idx >= 0 && idx < NUM_PADS)
        {
            audioProcessor.setAutomatablePadParameter(idx,
                                                      PadParameterSpecs::Param::Reverse,
                                                      getBool() ? 1.0f : 0.0f);
            broadcastPadUpdate(idx);
        }
    }
    // ── 連続パラメータ（ATK/REL/VEL/HUM/Trim/Fade）も echo しない ─────────
    else if (type == "setAttack")
    {
        // setVolume 等と同様 setAutomatablePadParameter 経由に統一。
        // これで APVTS 同期 + syncLayer0FromFlat (ボイスが読む layer0 へ反映) が
        // 行われ、再生中でも次トリガーから即変化する。直書きだと layer0 に届かず、
        // かつ他パラメータ編集時に APVTS 旧値へ巻き戻っていた。
        audioProcessor.setAutomatablePadParameter(getIndex(),
                                                  PadParameterSpecs::Param::Attack,
                                                  getFloat());
    }
    else if (type == "setRelease")
    {
        audioProcessor.setAutomatablePadParameter(getIndex(),
                                                  PadParameterSpecs::Param::Release,
                                                  getFloat());
    }
    else if (type == "setVelocitySens")
    {
        audioProcessor.setAutomatablePadParameter(getIndex(),
                                                  PadParameterSpecs::Param::Velocity,
                                                  getFloat());
    }
    else if (type == "setHumanize")
    {
        audioProcessor.setAutomatablePadParameter(getIndex(),
                                                  PadParameterSpecs::Param::Humanize,
                                                  getFloat());
    }
    else if (type == "setPolyphony")
    {
        const int idx = getIndex();
        if (idx >= 0 && idx < NUM_PADS)
        {
            audioProcessor.getKit().pads[(size_t) idx].polyphony =
                juce::jlimit(0, 16, (int) getFloat());
            audioProcessor.markKitDirty();
        }
    }
    else if (type == "setVoiceSteal")
    {
        const int idx = getIndex();
        if (idx >= 0 && idx < NUM_PADS)
        {
            const auto val = payload.getProperty("value", juce::var()).toString();
            auto mode = PadData::VoiceStealMode::Oldest;
            if      (val == "quietest") mode = PadData::VoiceStealMode::Quietest;
            else if (val == "off")      mode = PadData::VoiceStealMode::Off;
            audioProcessor.getKit().pads[(size_t) idx].voiceSteal = mode;
            audioProcessor.markKitDirty();
        }
    }
    else if (type == "resetPadClip")
    {
        // payload = { index } または { index: -1 } で全 Pad リセット
        const int idx = (int) payload.getProperty("index", -1);
        auto& vm = audioProcessor.getVoiceManager();
        if (idx < 0)
            vm.clearAllPadClips();
        else if (idx < NUM_PADS)
            vm.clearPadClip(idx);
    }
    else if (type == "setVelCurve")
    {
        // payload = { index, value: { preset, p1x, p1y, p2x, p2y } }
        const int idx = getIndex();
        if (idx >= 0 && idx < NUM_PADS)
        {
            const auto val = payload.getProperty("value", juce::var());
            if (auto* obj = val.getDynamicObject())
            {
                auto& vc = audioProcessor.getKit().pads[(size_t) idx].velCurve;
                vc.presetIndex = juce::jlimit(0, 5, (int) obj->getProperty("preset"));
                vc.p1x = juce::jlimit(0.0f, 1.0f, (float) (double) obj->getProperty("p1x"));
                vc.p1y = juce::jlimit(0.0f, 1.0f, (float) (double) obj->getProperty("p1y"));
                vc.p2x = juce::jlimit(0.0f, 1.0f, (float) (double) obj->getProperty("p2x"));
                vc.p2y = juce::jlimit(0.0f, 1.0f, (float) (double) obj->getProperty("p2y"));
                // p1.x < p2.x を保証
                if (vc.p1x > vc.p2x) std::swap(vc.p1x, vc.p2x);
                audioProcessor.markKitDirty();
            }
        }
    }
    else if (type == "setStartPosition")
    {
        const int idx = getIndex();
        if (idx >= 0 && idx < NUM_PADS)
        {
            auto& pad = audioProcessor.getKit().pads[(size_t) idx];
            audioProcessor.setPadSampleTrim(idx, getFloat(),
                                            pad.endPosition, pad.fadeIn, pad.fadeOut);
        }
    }
    else if (type == "setEndPosition")
    {
        const int idx = getIndex();
        if (idx >= 0 && idx < NUM_PADS)
        {
            auto& pad = audioProcessor.getKit().pads[(size_t) idx];
            audioProcessor.setPadSampleTrim(idx, pad.startPosition,
                                            getFloat(), pad.fadeIn, pad.fadeOut);
        }
    }
    else if (type == "setFadeIn")
    {
        const int idx = getIndex();
        if (idx >= 0 && idx < NUM_PADS)
        {
            auto& pad = audioProcessor.getKit().pads[(size_t) idx];
            audioProcessor.setPadSampleTrim(idx, pad.startPosition,
                                            pad.endPosition, getFloat(), pad.fadeOut);
        }
    }
    else if (type == "setFadeOut")
    {
        const int idx = getIndex();
        if (idx >= 0 && idx < NUM_PADS)
        {
            auto& pad = audioProcessor.getKit().pads[(size_t) idx];
            audioProcessor.setPadSampleTrim(idx, pad.startPosition,
                                            pad.endPosition, pad.fadeIn, getFloat());
        }
    }
    else if (type == "setSampleTrim")
    {
        const int idx = getIndex();
        if (idx >= 0 && idx < NUM_PADS)
        {
            auto& pad = audioProcessor.getKit().pads[(size_t) idx];
            const float startPosition = (float) (double) payload.getProperty("startPosition", pad.startPosition);
            const float endPosition   = (float) (double) payload.getProperty("endPosition",   pad.endPosition);
            const float fadeIn        = (float) (double) payload.getProperty("fadeIn",        pad.fadeIn);
            const float fadeOut       = (float) (double) payload.getProperty("fadeOut",       pad.fadeOut);
            audioProcessor.setPadSampleTrim(idx, startPosition, endPosition, fadeIn, fadeOut);
        }
    }
    else if (type == "setSmartTrim")
    {
        // smartTrim は PadData にメンバが無いため、グローバル設定 or no-op
        // とりあえずログ
        juce::Logger::writeToLog("setSmartTrim (UI only) idx=" + juce::String(getIndex()));
    }
    else if (type == "setPlaybackMode")
    {
        const int idx = getIndex();
        const auto modeStr = payload.getProperty("value", juce::var()).toString();
        if (idx >= 0 && idx < NUM_PADS)
        {
            audioProcessor.getKit().pads[(size_t) idx].playbackMode =
                (modeStr == "OneShot") ? PlaybackMode::OneShot : PlaybackMode::Gate;
            audioProcessor.markKitDirty();
            broadcastPadUpdate(idx);
        }
    }
    else if (type == "setChoke")
    {
        const int idx = getIndex();
        const int v = (int) payload.getProperty("value", 0);
        if (idx >= 0 && idx < NUM_PADS)
        {
            audioProcessor.getKit().pads[(size_t) idx].chokeGroup = juce::jlimit(0, 4, v);
            audioProcessor.markKitDirty();
            broadcastPadUpdate(idx);
        }
    }
    else if (type == "setPadColor")
    {
        const int idx = getIndex();
        if (idx >= 0 && idx < NUM_PADS)
        {
            auto& padRef = audioProcessor.getKit().pads[(size_t) idx];
            const auto mode = payload.getProperty("mode", juce::var()).toString();
            if (mode == "auto")
            {
                padRef.padColourARGB = KitData::defaultPadColourForIndex(idx);
                padRef.padColourMode = PadColourMode::Auto;
            }
            else if (mode == "custom")
            {
                const auto argb = (juce::int64) payload.getProperty("argb", 0);
                padRef.padColourARGB = static_cast<juce::uint32>(argb);
                padRef.padColourMode = PadColourMode::Custom;
            }
            audioProcessor.markKitDirty();
            broadcastPadUpdate(idx);
        }
    }
    else if (type == "setPadName")
    {
        const int idx = getIndex();
        const auto name = payload.getProperty("value", juce::var()).toString().trim();
        if (idx >= 0 && idx < NUM_PADS && name.isNotEmpty())
        {
            audioProcessor.getKit().pads[(size_t) idx].padName = name;
            audioProcessor.markKitDirty();
            broadcastPadUpdate(idx);
        }
    }
    else if (type == "setMidiNote")
    {
        const int idx = getIndex();
        const int note = (int) payload.getProperty("value", -1);
        const int swappedPadIndex = audioProcessor.setPadMidiNote(idx, note);
        if (swappedPadIndex >= -1)
        {
            broadcastPadUpdate(idx);
            if (swappedPadIndex >= 0)
                broadcastPadUpdate(swappedPadIndex);
        }
    }
    else if (type == "beginMidiLearn")
    {
        audioProcessor.beginMidiLearnForPad(getIndex());
    }
    else if (type == "cancelMidiLearn")
    {
        audioProcessor.cancelMidiLearn();
    }
    else if (type == "setOutput")
    {
        const int idx = getIndex();
        const int v = (int) payload.getProperty("value", 0);
        if (idx >= 0 && idx < NUM_PADS)
        {
            audioProcessor.getKit().pads[(size_t) idx].outputAssign = juce::jlimit(0, 47, v);
            audioProcessor.markKitDirty();
            broadcastPadUpdate(idx);
        }
    }
    else if (type == "copyPad")
    {
        const int idx = getIndex();
        if (idx >= 0 && idx < NUM_PADS)
            audioProcessor.copyPad(idx);
    }
    else if (type == "pastePad")
    {
        const int idx = getIndex();
        if (idx >= 0 && idx < NUM_PADS)
        {
            audioProcessor.pastePad(idx);
            broadcastKitState();
        }
    }
    else if (type == "resetPadSettings")
    {
        const int idx = getIndex();
        if (idx >= 0 && idx < NUM_PADS)
        {
            audioProcessor.resetPadSettings(idx);
            broadcastPadUpdate(idx);
        }
    }
    else if (type == "revealSample")
    {
        const int idx = getIndex();
        const int layerIdx = (int) payload.getProperty("layerIndex", 0);
        if (idx >= 0 && idx < NUM_PADS)
        {
            const auto& pad = audioProcessor.getKit().pads[(size_t) idx];
            juce::String path;
            bool missing = false;
            if (layerIdx > 0 && layerIdx < pad.layerCount())
            {
                path = pad.layers[(size_t) layerIdx].sampleFilePath;
                missing = pad.layers[(size_t) layerIdx].sampleMissing;
            }
            else
            {
                path = pad.sampleFilePath;
                missing = pad.sampleMissing;
            }
            const juce::File file(path);
            if (path.isNotEmpty() && ! missing && file.existsAsFile())
                file.revealToUser();
        }
    }
    else if (type == "clearPadSample")
    {
        // UI 側 Clear Sample → エンジン側の sample buffer / file path /
        // sampleMissing / clip latch まで完全に落とす。
        // layerIndex 指定があれば該当 Layer だけクリア、なければ Pad 全体。
        const int idx = getIndex();
        const int layerIdx = (int) payload.getProperty("layerIndex", -1);
        if (idx >= 0 && idx < NUM_PADS)
        {
            if (layerIdx >= 0)
                audioProcessor.clearLayerSample(idx, layerIdx);
            else
                audioProcessor.clearPadSample(idx);
            broadcastPadUpdate(idx);
        }
    }
    else if (type == "loadSampleDialog")
    {
        const int idx = getIndex();
        const int layerIdx = (int) payload.getProperty("layerIndex", 0);
        auto chooser = std::make_shared<juce::FileChooser>(
            "Select audio sample…", juce::File(), "*.wav;*.aif;*.aiff;*.mp3;*.flac");

        chooser->launchAsync(juce::FileBrowserComponent::openMode
                            | juce::FileBrowserComponent::canSelectFiles,
            [this, idx, layerIdx, chooser](const juce::FileChooser& fc)
            {
                auto file = fc.getResult();
                if (file.existsAsFile())
                    loadDroppedFileForLayer(idx, layerIdx, file);
            });
    }
    else if (type == "relinkSampleDialog")
    {
        const int idx = getIndex();
        if (idx < 0 || idx >= NUM_PADS)
            return;

        const auto& pad = audioProcessor.getKit().pads[(size_t) idx];
        juce::File initialFile;
        if (pad.sampleFilePath.isNotEmpty())
            initialFile = juce::File(pad.sampleFilePath);

        auto chooser = std::make_shared<juce::FileChooser>(
            "Relink sample for Pad " + juce::String(idx + 1),
            initialFile,
            "*.wav;*.aif;*.aiff;*.mp3;*.flac");

        chooser->launchAsync(juce::FileBrowserComponent::openMode
                            | juce::FileBrowserComponent::canSelectFiles,
            [this, idx, chooser](const juce::FileChooser& fc)
            {
                const auto file = fc.getResult();
                if (! file.existsAsFile())
                    return;

                if (audioProcessor.relinkPadSample(idx, file))
                    broadcastKitState();
            });
    }
    else if (type == "loadSampleFromPath")
    {
        const int idx = getIndex();
        const int layerIdx = (int) payload.getProperty("layerIndex", 0);
        const auto path = payload.getProperty("filePath", juce::var()).toString();
        const auto name = payload.getProperty("fileName", juce::var()).toString();
        juce::Logger::writeToLog("[ASTER DND] bridge call start type=loadSampleFromPath padIndex="
                                 + juce::String(idx)
                                 + " layerIndex=" + juce::String(layerIdx)
                                 + " fileName=" + name
                                 + " filePath=" + path);
        if (path.isEmpty())
        {
            juce::Logger::writeToLog("[ASTER DND] bridge call error: empty filePath");
            return;
        }

        const bool ok = loadDroppedFileForLayer(idx, layerIdx, juce::File(path));
        juce::Logger::writeToLog(ok ? "[ASTER DND] bridge call success"
                                    : "[ASTER DND] bridge call error");
    }
    else if (type == "beginSampleBytesDrop")
    {
        pendingSampleByteDrop = {};
        pendingSampleByteDrop.transferId = payload.getProperty("transferId", juce::var()).toString();
        pendingSampleByteDrop.padIndex = getIndex();
        pendingSampleByteDrop.layerIndex = (int) payload.getProperty("layerIndex", 0);
        pendingSampleByteDrop.fileName = payload.getProperty("fileName", juce::var()).toString();
        pendingSampleByteDrop.expectedChunks = (int) payload.getProperty("totalChunks", 0);
        pendingSampleByteDrop.expectedBytes = (int64) payload.getProperty("totalBytes", 0);
        pendingSampleByteDrop.active = pendingSampleByteDrop.transferId.isNotEmpty()
                                    && pendingSampleByteDrop.padIndex >= 0
                                    && pendingSampleByteDrop.padIndex < NUM_PADS
                                    && pendingSampleByteDrop.expectedChunks > 0
                                    && pendingSampleByteDrop.expectedBytes > 0;

        juce::Logger::writeToLog("[ASTER DND] bridge bytes begin transferId="
                                 + pendingSampleByteDrop.transferId
                                 + " padIndex=" + juce::String(pendingSampleByteDrop.padIndex)
                                 + " fileName=" + pendingSampleByteDrop.fileName
                                 + " totalBytes=" + juce::String(pendingSampleByteDrop.expectedBytes)
                                 + " totalChunks=" + juce::String(pendingSampleByteDrop.expectedChunks)
                                 + " active=" + (pendingSampleByteDrop.active ? "true" : "false"));
    }
    else if (type == "sampleBytesChunk")
    {
        const auto transferId = payload.getProperty("transferId", juce::var()).toString();
        const int chunkIndex = (int) payload.getProperty("chunkIndex", -1);
        const auto base64 = payload.getProperty("data", juce::var()).toString();

        if (! pendingSampleByteDrop.active || transferId != pendingSampleByteDrop.transferId)
        {
            juce::Logger::writeToLog("[ASTER DND] bridge bytes chunk error: no matching active transfer");
            return;
        }

        if (chunkIndex < 0 || chunkIndex >= pendingSampleByteDrop.expectedChunks || base64.isEmpty())
        {
            juce::Logger::writeToLog("[ASTER DND] bridge bytes chunk error: invalid chunk");
            pendingSampleByteDrop = {};
            return;
        }

        juce::MemoryOutputStream out(pendingSampleByteDrop.data, true);
        if (! juce::Base64::convertFromBase64(out, base64))
        {
            juce::Logger::writeToLog("[ASTER DND] bridge bytes chunk error: base64 decode failed");
            pendingSampleByteDrop = {};
            return;
        }

        ++pendingSampleByteDrop.receivedChunks;
    }
    else if (type == "finishSampleBytesDrop")
    {
        const auto transferId = payload.getProperty("transferId", juce::var()).toString();
        if (! pendingSampleByteDrop.active || transferId != pendingSampleByteDrop.transferId)
        {
            juce::Logger::writeToLog("[ASTER DND] bridge bytes finish error: no matching active transfer");
            return;
        }

        const auto padIndex = pendingSampleByteDrop.padIndex;
        const auto layerIndex = pendingSampleByteDrop.layerIndex;
        const auto fileName = pendingSampleByteDrop.fileName;
        const auto receivedChunks = pendingSampleByteDrop.receivedChunks;
        const auto expectedChunks = pendingSampleByteDrop.expectedChunks;
        const auto expectedBytes = pendingSampleByteDrop.expectedBytes;
        auto data = std::move(pendingSampleByteDrop.data);
        pendingSampleByteDrop = {};

        if (receivedChunks != expectedChunks || (int64) data.getSize() != expectedBytes)
        {
            juce::Logger::writeToLog("[ASTER DND] bridge bytes finish error: incomplete transfer receivedChunks="
                                     + juce::String(receivedChunks)
                                     + " expectedChunks=" + juce::String(expectedChunks)
                                     + " bytes=" + juce::String((int64) data.getSize())
                                     + " expectedBytes=" + juce::String(expectedBytes));
            return;
        }

        const bool ok = loadDroppedBytesForLayer(padIndex, layerIndex, fileName, data.getData(), data.getSize());
        juce::Logger::writeToLog(ok ? "[ASTER DND] bridge bytes finish success"
                                    : "[ASTER DND] bridge bytes finish error");
    }
    else if (type == "swapPadSounds")
    {
        const int sourceIndex = (int) payload.getProperty("sourceIndex", -1);
        const int targetIndex = (int) payload.getProperty("targetIndex", -1);
        juce::Logger::writeToLog("[ASTER PAD SWAP] bridge call start sourceIndex="
                                 + juce::String(sourceIndex)
                                 + " targetIndex=" + juce::String(targetIndex));

        if (sourceIndex < 0 || sourceIndex >= NUM_PADS
            || targetIndex < 0 || targetIndex >= NUM_PADS
            || sourceIndex == targetIndex)
        {
            juce::Logger::writeToLog("[ASTER PAD SWAP] bridge call error: invalid pad index");
            return;
        }

        const auto sourceMidi = audioProcessor.getKit().pads[(size_t) sourceIndex].midiNote;
        const auto targetMidi = audioProcessor.getKit().pads[(size_t) targetIndex].midiNote;
        const auto sourceOutput = audioProcessor.getKit().pads[(size_t) sourceIndex].outputAssign;
        const auto targetOutput = audioProcessor.getKit().pads[(size_t) targetIndex].outputAssign;

        audioProcessor.swapPads(sourceIndex, targetIndex);

        const auto& sourcePad = audioProcessor.getKit().pads[(size_t) sourceIndex];
        const auto& targetPad = audioProcessor.getKit().pads[(size_t) targetIndex];
        const bool slotsPreserved = sourcePad.midiNote == sourceMidi
                                 && targetPad.midiNote == targetMidi
                                 && sourcePad.outputAssign == sourceOutput
                                 && targetPad.outputAssign == targetOutput;

        juce::Logger::writeToLog(juce::String("[ASTER PAD SWAP] bridge call success slotFixedPreserved=")
                                 + (slotsPreserved ? "true" : "false"));
        juce::Logger::writeToLog("[ASTER PAD SWAP] pad state updated sourceIndex="
                                 + juce::String(sourceIndex)
                                 + " targetIndex=" + juce::String(targetIndex));
        broadcastPadUpdate(sourceIndex);
        broadcastPadUpdate(targetIndex);
        broadcastKitState();
    }
    else if (type == "saveKit")
    {
        if (audioProcessor.saveKitToCurrentFile())
        {
            broadcastKitState();
            return;
        }

        auto suggested = DrumSamplerAudioProcessor::getUserKitsDirectory()
            .getChildFile("My Kit.asterkit");
        auto chooser = std::make_shared<juce::FileChooser>(
            "Save Kit As…", suggested, "*.asterkit");

        chooser->launchAsync(juce::FileBrowserComponent::saveMode
                            | juce::FileBrowserComponent::canSelectFiles
                            | juce::FileBrowserComponent::warnAboutOverwriting,
            [this, chooser](const juce::FileChooser& fc)
            {
                auto file = fc.getResult();
                if (file == juce::File{})
                    return;

                if (audioProcessor.saveKitToFile(file))
                    broadcastKitState();
            });
    }
    else if (type == "saveKitAs")
    {
        const auto currentName = DrumSamplerAudioProcessor::isDefaultKitName(audioProcessor.getKit().kitName)
            ? juce::String("My Kit")
            : audioProcessor.getKit().kitName;
        auto suggested = DrumSamplerAudioProcessor::getUserKitsDirectory()
            .getChildFile(currentName + ".asterkit");
        auto chooser = std::make_shared<juce::FileChooser>(
            "Save Kit As…", suggested, "*.asterkit");

        chooser->launchAsync(juce::FileBrowserComponent::saveMode
                            | juce::FileBrowserComponent::canSelectFiles
                            | juce::FileBrowserComponent::warnAboutOverwriting,
            [this, chooser](const juce::FileChooser& fc)
            {
                auto file = fc.getResult();
                if (file == juce::File{})
                    return;

                if (audioProcessor.saveKitToFile(file))
                    broadcastKitState();
            });
    }
    else if (type == "loadKit")
    {
        DrumSamplerAudioProcessor::KitLoadOptions options;
        options.samples = (bool) payload.getProperty("samples", true);
        options.padNamesAndColours = (bool) payload.getProperty("padNamesAndColours", true);
        options.padParameters = (bool) payload.getProperty("padParameters", true);
        options.mixerSettings = (bool) payload.getProperty("mixerSettings", true);
        options.routing = (bool) payload.getProperty("routing", true);

        auto chooser = std::make_shared<juce::FileChooser>(
            "Load Kit…", DrumSamplerAudioProcessor::getUserKitsDirectory(), "*.asterkit;*.drumkit");

        chooser->launchAsync(juce::FileBrowserComponent::openMode
                            | juce::FileBrowserComponent::canSelectFiles,
            [this, chooser, options](const juce::FileChooser& fc)
            {
                auto file = fc.getResult();
                if (file == juce::File{})
                    return;

                if (audioProcessor.loadKitFromFile(file, options))
                    broadcastKitState();
            });
    }
    else if (type == "loadKitByPath")
    {
        const auto path = payload.getProperty("path", juce::var()).toString();

        // When the payload carries explicit category flags, honour them.
        // Otherwise default to a full load (legacy callers).
        const bool hasOptions = payload.hasProperty("samples")
                             || payload.hasProperty("padNamesAndColours")
                             || payload.hasProperty("padParameters")
                             || payload.hasProperty("mixerSettings")
                             || payload.hasProperty("routing");

        DrumSamplerAudioProcessor::KitLoadOptions options;
        if (hasOptions)
        {
            options.samples            = (bool) payload.getProperty("samples", true);
            options.padNamesAndColours = (bool) payload.getProperty("padNamesAndColours", true);
            options.padParameters      = (bool) payload.getProperty("padParameters", true);
            options.mixerSettings      = (bool) payload.getProperty("mixerSettings", true);
            options.routing            = (bool) payload.getProperty("routing", true);
        }

        bool ok = false;
        if (path.isEmpty())
            ok = audioProcessor.applyDefaultKit(options);
        else
            ok = audioProcessor.loadKitFromFile(juce::File(path), options);

        if (ok)
            broadcastKitState();
    }
    else if (type == "switchKitByPath")
    {
        const auto path = payload.getProperty("path", juce::var()).toString();
        const bool saveCurrent = (bool) payload.getProperty("saveCurrent", false);
        const auto loadTarget = [this, path]()
        {
            bool ok = false;
            if (path.isEmpty())
            {
                audioProcessor.newKit();
                ok = true;
            }
            else
            {
                ok = audioProcessor.loadKitFromFile(juce::File(path));
            }

            if (ok)
                broadcastKitState();
        };

        if (! saveCurrent)
        {
            loadTarget();
            return;
        }

        if (audioProcessor.saveKitToCurrentFile())
        {
            loadTarget();
            return;
        }

        const auto currentName = DrumSamplerAudioProcessor::isDefaultKitName(audioProcessor.getKit().kitName)
            ? juce::String("My Kit")
            : audioProcessor.getKit().kitName;
        auto suggested = DrumSamplerAudioProcessor::getUserKitsDirectory()
            .getChildFile(currentName + ".asterkit");
        auto chooser = std::make_shared<juce::FileChooser>(
            "Save Kit As…", suggested, "*.asterkit");

        chooser->launchAsync(juce::FileBrowserComponent::saveMode
                            | juce::FileBrowserComponent::canSelectFiles
                            | juce::FileBrowserComponent::warnAboutOverwriting,
            [this, chooser, path](const juce::FileChooser& fc)
            {
                const auto file = fc.getResult();
                if (file == juce::File{})
                    return;

                if (! audioProcessor.saveKitToFile(file))
                    return;

                bool ok = false;
                if (path.isEmpty())
                {
                    audioProcessor.newKit();
                    ok = true;
                }
                else
                {
                    ok = audioProcessor.loadKitFromFile(juce::File(path));
                }

                if (ok)
                    broadcastKitState();
            });
    }
    else if (type == "requestKitList")
    {
        broadcastKitList();
    }
    else if (type == "revealKitFolder")
    {
        DrumSamplerAudioProcessor::getUserKitsDirectory().revealToUser();
    }
    else if (type == "rescanKitFolder")
    {
        // Disk is the source of truth for the User Kits directory. We just
        // re-broadcast the kit list (broadcastKitList re-reads disk) — and if
        // the currently selected kit file has been deleted externally, fall
        // back to the Empty Kit so the header/dropdown stay consistent.
        auto dir = DrumSamplerAudioProcessor::getUserKitsDirectory();
        if (! dir.exists())
            dir.createDirectory();

        const auto currentFile = audioProcessor.getCurrentKitFile();
        if (currentFile != juce::File{} && ! currentFile.existsAsFile())
        {
            audioProcessor.newKit();
            broadcastKitState();
        }
        else
        {
            broadcastKitList();
        }
    }
    // ─────────────────────────────────────────────────────────────────────
    // Layer 操作 — Pad 内のレイヤー構造を変更する / Layer 個別の値を編集する
    //
    // 設計:
    //   - addLayer / removeLayer は構造変更。AudioFileManager の slot もクリア。
    //   - setLayer* は pad.layers[layerIndex] を直接ミラーなしで書き換える。
    //     Layer 0 を編集した場合のみ flat fields にもミラーする (VoiceManager が
    //     pad.layers[0] を読む運用なので、本来は不要だが互換のため)。
    //   - layerIndex の範囲チェックは必ず行う (UI 側のレース回避)。
    // ─────────────────────────────────────────────────────────────────────
    else if (type == "addLayer")
    {
        const int idx = getIndex();
        if (idx >= 0 && idx < NUM_PADS)
        {
            auto& pad = audioProcessor.getKit().pads[(size_t) idx];
            if (pad.layerCount() < MAX_LAYERS_PER_PAD)
            {
                const int copyFromIndex = juce::jlimit(0, pad.layerCount() - 1,
                    (int) payload.getProperty("copyFromIndex", pad.layerCount() - 1));
                LayerData L = pad.layers[(size_t) copyFromIndex];
                L.layerName.clear();
                L.mute = false;
                L.solo = false;
                const bool clearSample = (bool) payload.getProperty("clearSample", false);
                if (clearSample)
                {
                    L.sampleFileName.clear();
                    L.sampleFilePath.clear();
                    L.sampleMissing = false;
                }

                const int newLayerIndex = pad.layerCount();
                pad.layers.push_back(L);

                if (clearSample)
                {
                    audioProcessor.getFileManager().clearLayer(idx, newLayerIndex);
                }
                else if (L.sampleFilePath.isNotEmpty())
                {
                    juce::File f(L.sampleFilePath);
                    if (f.existsAsFile())
                        audioProcessor.getFileManager().loadFileForPad(idx, newLayerIndex, f);
                }

                audioProcessor.markKitDirty();
                broadcastPadUpdate(idx);
            }
        }
    }
    else if (type == "removeLayer")
    {
        const int idx = getIndex();
        const int layerIdx = (int) payload.getProperty("layerIndex", -1);
        if (idx >= 0 && idx < NUM_PADS)
        {
            auto& pad = audioProcessor.getKit().pads[(size_t) idx];
            if (pad.layerCount() > 1 && layerIdx >= 0 && layerIdx < pad.layerCount())
            {
                pad.layers.erase(pad.layers.begin() + layerIdx);
                for (int li = 0; li < MAX_LAYERS_PER_PAD; ++li)
                    audioProcessor.getFileManager().clearLayer(idx, li);
                for (int li = 0; li < pad.layerCount(); ++li)
                {
                    const auto& layer = pad.layers[(size_t) li];
                    if (layer.sampleFilePath.isNotEmpty())
                    {
                        juce::File f(layer.sampleFilePath);
                        if (f.existsAsFile())
                            audioProcessor.getFileManager().loadFileForPad(idx, li, f);
                    }
                }
                if (layerIdx == 0)
                {
                    // 新しい Layer 0 を flat fields に反映
                    pad.syncFlatFromLayer0();
                }
                audioProcessor.markKitDirty();
                broadcastPadUpdate(idx);
            }
        }
    }
    else if (type == "setLayerVelocityRange")
    {
        const int idx = getIndex();
        const int layerIdx = (int) payload.getProperty("layerIndex", 0);
        if (idx >= 0 && idx < NUM_PADS)
        {
            auto& pad = audioProcessor.getKit().pads[(size_t) idx];
            if (layerIdx >= 0 && layerIdx < pad.layerCount())
            {
                auto& L = pad.layers[(size_t) layerIdx];
                const int lo = juce::jlimit(0, 127, (int) payload.getProperty("min", L.velocityMin));
                const int hi = juce::jlimit(0, 127, (int) payload.getProperty("max", L.velocityMax));
                audioProcessor.setAutomatableLayerParameter(
                    idx, layerIdx, LayerParameterSpecs::Param::VelMin,
                    static_cast<float>(juce::jmin(lo, hi)) / 127.0f, true);
                audioProcessor.setAutomatableLayerParameter(
                    idx, layerIdx, LayerParameterSpecs::Param::VelMax,
                    static_cast<float>(juce::jmax(lo, hi)) / 127.0f, true);
            }
        }
    }
    else if (type == "setLayerMute")
    {
        const int idx = getIndex();
        const int layerIdx = (int) payload.getProperty("layerIndex", 0);
        if (idx >= 0 && idx < NUM_PADS)
        {
            auto& pad = audioProcessor.getKit().pads[(size_t) idx];
            if (layerIdx >= 0 && layerIdx < pad.layerCount())
            {
                pad.layers[(size_t) layerIdx].mute = getBool();
                audioProcessor.markKitDirty();
                broadcastPadUpdate(idx);
            }
        }
    }
    else if (type == "setLayerSolo")
    {
        const int idx = getIndex();
        const int layerIdx = (int) payload.getProperty("layerIndex", 0);
        if (idx >= 0 && idx < NUM_PADS)
        {
            auto& pad = audioProcessor.getKit().pads[(size_t) idx];
            if (layerIdx >= 0 && layerIdx < pad.layerCount())
            {
                const bool v = getBool();
                auto& L = pad.layers[(size_t) layerIdx];
                L.solo = v;
                if (v) L.mute = false;
                audioProcessor.markKitDirty();
                broadcastPadUpdate(idx);
            }
        }
    }
    // ── Layer 個別の音作りパラメータ (L2+ 編集時に UI から送られる) ──────
    // Layer 0 (MAIN) は既存の flat-field 経由ハンドラで処理されるので、こちらは
    // L2+ 用と考えてよい。ただし layerIdx を明示するため layerIdx==0 も受ける。
    else if (type == "setLayerVolume" || type == "setLayerPan" || type == "setLayerPitch"
          || type == "setLayerAttack" || type == "setLayerRelease" || type == "setLayerReverse")
    {
        const int idx = getIndex();
        const int layerIdx = (int) payload.getProperty("layerIndex", 0);
        if (idx >= 0 && idx < NUM_PADS)
        {
            auto& pad = audioProcessor.getKit().pads[(size_t) idx];
            if (layerIdx >= 0 && layerIdx < pad.layerCount())
            {
                auto& L = pad.layers[(size_t) layerIdx];
                if (type == "setLayerVolume")
                    audioProcessor.setAutomatableLayerParameter(idx, layerIdx, LayerParameterSpecs::Param::Volume, getFloat(), true);
                else if (type == "setLayerPan")
                    audioProcessor.setAutomatableLayerParameter(idx, layerIdx, LayerParameterSpecs::Param::Pan, getFloat(), true);
                else if (type == "setLayerPitch")
                    audioProcessor.setAutomatableLayerParameter(idx, layerIdx, LayerParameterSpecs::Param::Pitch, getFloat(), true);
                else if (type == "setLayerAttack")  L.attack  = juce::jlimit(0.0f, 10.0f, getFloat());
                else if (type == "setLayerRelease") L.release = juce::jlimit(0.0f, 10.0f, getFloat());
                else if (type == "setLayerReverse") L.reverse = getBool();
                if (type == "setLayerAttack" || type == "setLayerRelease" || type == "setLayerReverse")
                    audioProcessor.markKitDirty();
            }
        }
    }
    else if (type == "setLayerSampleTrim")
    {
        const int idx = getIndex();
        const int layerIdx = (int) payload.getProperty("layerIndex", 0);
        if (idx >= 0 && idx < NUM_PADS)
        {
            auto& pad = audioProcessor.getKit().pads[(size_t) idx];
            if (layerIdx >= 0 && layerIdx < pad.layerCount())
            {
                auto& L = pad.layers[(size_t) layerIdx];
                const float startPos = (float) (double) payload.getProperty("startPosition", L.startPosition);
                const float endPos   = (float) (double) payload.getProperty("endPosition",   L.endPosition);
                const float fIn      = (float) (double) payload.getProperty("fadeIn",        L.fadeIn);
                const float fOut     = (float) (double) payload.getProperty("fadeOut",       L.fadeOut);
                L.endPosition   = juce::jlimit(0.001f, 1.0f, endPos);
                L.startPosition = juce::jlimit(0.0f, L.endPosition - 0.001f, startPos);
                L.fadeIn        = juce::jlimit(0.0f, 1.0f, fIn);
                L.fadeOut       = juce::jlimit(0.0f, juce::jmax(0.0f, 1.0f - L.fadeIn), fOut);
                audioProcessor.markKitDirty();
            }
        }
    }
    else if (type == "reanalyzeLayer")
    {
        const int idx = getIndex();
        const int layerIdx = (int) payload.getProperty("layerIndex", 0);
        if (idx >= 0 && idx < NUM_PADS)
        {
            auto& pad = audioProcessor.getKit().pads[(size_t) idx];
            if (layerIdx >= 0 && layerIdx < pad.layerCount()
                && audioProcessor.getFileManager().hasSample(idx, layerIdx))
            {
                juce::ScopedReadLock rl(audioProcessor.getFileManager().getReadWriteLock());
                const auto* buf = audioProcessor.getFileManager().getBufferNoLock(idx, layerIdx);
                if (buf != nullptr)
                {
                    const auto r = SmartTrim::analyze(
                        *buf,
                        audioProcessor.getFileManager().getSampleRate(idx, layerIdx));

                    auto& L = pad.layers[(size_t) layerIdx];
                    L.startPosition = r.startPosition;
                    L.endPosition = r.endPosition;
                    L.smartTrim = true;
                    if (layerIdx == 0)
                        pad.syncFlatFromLayer0();

                    audioProcessor.markKitDirty();
                    broadcastPadUpdate(idx);
                }
            }
        }
    }
    else if (type == "setLayerEq")
    {
        const int idx = getIndex();
        const int layerIdx = (int) payload.getProperty("layerIndex", 0);
        if (idx >= 0 && idx < NUM_PADS)
        {
            const auto setIfPresent = [&] (const char* key,
                                           LayerParameterSpecs::Param param)
            {
                if (! payload.hasProperty(key))
                    return;
                const float value = (float) (double) payload.getProperty(key, 0.0);
                audioProcessor.setAutomatableLayerParameter(idx, layerIdx, param, value, true);
            };

            if (payload.hasProperty("bypassed"))
                audioProcessor.setAutomatableLayerParameter(
                    idx, layerIdx, LayerParameterSpecs::Param::EqBypass,
                    ((bool) payload.getProperty("bypassed", false)) ? 1.0f : 0.0f,
                    true);

            if (payload.hasProperty("lowMode"))
                audioProcessor.setAutomatableLayerParameter(
                    idx, layerIdx, LayerParameterSpecs::Param::EqLowMode,
                    ((bool) payload.getProperty("lowMode", false)) ? 1.0f : 0.0f,
                    true);
            if (payload.hasProperty("highMode"))
                audioProcessor.setAutomatableLayerParameter(
                    idx, layerIdx, LayerParameterSpecs::Param::EqHighMode,
                    ((bool) payload.getProperty("highMode", false)) ? 1.0f : 0.0f,
                    true);

            setIfPresent("lowFreq",     LayerParameterSpecs::Param::EqLowFreq);
            setIfPresent("lowGain",     LayerParameterSpecs::Param::EqLowGain);
            setIfPresent("lowQ",        LayerParameterSpecs::Param::EqLowQ);
            setIfPresent("lowMidFreq",  LayerParameterSpecs::Param::EqLowMidFreq);
            setIfPresent("lowMidGain",  LayerParameterSpecs::Param::EqLowMidGain);
            setIfPresent("lowMidQ",     LayerParameterSpecs::Param::EqLowMidQ);
            setIfPresent("highMidFreq", LayerParameterSpecs::Param::EqHighMidFreq);
            setIfPresent("highMidGain", LayerParameterSpecs::Param::EqHighMidGain);
            setIfPresent("highMidQ",    LayerParameterSpecs::Param::EqHighMidQ);
            setIfPresent("highFreq",    LayerParameterSpecs::Param::EqHighFreq);
            setIfPresent("highGain",    LayerParameterSpecs::Param::EqHighGain);
            setIfPresent("highQ",       LayerParameterSpecs::Param::EqHighQ);
        }
    }
    else if (type == "setLayerFxChain")
    {
        const int idx = getIndex();
        const int layerIdx = (int) payload.getProperty("layerIndex", 0);
        if (idx >= 0 && idx < NUM_PADS)
        {
            auto& pad = audioProcessor.getKit().pads[(size_t) idx];
            if (layerIdx >= 0 && layerIdx < pad.layerCount())
            {
                auto& L = pad.layers[(size_t) layerIdx];
                std::vector<LayerFxSlot> next;

                const auto slots = payload.getProperty("slots", juce::var {});
                if (const auto* arr = slots.getArray())
                {
                    next.reserve((size_t) arr->size());
                    for (const auto& item : *arr)
                    {
                        const auto* obj = item.getDynamicObject();
                        if (obj == nullptr)
                            continue;

                        LayerFxSlot slot {};
                        const juce::String typeName = obj->getProperty("type").toString();
                        slot.bypassed = obj->hasProperty("bypassed")
                            ? (bool) obj->getProperty("bypassed")
                            : false;
                        const auto params = obj->hasProperty("params")
                            ? obj->getProperty("params")
                            : juce::var {};
                        const auto* p = params.getDynamicObject();

                        if (typeName == "FILTER")
                        {
                            slot.type = LayerFxType::Filter;
                            if (p != nullptr)
                            {
                                const auto clampSlope = [] (int s) { return s >= 48 ? 48 : s >= 24 ? 24 : 12; };
                                if (p->hasProperty("hpEnabled") || p->hasProperty("lpEnabled"))
                                {
                                    slot.filter.hpEnabled = (bool) p->getProperty("hpEnabled");
                                    slot.filter.lpEnabled = (bool) p->getProperty("lpEnabled");
                                    slot.filter.hpCutoff = juce::jlimit(20.0f, 20000.0f, p->hasProperty("hpCutoff") ? (float) (double) p->getProperty("hpCutoff") : slot.filter.hpCutoff);
                                    slot.filter.lpCutoff = juce::jlimit(20.0f, 20000.0f, p->hasProperty("lpCutoff") ? (float) (double) p->getProperty("lpCutoff") : slot.filter.lpCutoff);
                                    const int sharedSlope = p->hasProperty("slope") ? (int) p->getProperty("slope") : 12;
                                    const float sharedRes = p->hasProperty("resonance") ? (float) (double) p->getProperty("resonance") : 0.7f;
                                    slot.filter.hpSlope = clampSlope(p->hasProperty("hpSlope") ? (int) p->getProperty("hpSlope") : sharedSlope);
                                    slot.filter.lpSlope = clampSlope(p->hasProperty("lpSlope") ? (int) p->getProperty("lpSlope") : sharedSlope);
                                    slot.filter.hpResonance = juce::jlimit(0.2f, 8.0f, p->hasProperty("hpResonance") ? (float) (double) p->getProperty("hpResonance") : sharedRes);
                                    slot.filter.lpResonance = juce::jlimit(0.2f, 8.0f, p->hasProperty("lpResonance") ? (float) (double) p->getProperty("lpResonance") : sharedRes);
                                }
                                else if (p->hasProperty("mode") || p->hasProperty("cutoff"))
                                {
                                    // legacy フォーマット
                                    const int mode = p->getProperty("mode").toString() == "LP" ? 1 : 0;
                                    const float cutoff = juce::jlimit(20.0f, 20000.0f, p->hasProperty("cutoff") ? (float) (double) p->getProperty("cutoff") : 80.0f);
                                    const float res = juce::jlimit(0.2f, 8.0f, p->hasProperty("resonance") ? (float) (double) p->getProperty("resonance") : 0.7f);
                                    slot.filter.hpEnabled = (mode == 0);
                                    slot.filter.lpEnabled = (mode == 1);
                                    slot.filter.hpCutoff = (mode == 0) ? cutoff : 80.0f;
                                    slot.filter.lpCutoff = (mode == 1) ? cutoff : 18000.0f;
                                    slot.filter.hpSlope = slot.filter.lpSlope = 12;
                                    slot.filter.hpResonance = slot.filter.lpResonance = res;
                                }
                            }
                        }
                        else if (typeName == "DRIVE")
                        {
                            slot.type = LayerFxType::Drive;
                            if (p != nullptr)
                            {
                                const auto driveType = p->hasProperty("type") ? p->getProperty("type").toString() : juce::String("SOFT_CLIP");
                                if      (driveType == "HARD_CLIP")  slot.drive.type = 1;
                                else if (driveType == "TUBE")       slot.drive.type = 2;
                                else if (driveType == "TAPE")       slot.drive.type = 3;
                                else if (driveType == "FOLD")       slot.drive.type = 4;
                                else if (driveType == "BIT_CRUSH")  slot.drive.type = 5;
                                else if (driveType == "DOWNSAMPLE") slot.drive.type = 6;
                                else                                slot.drive.type = 0;
                                slot.drive.amount = juce::jlimit(0.0f, 1.0f, p->hasProperty("amount") ? (float) (double) p->getProperty("amount") : slot.drive.amount);
                                slot.drive.tone   = juce::jlimit(0.0f, 1.0f, p->hasProperty("tone")   ? (float) (double) p->getProperty("tone")   : slot.drive.tone);
                                slot.drive.mix    = juce::jlimit(0.0f, 1.0f, p->hasProperty("mix")    ? (float) (double) p->getProperty("mix")    : slot.drive.mix);
                                slot.drive.outputDb = juce::jlimit(-24.0f, 12.0f, p->hasProperty("output") ? (float) (double) p->getProperty("output") : slot.drive.outputDb);
                            }
                        }
                        else if (typeName == "TRANSIENT")
                        {
                            slot.type = LayerFxType::Transient;
                            if (p != nullptr)
                            {
                                slot.transient.attack  = juce::jlimit(-1.0f, 1.0f, p->hasProperty("attack")  ? (float) (double) p->getProperty("attack")  : slot.transient.attack);
                                slot.transient.sustain = juce::jlimit(-1.0f, 1.0f, p->hasProperty("sustain") ? (float) (double) p->getProperty("sustain") : slot.transient.sustain);
                            }
                        }
                        else if (typeName == "COMPRESSOR")
                        {
                            slot.type = LayerFxType::Compressor;
                            if (p != nullptr)
                            {
                                slot.compressor.threshold = juce::jlimit(-48.0f, 0.0f, p->hasProperty("threshold") ? (float) (double) p->getProperty("threshold") : slot.compressor.threshold);
                                slot.compressor.ratio     = juce::jlimit(1.0f, 20.0f, p->hasProperty("ratio")     ? (float) (double) p->getProperty("ratio")     : slot.compressor.ratio);
                                slot.compressor.attack    = juce::jlimit(1.0f, 80.0f, p->hasProperty("attack")    ? (float) (double) p->getProperty("attack")    : slot.compressor.attack);
                                slot.compressor.release   = juce::jlimit(10.0f, 500.0f, p->hasProperty("release") ? (float) (double) p->getProperty("release")   : slot.compressor.release);
                                slot.compressor.mix       = juce::jlimit(0.0f, 1.0f, p->hasProperty("mix")       ? (float) (double) p->getProperty("mix")       : slot.compressor.mix);
                            }
                        }
                        else
                        {
                            slot.type = LayerFxType::Eq;
                            if (p != nullptr)
                            {
                                const auto getMode = [] (const juce::DynamicObject* owner, const char* name, LayerEqEdgeMode fallback)
                                {
                                    if (owner == nullptr || ! owner->hasProperty(name))
                                        return fallback;

                                    const auto raw = owner->getProperty(name);
                                    if (raw.isBool())
                                        return ((bool) raw) ? LayerEqEdgeMode::Cut : LayerEqEdgeMode::Shelf;

                                    const auto text = raw.toString().toLowerCase();
                                    return text == "cut" ? LayerEqEdgeMode::Cut : LayerEqEdgeMode::Shelf;
                                };

                                const auto getBand = [] (const juce::DynamicObject* owner, const char* name, LayerEqBand fallback)
                                {
                                    if (owner == nullptr)
                                        return fallback;
                                    if (const auto* b = owner->getProperty(name).getDynamicObject())
                                    {
                                        fallback.freq = juce::jlimit(20.0f, 20000.0f, b->hasProperty("freq") ? (float) (double) b->getProperty("freq") : fallback.freq);
                                        fallback.gain = juce::jlimit(-18.0f, 18.0f, b->hasProperty("gain") ? (float) (double) b->getProperty("gain") : fallback.gain);
                                        fallback.q    = juce::jlimit(0.2f, 8.0f, b->hasProperty("q") ? (float) (double) b->getProperty("q") : fallback.q);
                                    }
                                    return fallback;
                                };
                                slot.eq.bypassed = p->hasProperty("bypassed") ? (bool) p->getProperty("bypassed") : slot.eq.bypassed;
                                slot.eq.lowMode  = getMode(p, "lowMode",  slot.eq.lowMode);
                                slot.eq.highMode = getMode(p, "highMode", slot.eq.highMode);
                                slot.eq.low      = getBand(p, "low",     slot.eq.low);
                                slot.eq.lowMid   = getBand(p, "lowMid",  slot.eq.lowMid);
                                slot.eq.highMid  = getBand(p, "highMid", slot.eq.highMid);
                                slot.eq.high     = getBand(p, "high",    slot.eq.high);
                            }
                        }

                        next.push_back(slot);
                    }
                }

                L.fxChain = std::move(next);
                audioProcessor.markKitDirty();
            }
        }
    }
    else if (type == "prevKit" || type == "nextKit" || type == "routing")
    {
        // 既存 C++ メニューに将来繋ぐ。とりあえずログのみ。
        juce::Logger::writeToLog("WebView message (todo): " + type);
    }
    else
    {
        juce::Logger::writeToLog("WebView unknown message: " + type);
    }
}
