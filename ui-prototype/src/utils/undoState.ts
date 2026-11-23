import type { LayerParams, PadParams } from '../types';
import { NEUTRAL_EQ } from '../types';

// Cached native snapshots and decoded waveform data do not represent user edits.
const cache = new WeakMap<PadParams, unknown>();
const cachedKeys = new Set(['nativeUndoState', 'waveformPeaks', 'waveformChannels', 'sampleLengthMs']);
function normalizeLayer(layer: LayerParams) {
  return {
    ...layer,
    sampleMissing: Boolean(layer.sampleMissing), layerName: layer.layerName ?? '',
    activeSampleStockIndex: layer.activeSampleStockIndex ?? 0,
    roundRobin: layer.roundRobin ?? false, polarityInvert: layer.polarityInvert ?? false,
    eq: layer.eq ?? NEUTRAL_EQ, fxChain: layer.fxChain ?? [],
    sampleStock: (layer.sampleStock ?? (layer.sampleFilePath ? [{
      sampleFileName: layer.sampleFileName, sampleFilePath: layer.sampleFilePath,
      sampleMissing: layer.sampleMissing,
    }] : [])).map(sample => ({ ...sample, sampleMissing: Boolean(sample.sampleMissing) })),
  };
}
function normalized(pad: PadParams): unknown {
  if (cache.has(pad)) return cache.get(pad);
  const value: Record<string, unknown> = { ...pad, sampleMissing: Boolean(pad.sampleMissing) };
  if (pad.layers?.length) {
    value.layers = pad.layers.map(normalizeLayer);
    // These are legacy MAIN mirrors; the complete banks live on the layers.
    for (const key of ['sampleStock', 'activeSampleStockIndex', 'roundRobin', 'eq', 'fxChain', 'polarityInvert']) delete value[key];
  }
  delete value.selectedLayerIndex;
  const result: unknown = JSON.parse(JSON.stringify(value, (key, entry) => cachedKeys.has(key) ? undefined : entry));
  cache.set(pad, result);
  return result;
}
function equal(a: unknown, b: unknown): boolean {
  if (a === b) return true;
  // C++ stores many controls as float32. Its echo must not create another Undo.
  if (typeof a === 'number' && typeof b === 'number')
    return Math.abs(a - b) <= 2e-7 * Math.max(1, Math.abs(a), Math.abs(b));
  if (!a || !b || typeof a !== 'object' || typeof b !== 'object') return false;
  if (Array.isArray(a) !== Array.isArray(b)) return false;
  const aa = a as Record<string, unknown>, bb = b as Record<string, unknown>;
  const keys = Object.keys(aa);
  return keys.length === Object.keys(bb).length && keys.every(key => key in bb && equal(aa[key], bb[key]));
}
export function undoPadsEqual(a: PadParams[], b: PadParams[]): boolean {
  return a.length === b.length && a.every((pad, i) => pad === b[i] || equal(normalized(pad), normalized(b[i])));
}
