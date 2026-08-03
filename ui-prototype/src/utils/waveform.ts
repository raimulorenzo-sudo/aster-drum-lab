import type { WaveformChannel } from '../types';

/** waveform を上下対称に描画する旧/SUM互換パス。 */
export function waveformToPath(
  samples: number[],
  width: number,
  height: number,
): { top: string; bottom: string } {
  const mid = height / 2;
  const n = samples.length;
  let top = '';
  let bottom = '';

  for (let i = 0; i < n; i++) {
    const x = (i / (n - 1)) * width;
    const y = mid - Math.abs(samples[i]) * (height * 0.46);
    const yBottom = mid + Math.abs(samples[i]) * (height * 0.46);
    if (i === 0) {
      top += `M ${x.toFixed(2)} ${y.toFixed(2)}`;
      bottom += `M ${x.toFixed(2)} ${yBottom.toFixed(2)}`;
    } else {
      top += ` L ${x.toFixed(2)} ${y.toFixed(2)}`;
      bottom += ` L ${x.toFixed(2)} ${yBottom.toFixed(2)}`;
    }
  }
  return { top, bottom };
}

/**
 * 符号付き min/max を、極値が現れた時間順に1本の連続線へ変換する。
 * 疎な箇所は細い波形、密な箇所は自然に面へ近づいて見える。
 */
export function waveformChannelToStrokePath(
  channel: WaveformChannel,
  width: number,
  centerY: number,
  amplitude: number,
): string {
  const n = Math.min(channel.min.length, channel.max.length);
  if (n < 2) return '';

  let path = '';
  let previousY = centerY;
  for (let i = 0; i < n; i++) {
    const x = (i / (n - 1)) * width;
    const minimum = Math.max(-1, Math.min(1, channel.min[i] ?? 0));
    const maximum = Math.max(-1, Math.min(1, channel.max[i] ?? 0));
    const yMaximum = centerY - maximum * amplitude;
    const yMinimum = centerY - minimum * amplitude;
    const recordedOrder = channel.extremeOrder?.[i];
    const maximumFirst = recordedOrder === 1
      || (recordedOrder == null
        && Math.abs(previousY - yMaximum) <= Math.abs(previousY - yMinimum));
    const firstY = maximumFirst ? yMaximum : yMinimum;
    const secondY = maximumFirst ? yMinimum : yMaximum;

    path += `${i === 0 ? 'M' : ' L'} ${x.toFixed(2)} ${firstY.toFixed(2)}`;
    if (Math.abs(secondY - firstY) > 0.01)
      path += ` L ${x.toFixed(2)} ${secondY.toFixed(2)}`;
    previousY = secondY;
  }
  return path;
}

export function audioBufferToPeaks(buffer: AudioBuffer, length = 600): number[] {
  const numSamples = buffer.length;
  const numChannels = buffer.numberOfChannels;
  if (numSamples <= 0 || numChannels <= 0) return [];

  const points = Math.max(1, Math.min(length, numSamples));
  const peaks = new Array<number>(points);
  let maxPeak = 0;

  for (let i = 0; i < points; i++) {
    const start = Math.floor((i * numSamples) / points);
    const end = Math.max(start + 1, Math.floor(((i + 1) * numSamples) / points));
    let peak = 0;

    for (let channel = 0; channel < numChannels; channel++) {
      const data = buffer.getChannelData(channel);
      for (let sample = start; sample < end; sample++) {
        peak = Math.max(peak, Math.abs(data[sample] ?? 0));
      }
    }

    peaks[i] = peak;
    maxPeak = Math.max(maxPeak, peak);
  }

  if (maxPeak <= 0) return peaks.map(() => 0);
  return peaks.map(peak => Math.max(0, Math.min(1, peak / maxPeak)));
}

/** Browser preview用。最大2chを同一ピークで正規化した符号付き波形へ変換する。 */
export function audioBufferToWaveformChannels(
  buffer: AudioBuffer,
  length = 2000,
): WaveformChannel[] {
  const numSamples = buffer.length;
  const channelCount = Math.min(2, buffer.numberOfChannels);
  if (numSamples <= 0 || channelCount <= 0) return [];

  const points = Math.max(1, Math.min(length, numSamples));
  const channels = Array.from({ length: channelCount }, () => ({
    min: new Array<number>(points).fill(0),
    max: new Array<number>(points).fill(0),
    extremeOrder: new Array<number>(points).fill(0),
  }));
  let sharedPeak = 0;

  for (let point = 0; point < points; point++) {
    const start = Math.floor((point * numSamples) / points);
    const end = Math.max(start + 1, Math.floor(((point + 1) * numSamples) / points));
    for (let channel = 0; channel < channelCount; channel++) {
      const data = buffer.getChannelData(channel);
      let min = data[start] ?? 0;
      let max = min;
      let minIndex = start;
      let maxIndex = start;
      for (let sample = start + 1; sample < end; sample++) {
        const value = data[sample] ?? 0;
        if (value < min) {
          min = value;
          minIndex = sample;
        }
        if (value > max) {
          max = value;
          maxIndex = sample;
        }
      }
      channels[channel].min[point] = min;
      channels[channel].max[point] = max;
      channels[channel].extremeOrder[point] = maxIndex < minIndex ? 1 : 0;
      sharedPeak = Math.max(sharedPeak, Math.abs(min), Math.abs(max));
    }
  }

  if (sharedPeak <= 0) return channels;
  return channels.map(channel => ({
    min: channel.min.map(value => Math.max(-1, Math.min(1, value / sharedPeak))),
    max: channel.max.map(value => Math.max(-1, Math.min(1, value / sharedPeak))),
    extremeOrder: channel.extremeOrder,
  }));
}

export function waveformToBars(peaks: number[], length = 32): number[] {
  if (peaks.length === 0) return [];

  const bars = new Array<number>(length);
  for (let i = 0; i < length; i++) {
    const start = Math.floor((i * peaks.length) / length);
    const end = Math.max(start + 1, Math.floor(((i + 1) * peaks.length) / length));
    let peak = 0;

    for (let n = start; n < end; n++)
      peak = Math.max(peak, peaks[n] ?? 0);

    bars[i] = 8 + peak * 82;
  }

  return bars;
}
