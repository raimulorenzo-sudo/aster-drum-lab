import type { FxSlot, EqParams, LayerParams, PadParams } from '../types';
import { NEUTRAL_EQ } from '../types';
import { ensureLayers, selectedLayerIndexOf } from './layerView';

/** Layer に対する FX Chain を読み出す (undefined を [] に正規化)。 */
export function fxChainOf(layer: LayerParams | undefined): FxSlot[] {
  return layer?.fxChain ?? [];
}

/** EQ slot の index を返す (無ければ -1)。 */
export function findEqIndex(chain: FxSlot[]): number {
  return chain.findIndex(s => s.type === 'EQ');
}

/** ニュートラル EQ を含む新規 FX Chain (初期投入用)。 */
export function makeInitialFxChain(): FxSlot[] {
  return [{ type: 'EQ', bypassed: false, params: cloneEq(NEUTRAL_EQ) }];
}

export function cloneEq(eq: EqParams): EqParams {
  return {
    bypassed: eq.bypassed,
    lowMode: eq.lowMode === 'cut' ? 'cut' : 'shelf',
    highMode: eq.highMode === 'cut' ? 'cut' : 'shelf',
    low: { ...eq.low },
    lowMid: { ...eq.lowMid },
    highMid: { ...eq.highMid },
    high: { ...eq.high },
  };
}

export function makeFxSlot(type: FxSlot['type']): FxSlot {
  switch (type) {
    case 'EQ':
      return { type: 'EQ', bypassed: false, params: cloneEq(NEUTRAL_EQ) };
    case 'FILTER':
      return {
        type: 'FILTER',
        bypassed: false,
        params: {
          hpEnabled: true, hpCutoff: 80, hpSlope: 12, hpResonance: 0.7,
          lpEnabled: false, lpCutoff: 18000, lpSlope: 12, lpResonance: 0.7,
        },
      };
    case 'DRIVE':
      return { type: 'DRIVE', bypassed: false, params: { type: 'SOFT_CLIP', amount: 0.25, tone: 0.5, mix: 1, output: 0 } };
    case 'TRANSIENT':
      return { type: 'TRANSIENT', bypassed: false, params: { attack: 0, sustain: 0 } };
    case 'COMPRESSOR':
      return {
        type: 'COMPRESSOR',
        bypassed: false,
        params: { threshold: -12, ratio: 4, attack: 8, release: 80, makeup: 0, mix: 1, output: 0 },
      };
  }
}

export function cloneFxSlot(slot: FxSlot): FxSlot {
  switch (slot.type) {
    case 'EQ':
      return { type: 'EQ', bypassed: slot.bypassed, params: cloneEq(slot.params) };
    case 'FILTER':
      return { type: 'FILTER', bypassed: slot.bypassed, params: { ...slot.params } };
    case 'DRIVE':
      return { type: 'DRIVE', bypassed: slot.bypassed, params: { ...slot.params } };
    case 'TRANSIENT':
      return { type: 'TRANSIENT', bypassed: slot.bypassed, params: { ...slot.params } };
    case 'COMPRESSOR':
      return { type: 'COMPRESSOR', bypassed: slot.bypassed, params: { ...slot.params } };
  }
}

export function cloneFxChain(chain: FxSlot[]): FxSlot[] {
  return chain.map(cloneFxSlot);
}

/**
 * 選択中 Layer の FX Chain を返す。
 * - layer.fxChain が **未定義** → 初期 EQ 1 個を含む既定チェーンを返す
 *   (UI 上ではニュートラル EQ が刺さって見える)。
 * - layer.fxChain が `[]` → ユーザーが明示的に空にした扱い (空のまま返す)。
 */
export function fxChainOfSelected(pad: PadParams): FxSlot[] {
  const layers = ensureLayers(pad);
  const idx = selectedLayerIndexOf(pad);
  const layer = layers[idx];
  if (!layer) return [];
  return layer.fxChain === undefined ? makeInitialFxChain() : layer.fxChain;
}

/** EQ slot を更新するヘルパー (新しい params をマージ)。 */
export function updateEqSlot(
  chain: FxSlot[],
  patch: Partial<EqParams>,
): FxSlot[] {
  const idx = findEqIndex(chain);
  if (idx === -1) return chain;
  const slot = chain[idx];
  if (slot.type !== 'EQ') return chain;
  const next: FxSlot = {
    type: 'EQ',
    bypassed: slot.bypassed,
    params: {
      bypassed: patch.bypassed ?? slot.params.bypassed,
      lowMode: patch.lowMode ?? slot.params.lowMode,
      highMode: patch.highMode ?? slot.params.highMode,
      low: { ...slot.params.low, ...patch.low },
      lowMid: { ...slot.params.lowMid, ...patch.lowMid },
      highMid: { ...slot.params.highMid, ...patch.highMid },
      high: { ...slot.params.high, ...patch.high },
    },
  };
  return chain.map((s, i) => (i === idx ? next : s));
}

/**
 * EQ slot を「現在の params を読みつつ更新する」形で書き換える。
 * バンドごとのノブ/グラフが古いクロージャで chain を上書きしないよう、
 * 呼び出し時点の最新 params に対して updater を適用する用。
 */
export function updateEqSlotWith(
  chain: FxSlot[],
  updater: (eq: EqParams) => EqParams,
): FxSlot[] {
  const idx = findEqIndex(chain);
  if (idx === -1) return chain;
  const slot = chain[idx];
  if (slot.type !== 'EQ') return chain;
  return chain.map((s, i) =>
    i === idx
      ? { type: 'EQ' as const, bypassed: slot.bypassed, params: updater(slot.params) }
      : s,
  );
}

/** EQ slot の Bypass を toggle。 */
export function toggleEqBypass(chain: FxSlot[]): FxSlot[] {
  const idx = findEqIndex(chain);
  if (idx === -1) return chain;
  return chain.map((s, i) =>
    i === idx && s.type === 'EQ' ? { ...s, bypassed: !s.bypassed } : s,
  );
}

/** EQ slot を削除。 */
export function removeEqSlot(chain: FxSlot[]): FxSlot[] {
  const idx = findEqIndex(chain);
  if (idx === -1) return chain;
  return chain.filter((_, i) => i !== idx);
}

/** EQ slot を初期値で追加 (既存があれば何もしない)。 */
export function addEqSlot(chain: FxSlot[]): FxSlot[] {
  if (findEqIndex(chain) !== -1) return chain;
  return [...chain, { type: 'EQ', bypassed: false, params: cloneEq(NEUTRAL_EQ) }];
}
