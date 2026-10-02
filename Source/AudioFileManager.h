#pragma once
#include <JuceHeader.h>
#include "KitData.h"   // NUM_PADS の定義
#include "PadData.h"   // MAX_LAYERS_PER_PAD
#include <array>
#include <memory>

// ─────────────────────────────────────────────────────────────────────────────
// AudioFileManager  ─  各 (Pad, Layer) のオーディオデータを管理するクラス
//
// 内部ストレージ:
//   buffers[padIndex][layerIndex][variationIndex]     → AudioBuffer
//   sampleRates[padIndex][layerIndex][variationIndex] → 元 WAV のサンプルレート
//
// 後方互換:
//   1 引数 API (padIndex のみ) は内部で layerIndex=0 にディスパッチする。
//
// スレッド安全設計:
//   loadFileForPad(...)  → メッセージスレッドから呼ぶ → 書き込みロック取得
//   getBufferNoLock(...) → オーディオスレッドから呼ぶ → 呼び出し元がロック保持
//   getReadWriteLock()   → PluginProcessor が processBlock の前後で取得
// ─────────────────────────────────────────────────────────────────────────────
class AudioFileManager
{
public:
    AudioFileManager();

    // ── サンプル読み込み ────────────────────────────────────────────────────
    bool loadFileForPad(int padIndex, int layerIndex, const juce::File& file);
    bool loadFileForPadVariation(int padIndex, int layerIndex, int variationIndex,
                                 const juce::File& file);
    bool loadFileForPad(int padIndex, const juce::File& file)  // Layer 0 互換 API
    {
        return loadFileForPad(padIndex, 0, file);
    }

    bool installDecodedVariation(int padIndex, int layerIndex, int variationIndex,
                                 std::unique_ptr<juce::AudioBuffer<float>> audio, double sampleRate);

    // ── レイヤー個別クリア ─────────────────────────────────────────────────
    void clearLayer(int padIndex, int layerIndex);
    void clearSampleVariation(int padIndex, int layerIndex, int variationIndex);
    void removeSampleVariation(int padIndex, int layerIndex, int variationIndex);
    void setActiveVariationIndex(int padIndex, int layerIndex, int variationIndex);

    // ── パッド全 Layer をクリア（"Clear Sample" / "Empty Kit reset" 用） ────
    void clearPad(int padIndex);

    // ── 2 パッドのオーディオバッファを交換（全 Layer 一括） ───────────────
    void swapPads(int a, int b);

    // ── オーディオデータ取得（ロック保持中に呼ぶ） ──────────────────────────
    const juce::AudioBuffer<float>* getBufferNoLock(int padIndex, int layerIndex) const noexcept;
    const juce::AudioBuffer<float>* getBufferNoLock(int padIndex, int layerIndex,
                                                     int variationIndex) const noexcept;
    const juce::AudioBuffer<float>* getBufferNoLock(int padIndex) const noexcept
    {
        return getBufferNoLock(padIndex, 0);
    }

    // ── サンプルレート / 長さ ────────────────────────────────────────────────
    double getSampleRate(int padIndex, int layerIndex) const noexcept;
    double getSampleRate(int padIndex, int layerIndex, int variationIndex) const noexcept;
    double getSampleRate(int padIndex) const noexcept { return getSampleRate(padIndex, 0); }

    double getSampleLengthMs(int padIndex, int layerIndex) const noexcept;
    double getSampleLengthMs(int padIndex) const noexcept { return getSampleLengthMs(padIndex, 0); }

    std::uint64_t getTotalSampleBytes() const noexcept;

    // ── ロックオブジェクトへのアクセス ──────────────────────────────────────
    juce::ReadWriteLock& getReadWriteLock() noexcept { return rwLock; }

    // ── サンプル読み込み済みか確認 ──────────────────────────────────────────
    bool hasSample(int padIndex, int layerIndex) const noexcept;
    bool hasSample(int padIndex, int layerIndex, int variationIndex) const noexcept;
    bool hasSample(int padIndex) const noexcept { return hasSample(padIndex, 0); }

private:
    juce::AudioFormatManager formatManager;

    using VariationBuffers = std::array<std::unique_ptr<juce::AudioBuffer<float>>,
                                        MAX_SAMPLE_STOCK_PER_LAYER>;
    using VariationRates = std::array<double, MAX_SAMPLE_STOCK_PER_LAYER>;

    // (Pad, Layer, Sample Variation) ごとのオーディオバッファ
    std::array<std::array<VariationBuffers, MAX_LAYERS_PER_PAD>, NUM_PADS> buffers;

    // (Pad, Layer) ごとのサンプルレート
    std::array<std::array<VariationRates, MAX_LAYERS_PER_PAD>, NUM_PADS> sampleRates;
    std::array<std::array<int, MAX_LAYERS_PER_PAD>, NUM_PADS> activeVariationIndices {};

    // ReadWriteLock: 読み取り（オーディオスレッド）と書き込み（UIスレッド）を分離
    mutable juce::ReadWriteLock rwLock;

    static bool inRange(int padIndex, int layerIndex) noexcept
    {
        return padIndex >= 0 && padIndex < NUM_PADS
            && layerIndex >= 0 && layerIndex < MAX_LAYERS_PER_PAD;
    }

    static bool variationInRange(int variationIndex) noexcept
    {
        return variationIndex >= 0 && variationIndex < MAX_SAMPLE_STOCK_PER_LAYER;
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioFileManager)
};
