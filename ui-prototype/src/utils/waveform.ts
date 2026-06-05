/**
 * SVG path 文字列を生成する。waveform を「上下対称」に描画した
 * 縦長エンベロープ風のシルエット。
 */
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
