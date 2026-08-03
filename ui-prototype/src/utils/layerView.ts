import type { LayerParams, PadParams } from '../types';

/** Pad あたりの最大 Layer 数。UI / データの両方でこの上限を守る。 */
export const MAX_LAYERS_PER_PAD = 8;

/**
 * UI が「Layer N の値で Pad を編集している」ように見せるためのヘルパー。
 *
 * 信号フロー（types.ts に対応）:
 *   Layer Vol/Pan/Pitch/Fine → Pad Vol/Pan/Pitch/Fine → Output Routing
 *
 * 既存の WaveformEditor / PadControlSections は flat な PadParams を読む前提で
 * 書かれているので、選択中 Layer の値を flat フィールドに「上書き」した
 * 仮想 Pad を composePadView() で合成して渡す。
 * 編集側は routeLayerPatch() を通じて Layer-level なキーを layers[idx] に転送する。
 *
 * Layer 0 は後方互換のため flat フィールドにもミラーしておく（JUCE 側 v6 未満や
 * `layers` が undefined のまま動いている経路と整合させる）。
 */

/** Layer-level なキー（PadParams と LayerParams の両方にあるフィールドのみ） */
const LAYER_LEVEL_KEYS = [
  'sampleFileName',
  'sampleFilePath',
  'sampleMissing',
  'volume',
  'pan',
  'pitch',
  'fine',
  'attack',
  'release',
  'startMs',
  'endMs',
  'fadeInMs',
  'fadeOutMs',
  'sampleLengthMs',
  'waveformPeaks',
  'reverse',
  'smartTrim',
  // Layer 単位で持つ EQ / FX / 極性反転 (PadParams 側にも optional として写しているのでキャストで通す)
] as const satisfies readonly (keyof PadParams & keyof LayerParams)[];

/** Layer 専用キー (PadParams flat には存在しない)。layers[idx] のみに書く。 */
const LAYER_ONLY_KEYS = ['eq', 'fxChain', 'polarityInvert'] as const;
const LAYER_ONLY_KEY_SET: ReadonlySet<string> = new Set<string>(LAYER_ONLY_KEYS);

type LayerLevelKey = (typeof LAYER_LEVEL_KEYS)[number];

const LAYER_LEVEL_KEY_SET: ReadonlySet<string> = new Set<string>(LAYER_LEVEL_KEYS);

export function isLayerLevelKey(key: string): key is LayerLevelKey {
  return LAYER_LEVEL_KEY_SET.has(key);
}

/** 選択中 Layer のインデックス。範囲外でも 0 に丸める。 */
export function selectedLayerIndexOf(pad: PadParams): number {
  const layerCount = pad.layers?.length ?? 1;
  const raw = pad.selectedLayerIndex ?? 0;
  if (raw < 0) return 0;
  if (raw >= layerCount) return Math.max(0, layerCount - 1);
  return raw;
}

/** Layer 個数。layers 未定義なら flat fields を Layer 0 として 1 個扱い。 */
export function layerCountOf(pad: PadParams): number {
  return pad.layers?.length ?? 1;
}

/**
 * 選択中 Layer の値で flat フィールドを上書きした「ビュー Pad」を返す。
 *
 * - `pad.layers` が無い / 空 → 元の pad をそのまま返す（Layer 0 = flat fields の経路）。
 * - 選択中 Layer の値が `undefined` のフィールドは flat fields をフォールバックに使う
 *   （古いデータでサンプル長やピークが Layer 側に無いケースを想定）。
 */
export function composePadView(pad: PadParams): PadParams {
  const layers = pad.layers;
  if (!layers || layers.length === 0) return pad;

  const idx = selectedLayerIndexOf(pad);
  const layer = layers[idx];
  if (!layer) return pad;

  const view: PadParams = { ...pad };
  for (const key of LAYER_LEVEL_KEYS) {
    const v = layer[key];
    if (v !== undefined) {
      (view as unknown as Record<string, unknown>)[key] = v;
    }
  }
  return view;
}

/**
 * UI からの patch を「Layer-level キーは layers[idx] に、Pad-level キーは flat に」
 * 振り分けた `Partial<PadParams>` を返す。
 *
 * - layers が未定義のときは layers を [Layer0] として初期化したうえで適用する。
 * - 既定で Layer 0 への変更は flat フィールドにもミラーする（後方互換）。
 *   ミラーしたくない場合は呼び出し側で flat 側のキーを削れば良いが、
 *   いまの WaveformEditor / PadControlSections の挙動と整合させるなら ON のままで OK。
 */
export function routeLayerPatch(
  pad: PadParams,
  patch: Partial<PadParams>,
): Partial<PadParams> {
  const layerPatch: Partial<LayerParams> = {};
  const padPatch: Partial<PadParams> = {};
  let touchedLayer = false;

  for (const [key, value] of Object.entries(patch)) {
    if (isLayerLevelKey(key) || LAYER_ONLY_KEY_SET.has(key)) {
      (layerPatch as Record<string, unknown>)[key] = value;
      touchedLayer = true;
    } else {
      (padPatch as Record<string, unknown>)[key] = value;
    }
  }

  if (!touchedLayer) return padPatch;

  const idx = selectedLayerIndexOf(pad);
  const baseLayers = pad.layers ?? [seedLayerFromFlat(pad)];
  const nextLayers = baseLayers.map((l, i) =>
    i === idx ? { ...l, ...layerPatch } : l,
  );

  // Layer 0 は flat フィールドにもミラー（後方互換 / JUCE v6 未満との整合）。
  if (idx === 0) {
    for (const key of Object.keys(layerPatch)) {
      const v = (layerPatch as Record<string, unknown>)[key];
      (padPatch as Record<string, unknown>)[key] = v;
    }
  }

  return { ...padPatch, layers: nextLayers };
}

/** flat な PadParams から Layer 0 相当の LayerParams を組み立てる。 */
function seedLayerFromFlat(pad: PadParams): LayerParams {
  return {
    sampleFileName: pad.sampleFileName,
    sampleFilePath: pad.sampleFilePath,
    sampleMissing: pad.sampleMissing,
    volume: pad.volume,
    pan: pad.pan,
    pitch: pad.pitch,
    fine: pad.fine,
    attack: pad.attack,
    release: pad.release,
    startMs: pad.startMs,
    endMs: pad.endMs,
    fadeInMs: pad.fadeInMs,
    fadeOutMs: pad.fadeOutMs,
    sampleLengthMs: pad.sampleLengthMs,
    waveformPeaks: pad.waveformPeaks,
    reverse: pad.reverse,
    smartTrim: pad.smartTrim,
    mute: false,
    solo: false,
    velocityMin: 0,
    velocityMax: 127,
    eq: pad.eq,
  };
}

/** UI 上の Layer 表示名（layerName が空ならサンプル名、それも無ければ "Layer N"）。 */
export function layerDisplayName(layer: LayerParams | undefined, index: number): string {
  if (layer?.layerName && layer.layerName.trim().length > 0) return layer.layerName;
  if (layer?.sampleFileName && layer.sampleFileName.trim().length > 0) return layer.sampleFileName;
  return `Layer ${index + 1}`;
}

/** flat な PadParams から Layer 0 相当の LayerParams を組み立てる（公開版）。 */
export function layerFromFlat(pad: PadParams): LayerParams {
  return seedLayerFromFlat(pad);
}

/** 現在の `layers` を [Layer0] 互換で取得する（無ければ flat から組み立て）。 */
export function ensureLayers(pad: PadParams): LayerParams[] {
  const layers = pad.layers;
  if (!layers || layers.length === 0) return [seedLayerFromFlat(pad)];

  // JUCE の native integration が環境によって穴あき配列を返しても、
  // `.length` と実際に描画できる行数が食い違わないようにする。
  // 正常な配列は参照をそのまま返し、通常時の React 再描画は増やさない。
  let needsRepair = false;
  for (let i = 0; i < layers.length; i += 1) {
    if (!(i in layers) || !layers[i]) {
      needsRepair = true;
      break;
    }
  }
  if (!needsRepair) return layers;

  const flatLayer = seedLayerFromFlat(pad);
  return Array.from({ length: layers.length }, (_, index) => {
    const layer = layers[index];
    if (layer) return layer;
    if (index === 0) return flatLayer;

    return {
      ...flatLayer,
      sampleFileName: '',
      sampleFilePath: '',
      sampleMissing: false,
      layerName: `Layer ${index + 1}`,
      waveformPeaks: undefined,
    };
  });
}

/**
 * Pad に新しい Layer を追加する patch を返す。
 * - 最大 MAX_LAYERS_PER_PAD まで。超過時は null。
 * - 新規 Layer は現在選択中の Layer をコピー（典型ワークフロー: 微調整して重ねる）。
 *   ただし sampleFile* は空にする — 何のサンプルも入っていない新規 Layer として扱う。
 * - selectedLayerIndex は追加した末尾に移す。
 */
export function patchAddLayer(pad: PadParams): Partial<PadParams> | null {
  const layers = ensureLayers(pad);
  if (layers.length >= MAX_LAYERS_PER_PAD) return null;
  const src = layers[selectedLayerIndexOf(pad)] ?? layers[0];
  const next: LayerParams = {
    ...src,
    sampleFileName: '',
    sampleFilePath: '',
    sampleMissing: undefined,
    layerName: undefined,
    waveformPeaks: undefined,
    sampleLengthMs: undefined,
    mute: false,
    solo: false,
    // 新規 Layer は EQ / FX Chain / Polarity をリセット (上位の挙動と一貫させる)
    eq: undefined,
    fxChain: undefined,
    polarityInvert: false,
  };
  const nextLayers = [...layers, next];
  return { layers: nextLayers, selectedLayerIndex: nextLayers.length - 1 };
}

/**
 * 指定 index の Layer を削除する patch を返す。
 * - 最後の 1 個は削除不可（null）。
 * - 削除後、selectedLayerIndex は範囲内に丸める。
 * - Layer 0 を削除した場合は flat フィールドも新 Layer 0 にミラーする（後方互換）。
 */
export function patchRemoveLayer(pad: PadParams, removeIndex: number): Partial<PadParams> | null {
  const layers = ensureLayers(pad);
  if (layers.length <= 1) return null;
  if (removeIndex < 0 || removeIndex >= layers.length) return null;

  const nextLayers = layers.filter((_, i) => i !== removeIndex);
  const currentIdx = selectedLayerIndexOf(pad);
  let nextIdx = currentIdx;
  if (removeIndex < currentIdx) nextIdx = currentIdx - 1;
  if (nextIdx >= nextLayers.length) nextIdx = nextLayers.length - 1;
  if (nextIdx < 0) nextIdx = 0;

  const patch: Partial<PadParams> = {
    layers: nextLayers,
    selectedLayerIndex: nextIdx,
  };

  // Layer 0 が変わったら flat フィールドにミラー
  if (removeIndex === 0) {
    const newLayer0 = nextLayers[0];
    patch.sampleFileName = newLayer0.sampleFileName;
    patch.sampleFilePath = newLayer0.sampleFilePath;
    patch.sampleMissing = newLayer0.sampleMissing;
    patch.volume = newLayer0.volume;
    patch.pan = newLayer0.pan;
    patch.pitch = newLayer0.pitch;
    patch.fine = newLayer0.fine;
    patch.attack = newLayer0.attack;
    patch.release = newLayer0.release;
    patch.startMs = newLayer0.startMs;
    patch.endMs = newLayer0.endMs;
    patch.fadeInMs = newLayer0.fadeInMs;
    patch.fadeOutMs = newLayer0.fadeOutMs;
    patch.sampleLengthMs = newLayer0.sampleLengthMs;
    patch.waveformPeaks = newLayer0.waveformPeaks;
    patch.reverse = newLayer0.reverse;
    patch.smartTrim = newLayer0.smartTrim;
  }

  return patch;
}

/**
 * 指定 Layer の mute / solo を更新する patch を返す。
 * Solo を ON にすると同 Layer の Mute は自動解除（Pad の Solo と同じ運用）。
 */
export function patchLayerMuteSolo(
  pad: PadParams,
  layerIndex: number,
  field: 'mute' | 'solo',
  value: boolean,
): Partial<PadParams> {
  const layers = ensureLayers(pad);
  const nextLayers = layers.map((l, i) => {
    if (i !== layerIndex) return l;
    if (field === 'solo' && value) return { ...l, solo: true, mute: false };
    return { ...l, [field]: value };
  });
  return { layers: nextLayers };
}

/**
 * 指定 Layer の velocityMin / velocityMax を更新する patch を返す。
 * 0..127 にクランプし、min <= max を保証する。
 */
export function patchLayerVelocityRange(
  pad: PadParams,
  layerIndex: number,
  range: { min?: number; max?: number },
): Partial<PadParams> {
  const layers = ensureLayers(pad);
  const target = layers[layerIndex];
  if (!target) return {};
  const min = clamp127(range.min ?? target.velocityMin);
  const max = clamp127(range.max ?? target.velocityMax);
  const lo = Math.min(min, max);
  const hi = Math.max(min, max);
  const nextLayers = layers.map((l, i) =>
    i === layerIndex ? { ...l, velocityMin: lo, velocityMax: hi } : l,
  );
  return { layers: nextLayers };
}

function clamp127(v: number): number {
  if (!Number.isFinite(v)) return 0;
  return Math.max(0, Math.min(127, Math.round(v)));
}

/**
 * Pad 内の全 Layer の velocityMin/Max を均等分割で割り当てる patch を返す。
 *
 * - `splits === 1` (Full Range) は全 Layer に 0..127 を割り当てる。
 * - `splits >= 2` は先頭 N Layer に対し 0..127 を等分配し、N を超える Layer は
 *   Full Range にフォールバック（無効化したくないので、明示的に全域に戻す）。
 * - 端数は前半区間にまとめる（割り切れない 127 を均す）。
 */
export function patchAutoSplitVelocity(
  pad: PadParams,
  splits: number,
): Partial<PadParams> {
  const layers = ensureLayers(pad);
  const n = Math.max(1, Math.min(MAX_LAYERS_PER_PAD, Math.floor(splits)));

  if (n === 1) {
    const nextLayers = layers.map(l => ({ ...l, velocityMin: 0, velocityMax: 127 }));
    return { layers: nextLayers };
  }

  // 0..127 (128 値) を n 等分。floor で割り、余りは前半区間に +1 加算。
  const total = 128;
  const base = Math.floor(total / n);
  const remainder = total - base * n;

  const ranges: Array<{ min: number; max: number }> = [];
  let cursor = 0;
  for (let i = 0; i < n; i++) {
    const size = base + (i < remainder ? 1 : 0);
    const min = cursor;
    const max = Math.min(127, cursor + size - 1);
    ranges.push({ min, max });
    cursor += size;
  }

  const nextLayers = layers.map((l, i) => {
    const r = ranges[i];
    if (!r) return { ...l, velocityMin: 0, velocityMax: 127 };
    return { ...l, velocityMin: r.min, velocityMax: r.max };
  });
  return { layers: nextLayers };
}
