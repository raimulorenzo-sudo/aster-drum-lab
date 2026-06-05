#include "AudioFileManager.h"

AudioFileManager::AudioFileManager()
{
    // WAV / AIFF / その他の基本フォーマットを登録
    formatManager.registerBasicFormats();

    // サンプルレートの初期値
    for (auto& padRates : sampleRates)
        padRates.fill(44100.0);
}

// ─────────────────────────────────────────────────────────────────────────────
// サンプルファイルの読み込み（メッセージスレッドから呼ぶ）
// ─────────────────────────────────────────────────────────────────────────────
bool AudioFileManager::loadFileForPad(int padIndex, int layerIndex, const juce::File& file)
{
    if (! inRange(padIndex, layerIndex)) return false;

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
        juce::ScopedWriteLock wl(rwLock);
        buffers[p][l]     = std::move(newBuffer);
        sampleRates[p][l] = sr;
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
    buffers[p][l].reset();
    sampleRates[p][l] = 44100.0;
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
        buffers[p][(size_t) l].reset();
        sampleRates[p][(size_t) l] = 44100.0;
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
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// バッファ取得（呼び出し元が読み取りロックを保持している前提）
// ─────────────────────────────────────────────────────────────────────────────
const juce::AudioBuffer<float>* AudioFileManager::getBufferNoLock(int padIndex, int layerIndex) const noexcept
{
    if (! inRange(padIndex, layerIndex)) return nullptr;
    return buffers[static_cast<size_t>(padIndex)][static_cast<size_t>(layerIndex)].get();
}

double AudioFileManager::getSampleRate(int padIndex, int layerIndex) const noexcept
{
    if (! inRange(padIndex, layerIndex)) return 44100.0;
    return sampleRates[static_cast<size_t>(padIndex)][static_cast<size_t>(layerIndex)];
}

double AudioFileManager::getSampleLengthMs(int padIndex, int layerIndex) const noexcept
{
    if (! inRange(padIndex, layerIndex)) return 0.0;

    const auto p = static_cast<size_t>(padIndex);
    const auto l = static_cast<size_t>(layerIndex);
    juce::ScopedReadLock rl(rwLock);
    const auto* buffer = buffers[p][l].get();
    const double sr = sampleRates[p][l];

    if (buffer == nullptr || buffer->getNumSamples() <= 0 || sr <= 0.0)
        return 0.0;

    return (static_cast<double>(buffer->getNumSamples()) / sr) * 1000.0;
}

std::uint64_t AudioFileManager::getTotalSampleBytes() const noexcept
{
    juce::ScopedReadLock rl(rwLock);

    std::uint64_t total = 0;
    for (const auto& padArr : buffers)
        for (const auto& buffer : padArr)
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
    // NOTE: ここはロックなし。UI 表示確認用なのでレース条件は許容範囲
    return buffers[static_cast<size_t>(padIndex)][static_cast<size_t>(layerIndex)] != nullptr;
}
