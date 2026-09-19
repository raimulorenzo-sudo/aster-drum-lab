#include "AudioFileManager.h"

AudioFileManager::AudioFileManager()
{
    // WAV / AIFF / その他の基本フォーマットを登録
    formatManager.registerBasicFormats();

    // サンプルレートの初期値
    for (auto& padRates : sampleRates)
        for (auto& layerRates : padRates)
            layerRates.fill(44100.0);
}

// ─────────────────────────────────────────────────────────────────────────────
// サンプルファイルの読み込み（メッセージスレッドから呼ぶ）
// ─────────────────────────────────────────────────────────────────────────────
bool AudioFileManager::loadFileForPad(int padIndex, int layerIndex, const juce::File& file)
{
    if (! inRange(padIndex, layerIndex)) return false;
    const int variationIndex = activeVariationIndices[static_cast<size_t>(padIndex)]
                                                     [static_cast<size_t>(layerIndex)];
    return loadFileForPadVariation(padIndex, layerIndex, variationIndex, file);
}

bool AudioFileManager::loadFileForPadVariation(int padIndex, int layerIndex,
                                               int variationIndex,
                                               const juce::File& file)
{
    if (! inRange(padIndex, layerIndex) || ! variationInRange(variationIndex)) return false;

    std::unique_ptr<juce::AudioFormatReader> reader(
        formatManager.createReaderFor(file));

    if (reader == nullptr) return false;

    const int    numSamples  = static_cast<int>(reader->lengthInSamples);
    const int    numChannels = static_cast<int>(reader->numChannels);
    const double sr          = reader->sampleRate;

    if (numSamples <= 0) return false;

    const int storeCh = std::min(numChannels, 2);
    auto newBuffer = std::make_unique<juce::AudioBuffer<float>>(storeCh, numSamples);

    reader->read(newBuffer.get(),
                 0,
                 numSamples,
                 0,
                 true,
                 numChannels >= 2);

    {
        const auto p = static_cast<size_t>(padIndex);
        const auto l = static_cast<size_t>(layerIndex);
        const auto v = static_cast<size_t>(variationIndex);
        juce::ScopedWriteLock wl(rwLock);
        buffers[p][l][v]     = std::move(newBuffer);
        sampleRates[p][l][v] = sr;
    }

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// レイヤー個別クリア
// ─────────────────────────────────────────────────────────────────────────────
void AudioFileManager::clearLayer(int padIndex, int layerIndex)
{
    if (! inRange(padIndex, layerIndex)) return;

    const auto p = static_cast<size_t>(padIndex);
    const auto l = static_cast<size_t>(layerIndex);
    juce::ScopedWriteLock wl(rwLock);
    for (int v = 0; v < MAX_SAMPLE_STOCK_PER_LAYER; ++v)
    {
        buffers[p][l][static_cast<size_t>(v)].reset();
        sampleRates[p][l][static_cast<size_t>(v)] = 44100.0;
    }
    activeVariationIndices[p][l] = 0;
}

void AudioFileManager::clearSampleVariation(int padIndex, int layerIndex, int variationIndex)
{
    if (! inRange(padIndex, layerIndex) || ! variationInRange(variationIndex)) return;
    const auto p = static_cast<size_t>(padIndex);
    const auto l = static_cast<size_t>(layerIndex);
    const auto v = static_cast<size_t>(variationIndex);
    juce::ScopedWriteLock wl(rwLock);
    buffers[p][l][v].reset();
    sampleRates[p][l][v] = 44100.0;
}

void AudioFileManager::removeSampleVariation(int padIndex, int layerIndex, int variationIndex)
{
    if (! inRange(padIndex, layerIndex) || ! variationInRange(variationIndex)) return;
    const auto p = static_cast<size_t>(padIndex);
    const auto l = static_cast<size_t>(layerIndex);
    juce::ScopedWriteLock wl(rwLock);
    for (int v = variationIndex; v + 1 < MAX_SAMPLE_STOCK_PER_LAYER; ++v)
    {
        buffers[p][l][static_cast<size_t>(v)] =
            std::move(buffers[p][l][static_cast<size_t>(v + 1)]);
        sampleRates[p][l][static_cast<size_t>(v)] =
            sampleRates[p][l][static_cast<size_t>(v + 1)];
    }
    buffers[p][l].back().reset();
    sampleRates[p][l].back() = 44100.0;
}

void AudioFileManager::setActiveVariationIndex(int padIndex, int layerIndex, int variationIndex)
{
    if (! inRange(padIndex, layerIndex)) return;
    const auto p = static_cast<size_t>(padIndex);
    const auto l = static_cast<size_t>(layerIndex);
    juce::ScopedWriteLock wl(rwLock);
    activeVariationIndices[p][l] = juce::jlimit(0, MAX_SAMPLE_STOCK_PER_LAYER - 1,
                                                variationIndex);
}

// ─────────────────────────────────────────────────────────────────────────────
// パッド全 Layer クリア
// ─────────────────────────────────────────────────────────────────────────────
void AudioFileManager::clearPad(int padIndex)
{
    if (padIndex < 0 || padIndex >= NUM_PADS) return;

    const auto p = static_cast<size_t>(padIndex);
    juce::ScopedWriteLock wl(rwLock);
    for (int l = 0; l < MAX_LAYERS_PER_PAD; ++l)
    {
        for (int v = 0; v < MAX_SAMPLE_STOCK_PER_LAYER; ++v)
        {
            buffers[p][(size_t) l][(size_t) v].reset();
            sampleRates[p][(size_t) l][(size_t) v] = 44100.0;
        }
        activeVariationIndices[p][(size_t) l] = 0;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 2 パッドの全 Layer を交換
// ─────────────────────────────────────────────────────────────────────────────
void AudioFileManager::swapPads(int a, int b)
{
    if (a < 0 || a >= NUM_PADS) return;
    if (b < 0 || b >= NUM_PADS) return;
    if (a == b)                 return;

    const auto ai = static_cast<size_t>(a);
    const auto bi = static_cast<size_t>(b);

    juce::ScopedWriteLock wl(rwLock);
    for (int l = 0; l < MAX_LAYERS_PER_PAD; ++l)
    {
        std::swap(buffers[ai][(size_t) l],     buffers[bi][(size_t) l]);
        std::swap(sampleRates[ai][(size_t) l], sampleRates[bi][(size_t) l]);
        std::swap(activeVariationIndices[ai][(size_t) l],
                  activeVariationIndices[bi][(size_t) l]);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// バッファ取得（呼び出し元が読み取りロックを保持している前提）
// ─────────────────────────────────────────────────────────────────────────────
const juce::AudioBuffer<float>* AudioFileManager::getBufferNoLock(int padIndex, int layerIndex) const noexcept
{
    if (! inRange(padIndex, layerIndex)) return nullptr;
    const auto p = static_cast<size_t>(padIndex);
    const auto l = static_cast<size_t>(layerIndex);
    return getBufferNoLock(padIndex, layerIndex, activeVariationIndices[p][l]);
}

const juce::AudioBuffer<float>* AudioFileManager::getBufferNoLock(
    int padIndex, int layerIndex, int variationIndex) const noexcept
{
    if (! inRange(padIndex, layerIndex) || ! variationInRange(variationIndex)) return nullptr;
    return buffers[static_cast<size_t>(padIndex)][static_cast<size_t>(layerIndex)]
                  [static_cast<size_t>(variationIndex)].get();
}

double AudioFileManager::getSampleRate(int padIndex, int layerIndex) const noexcept
{
    if (! inRange(padIndex, layerIndex)) return 44100.0;
    const auto p = static_cast<size_t>(padIndex);
    const auto l = static_cast<size_t>(layerIndex);
    return getSampleRate(padIndex, layerIndex, activeVariationIndices[p][l]);
}

double AudioFileManager::getSampleRate(int padIndex, int layerIndex,
                                       int variationIndex) const noexcept
{
    if (! inRange(padIndex, layerIndex) || ! variationInRange(variationIndex)) return 44100.0;
    return sampleRates[static_cast<size_t>(padIndex)][static_cast<size_t>(layerIndex)]
                      [static_cast<size_t>(variationIndex)];
}

double AudioFileManager::getSampleLengthMs(int padIndex, int layerIndex) const noexcept
{
    if (! inRange(padIndex, layerIndex)) return 0.0;

    const auto p = static_cast<size_t>(padIndex);
    const auto l = static_cast<size_t>(layerIndex);
    juce::ScopedReadLock rl(rwLock);
    const int variationIndex = activeVariationIndices[p][l];
    const auto* buffer = buffers[p][l][static_cast<size_t>(variationIndex)].get();
    const double sr = sampleRates[p][l][static_cast<size_t>(variationIndex)];

    if (buffer == nullptr || buffer->getNumSamples() <= 0 || sr <= 0.0)
        return 0.0;

    return (static_cast<double>(buffer->getNumSamples()) / sr) * 1000.0;
}

std::uint64_t AudioFileManager::getTotalSampleBytes() const noexcept
{
    juce::ScopedReadLock rl(rwLock);

    std::uint64_t total = 0;
    for (const auto& padArr : buffers)
        for (const auto& layerArr : padArr)
            for (const auto& buffer : layerArr)
            {
                if (buffer == nullptr) continue;
                total += static_cast<std::uint64_t>(buffer->getNumChannels())
                       * static_cast<std::uint64_t>(buffer->getNumSamples())
                       * static_cast<std::uint64_t>(sizeof(float));
            }

    return total;
}

bool AudioFileManager::hasSample(int padIndex, int layerIndex) const noexcept
{
    if (! inRange(padIndex, layerIndex)) return false;
    const auto p = static_cast<size_t>(padIndex);
    const auto l = static_cast<size_t>(layerIndex);
    return hasSample(padIndex, layerIndex, activeVariationIndices[p][l]);
}

bool AudioFileManager::hasSample(int padIndex, int layerIndex, int variationIndex) const noexcept
{
    if (! inRange(padIndex, layerIndex) || ! variationInRange(variationIndex)) return false;
    // NOTE: ここはロックなし。UI 表示確認用なのでレース条件は許容範囲
    return buffers[static_cast<size_t>(padIndex)][static_cast<size_t>(layerIndex)]
                  [static_cast<size_t>(variationIndex)] != nullptr;
}
