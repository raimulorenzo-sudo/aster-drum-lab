/**
 * JUCE WebBrowserComponent native integration bridge.
 *
 * All functions are safe no-ops when running outside the plugin
 * (window.__JUCE__ will be undefined in the browser dev server).
 *
 * JUCE 8 JS API:
 *   backend.addEventListener(eventId, fn)  → returns handle [eventId, id]
 *   backend.removeEventListener(handle)
 *   backend.emitEvent(eventId, data)       → sends to C++ withEventListener handlers
 *   (C++ → JS via backend.emitByBackend called internally when C++ calls emitEventIfBrowserIsVisible)
 */

import type { PadParams, PlayMode, KitPage, OutputMode, LayerParams, EqParams, FxSlot, FilterParams, FilterSlope } from '../types';
import { NEUTRAL_EQ } from '../types';
import { INITIAL_PADS } from '../data/padData';
import {
  FALLBACK_SAMPLE_LENGTH_MS,
  MIN_TRIM_RANGE_MS,
  normalizePadTrimPatch,
  trimToNormalized,
} from './sampleTrim';

// ─── JUCE window type ───────────────────────────────────────────────────────
declare global {
  interface Window {
    __JUCE__?: {
      backend: {
        addEventListener: (eventId: string, fn: (data: unknown) => void) => [string, number];
        removeEventListener: (handle: [string, number]) => void;
        emitEvent: (eventId: string, object: unknown) => void;
      };
    };
  }
}

// ─── Availability check ─────────────────────────────────────────────────────
export function isJuceAvailable(): boolean {
  return typeof window !== 'undefined' &&
    window.__JUCE__ != null &&
    window.__JUCE__.backend != null;
}

// ─── JS → C++ ───────────────────────────────────────────────────────────────
/** Send a typed UI message to the C++ plugin. Silent no-op in browser. */
export function sendToJuce(type: string, payload: Record<string, unknown>): void {
  if (!isJuceAvailable()) return;
  try {
    window.__JUCE__!.backend.emitEvent('uiMessage', { type, payload });
  } catch (err) {
    console.warn('[JUCE Bridge] emitEvent error:', err);
  }
}

// ─── C++ → JS ───────────────────────────────────────────────────────────────
/**
 * Register a listener for an event emitted by C++.
 * Returns a cleanup (unsubscribe) function.
 */
export function onJuceEvent(
  name: string,
  callback: (data: unknown) => void,
): () => void {
  if (!isJuceAvailable()) return () => {};
  try {
    const handle = window.__JUCE__!.backend.addEventListener(name, callback);
    return () => {
      try { window.__JUCE__!.backend.removeEventListener(handle); } catch { /* ignore */ }
    };
  } catch (err) {
    console.warn('[JUCE Bridge] addEventListener error:', err);
    return () => {};
  }
}

// ─── Data conversion: C++ → React ───────────────────────────────────────────
/**
 * Raw pad shape as serialised by PadDataJson::padToVar().
 * Field names must match the C++ PadData fields exactly.
 */
export interface JucePadData {
  padName: string;
  sampleFileName: string;
  sampleFilePath: string;
  sampleMissing: boolean;
  // Pad color override. JS treats padColourMode === 1 as "user override" and
  // applies padColourARGB as the displayed color. mode === 0 means "auto"
  // (use the category color derived on the JS side).
  padColourARGB?: number;
  padColourMode?: number;
  midiNote: number;
  volume: number;
  pan: number;
  pitch: number;
  attack: number;
  release: number;
  startPosition: number;   // normalised 0..1 within sample
  endPosition: number;     // normalised 0..1 within sample
  fadeIn: number;          // normalised 0..1 of playback range
  fadeOut: number;         // normalised 0..1 of playback range
  sampleLengthMs?: number; // actual sample duration when known
  waveformPeaks?: number[]; // lightweight 0..1 peaks generated from actual sample data
  reverse: boolean;
  playbackMode: string;    // "OneShot" | "Gate"
  chokeGroup: number;
  mute: boolean;
  solo: boolean;
  outputAssign: number;
  velocitySens: number;
  humanize: number;

  // Polyphony / Voice Steal (v7+)
  polyphony?: number;
  voiceSteal?: 'oldest' | 'quietest' | 'off';

  // VEL Curve (v7+). Optional for backward compat.
  velCurve?: { preset: number; p1x: number; p1y: number; p2x: number; p2y: number };

  // Pad-level Vol/Pan/Pitch. Optional for backward compat.
  padVolume?: number;
  padPan?: number;
  padPitch?: number;

  // Layers (v6+). Optional for backward compat with older C++ builds.
  layers?: JuceLayerData[];
}

/**
 * Raw layer shape as serialised by PadDataJson::padToVar() / padToWebVar() in v6+.
 * waveformPeaks / sampleLengthMs are injected by padToWebVar() (not saved to file).
 */
export interface JuceLayerData {
  sampleFileName: string;
  sampleFilePath: string;
  sampleMissing?: boolean;
  layerName?: string;
  volume: number;
  pan: number;
  pitch: number;
  attack: number;
  release: number;
  startPosition: number;
  endPosition: number;
  fadeIn: number;
  fadeOut: number;
  reverse: boolean;
  smartTrim: boolean;
  mute: boolean;
  solo: boolean;
  velocityMin: number;
  velocityMax: number;
  eq?: EqParams;
  fxChain?: FxSlot[];
  // Runtime-only (injected by padToWebVar, not persisted to .asterkit)
  sampleLengthMs?: number;
  waveformPeaks?: number[];
}

export interface JuceKitData {
  pads: JucePadData[];
  page: number;            // 0=A 1=B 2=C
  selectedIndex: number;
  kitName: string;
  kitDirty?: boolean;
  kitReadOnly?: boolean;
  currentKitPath?: string;
  outputMode: string;      // "Stereo" | "16Outs" | "32Outs" | "48Outs"
  masterVolume?: number;   // フェーダー位置 [0..1]（0.75 = ユニティ）
}

/**
 * ms reference length used when converting normalised positions → ms.
 * C++ does not send sample duration, so we use a reasonable default.
 * The visual display will be proportionally correct; exact ms values
 * only matter for display (not for audio—C++ uses normalised positions).
 */
export const JUCE_DEFAULT_SAMPLE_MS = FALLBACK_SAMPLE_LENGTH_MS;

export const JUCE_PAGES: KitPage[] = ['A', 'B', 'C'];

/**
 * Convert a C++ JucePadData object to a React PadParams object.
 * UI-only fields (categoryColor, outputName, …) are preserved from `existing`.
 */
function argbToHexColor(argb: number): string {
  // JUCE uses 0xAARRGGBB. JS color strings only need RGB.
  const r = (argb >> 16) & 0xff;
  const g = (argb >> 8)  & 0xff;
  const b =  argb        & 0xff;
  return '#' + [r, g, b].map(n => n.toString(16).padStart(2, '0')).join('');
}

/**
 * Convert a single JuceLayerData to a React LayerParams.
 * fallbackLengthMs: top-level pad sampleLengthMs — used only when the layer
 * has no sample loaded (sampleLengthMs === 0) so trim knobs show a
 * reasonable range even for an empty layer.
 */
function juceLayerToReact(jl: JuceLayerData, fallbackLengthMs: number): LayerParams {
  // Per-layer sampleLengthMs injected by padToWebVar() takes priority.
  // If 0 (no buffer loaded yet) fall back to the pad-level length so knobs
  // still display sensible values.
  const layerLengthMs =
    Number.isFinite(jl.sampleLengthMs) && (jl.sampleLengthMs ?? 0) > MIN_TRIM_RANGE_MS
      ? (jl.sampleLengthMs as number)
      : fallbackLengthMs;

  const endMs   = jl.endPosition   * layerLengthMs;
  const startMs = jl.startPosition * layerLengthMs;
  const playbackRangeMs = Math.max(MIN_TRIM_RANGE_MS, endMs - startMs);

  // Per-layer waveform injected by padToWebVar().
  const waveformPeaks = Array.isArray(jl.waveformPeaks)
    ? jl.waveformPeaks.filter(v => Number.isFinite(v)).map(v => Math.max(0, Math.min(1, Number(v))))
    : undefined;

  return {
    sampleFileName: jl.sampleFileName,
    sampleFilePath: jl.sampleFilePath,
    sampleMissing:  jl.sampleMissing,
    layerName:      jl.layerName,
    volume:         jl.volume,
    pan:            jl.pan,
    pitch:          jl.pitch,
    attack:         jl.attack,
    release:        jl.release,
    sampleLengthMs: layerLengthMs,
    waveformPeaks,
    startMs,
    endMs,
    fadeInMs:       jl.fadeIn  * playbackRangeMs,
    fadeOutMs:      jl.fadeOut * playbackRangeMs,
    reverse:        jl.reverse,
    smartTrim:      jl.smartTrim,
    mute:           jl.mute,
    solo:           jl.solo,
    velocityMin:    jl.velocityMin,
    velocityMax:    jl.velocityMax,
    eq:             normalizeEq(jl.eq),
    fxChain:        normalizeFxChain(jl.fxChain),
  };
}

function cloneEq(eq: EqParams): EqParams {
  return {
    bypassed: Boolean(eq.bypassed),
    lowMode: normalizeEqMode(eq.lowMode),
    highMode: normalizeEqMode(eq.highMode),
    low: { ...eq.low },
    lowMid: { ...eq.lowMid },
    highMid: { ...eq.highMid },
    high: { ...eq.high },
  };
}

function normalizeEq(eq: EqParams | undefined): EqParams {
  if (!eq) return cloneEq(NEUTRAL_EQ);
  return {
    bypassed: Boolean(eq.bypassed),
    lowMode: normalizeEqMode(eq.lowMode),
    highMode: normalizeEqMode(eq.highMode),
    low: { ...NEUTRAL_EQ.low, ...(eq.low ?? {}) },
    lowMid: { ...NEUTRAL_EQ.lowMid, ...(eq.lowMid ?? {}) },
    highMid: { ...NEUTRAL_EQ.highMid, ...(eq.highMid ?? {}) },
    high: { ...NEUTRAL_EQ.high, ...(eq.high ?? {}) },
  };
}

function normalizeEqMode(value: unknown): 'shelf' | 'cut' {
  if (value === 'cut' || value === true || value === 1) return 'cut';
  return 'shelf';
}

function normalizeFxChain(chain: FxSlot[] | undefined): FxSlot[] | undefined {
  if (!Array.isArray(chain)) return undefined;
  return chain.map(slot => {
    switch (slot.type) {
      case 'EQ':
        return { type: 'EQ' as const, bypassed: Boolean(slot.bypassed), params: normalizeEq(slot.params) };
      case 'FILTER': {
        const p = (slot.params ?? {}) as Partial<FilterParams>
          & { mode?: string; cutoff?: number; slope?: number; resonance?: number };
        const clampF = (v: number, d: number) => Number.isFinite(v) ? Math.max(20, Math.min(20000, v)) : d;
        const toSlope = (v: unknown, d: FilterSlope): FilterSlope => v === 12 ? 12 : v === 24 ? 24 : v === 48 ? 48 : d;

        let hpEnabled: boolean, hpCutoff: number, lpEnabled: boolean, lpCutoff: number;
        let hpSlope: FilterSlope, lpSlope: FilterSlope, hpResonance: number, lpResonance: number;

        if (p.hpEnabled === undefined && p.lpEnabled === undefined && p.mode !== undefined) {
          // legacy (single mode HP/LP) → 移行
          const isLp = p.mode === 'LP';
          hpEnabled = !isLp; lpEnabled = isLp;
          hpCutoff = clampF(Number(!isLp ? p.cutoff : 80), 80);
          lpCutoff = clampF(Number(isLp ? p.cutoff : 18000), 18000);
          hpSlope = lpSlope = 12;
          hpResonance = lpResonance = Number(p.resonance ?? 0.7);
        } else {
          hpEnabled = Boolean(p.hpEnabled);
          lpEnabled = Boolean(p.lpEnabled);
          hpCutoff = clampF(Number(p.hpCutoff), 80);
          lpCutoff = clampF(Number(p.lpCutoff), 18000);
          // 旧 shared slope/resonance があればフォールバックに使う
          hpSlope = toSlope(p.hpSlope, toSlope(p.slope, 12));
          lpSlope = toSlope(p.lpSlope, toSlope(p.slope, 12));
          hpResonance = Number(p.hpResonance ?? p.resonance ?? 0.7);
          lpResonance = Number(p.lpResonance ?? p.resonance ?? 0.7);
        }
        return {
          type: 'FILTER' as const,
          bypassed: Boolean(slot.bypassed),
          params: { hpEnabled, hpCutoff, hpSlope, hpResonance, lpEnabled, lpCutoff, lpSlope, lpResonance },
        };
      }
      case 'DRIVE':
        return {
          type: 'DRIVE' as const,
          bypassed: Boolean(slot.bypassed),
          params: {
            type: slot.params?.type ?? 'SOFT_CLIP',
            amount: Number(slot.params?.amount ?? 0.25),
            tone: Number(slot.params?.tone ?? 0.5),
            mix: Number(slot.params?.mix ?? 1),
            output: Number(slot.params?.output ?? 0),
          },
        };
      case 'TRANSIENT':
        return {
          type: 'TRANSIENT' as const,
          bypassed: Boolean(slot.bypassed),
          params: {
            attack: Number(slot.params?.attack ?? 0),
            sustain: Number(slot.params?.sustain ?? 0),
          },
        };
      case 'COMPRESSOR':
        return {
          type: 'COMPRESSOR' as const,
          bypassed: Boolean(slot.bypassed),
          params: {
            threshold: Number(slot.params?.threshold ?? -12),
            ratio: Number(slot.params?.ratio ?? 4),
            attack: Number(slot.params?.attack ?? 8),
            release: Number(slot.params?.release ?? 80),
            mix: Number(slot.params?.mix ?? 1),
          },
        };
    }
  });
}

export function jucePadToReact(jp: JucePadData, existing: PadParams): PadParams {
  const sampleLengthMs = Number.isFinite(jp.sampleLengthMs) && (jp.sampleLengthMs ?? 0) > MIN_TRIM_RANGE_MS
    ? jp.sampleLengthMs as number
    : JUCE_DEFAULT_SAMPLE_MS;
  const endMs   = jp.endPosition   * sampleLengthMs;
  const startMs = jp.startPosition * sampleLengthMs;
  const playbackRangeMs = Math.max(MIN_TRIM_RANGE_MS, endMs - startMs);

  // Color: mode=1 (Custom) → use ARGB as user override; otherwise auto (undefined).
  const isCustomColor = jp.padColourMode === 1
    && typeof jp.padColourARGB === 'number'
    && jp.padColourARGB !== 0;
  const padColor = isCustomColor ? argbToHexColor(jp.padColourARGB as number) : undefined;

  // Layers (v6+). If older C++ omits it, derive Layer 0 from flat fields.
  const layers: LayerParams[] = Array.isArray(jp.layers) && jp.layers.length > 0
    ? jp.layers.map(jl => juceLayerToReact(jl, sampleLengthMs))
    : [{
        sampleFileName: jp.sampleFileName,
        sampleFilePath: jp.sampleFilePath,
        sampleMissing:  jp.sampleMissing,
        volume:         jp.volume,
        pan:            jp.pan,
        pitch:          jp.pitch,
        attack:         jp.attack,
        release:        jp.release,
        sampleLengthMs,
        startMs,
        endMs,
        fadeInMs:       jp.fadeIn  * playbackRangeMs,
        fadeOutMs:      jp.fadeOut * playbackRangeMs,
        reverse:        jp.reverse,
        smartTrim:      existing.smartTrim ?? true,
        mute:           false,
        solo:           false,
        velocityMin:    0,
        velocityMax:    127,
        eq:             cloneEq(NEUTRAL_EQ),
        fxChain:        undefined,
      }];

  return {
    ...existing,
    padColor,
    padName:       jp.padName       || existing.padName,
    sampleFileName: jp.sampleFileName,
    sampleFilePath: jp.sampleFilePath,
    sampleMissing:  jp.sampleMissing,
    midiNote:       jp.midiNote,
    volume:         jp.volume,
    pan:            jp.pan,
    pitch:          jp.pitch,
    attack:         jp.attack,
    release:        jp.release,
    sampleLengthMs,
    waveformPeaks:  Array.isArray(jp.waveformPeaks)
      ? jp.waveformPeaks.filter(v => Number.isFinite(v)).map(v => Math.max(0, Math.min(1, Number(v))))
      : [],
    startMs,
    endMs,
    fadeInMs:       jp.fadeIn  * playbackRangeMs,
    fadeOutMs:      jp.fadeOut * playbackRangeMs,
    reverse:        jp.reverse,
    playMode:       jp.playbackMode as PlayMode,
    chokeGroup:     jp.chokeGroup,
    mute:           jp.mute,
    solo:           jp.solo,
    outputAssign:   jp.outputAssign,
    velocitySens:   jp.velocitySens,
    humanize:       jp.humanize,
    polyphony:      typeof jp.polyphony  === 'number' ? jp.polyphony  : existing.polyphony,
    voiceSteal:     jp.voiceSteal ?? existing.voiceSteal,
    velCurve:       jp.velCurve ? {
      preset: (['Linear','Soft','Hard','Less Dynamics','More Dynamics','Custom'] as const)[
        Math.max(0, Math.min(5, jp.velCurve.preset))
      ],
      p1: { x: jp.velCurve.p1x, y: jp.velCurve.p1y },
      p2: { x: jp.velCurve.p2x, y: jp.velCurve.p2y },
    } : existing.velCurve,
    padVolume:      typeof jp.padVolume === 'number' ? jp.padVolume : 0.75,
    padPan:         typeof jp.padPan === 'number' ? jp.padPan : 0.0,
    padPitch:       typeof jp.padPitch === 'number' ? jp.padPitch : 0.0,
    layers,
    selectedLayerIndex: existing.selectedLayerIndex ?? 0,
  };
}

/**
 * Convert a JuceKitData payload into the individual React state values.
 * Uses INITIAL_PADS as the base for UI-only fields.
 */
export function juceKitToReact(kit: JuceKitData, currentPads: PadParams[]): {
  pads: PadParams[];
  page: KitPage;
  selectedIndex: number;
  kitName: string;
  outputMode: OutputMode;
} {
  const pads = kit.pads.map((jp, i) =>
    jucePadToReact(jp, currentPads[i] ?? INITIAL_PADS[i]),
  );

  const page: KitPage =
    kit.page >= 0 && kit.page < JUCE_PAGES.length
      ? JUCE_PAGES[kit.page]
      : 'A';

  const selectedIndex =
    kit.selectedIndex >= 0 && kit.selectedIndex < 48 ? kit.selectedIndex : 0;

  const kitName = kit.kitName || 'Default';

  const validModes: OutputMode[] = ['Stereo', '16Outs', '32Outs', '48Outs'];
  const outputMode: OutputMode = validModes.includes(kit.outputMode as OutputMode)
    ? (kit.outputMode as OutputMode)
    : '48Outs';

  return { pads, page, selectedIndex, kitName, outputMode };
}

// ─── Data conversion: React → C++ ───────────────────────────────────────────
/**
 * Inspect a PadParams patch and emit the corresponding C++ messages.
 * `currentPad` is the pad state BEFORE the patch is applied (used for trim
 * normalisation, since C++ needs 0..1 positions).
 */
export function sendPadPatchToJuce(
  index: number,
  patch: Partial<PadParams>,
  currentPad: PadParams,
): void {
  if (!isJuceAvailable()) return;

  // ── Sample clear ───────────────────────────────────────────────────────
  // 「sampleFileName を空文字に設定する」パッチは Clear Sample 操作と解釈し、
  // C++ 側の AudioBuffer / sampleFilePath / sampleMissing まで含めて
  // まとめて落とす専用メッセージへ変換する。
  // UI 側だけ消えてエンジンには残るバグ（クリア後も MIDI で鳴ってしまう）
  // を防ぐため、ここでひとつにまとめる。
  if (patch.sampleFileName === '' && (currentPad.sampleFileName || currentPad.sampleFilePath)) {
    sendToJuce('clearPadSample', { index });
  }

  // ── Direct 1:1 mappings ────────────────────────────────────────────────
  if (patch.padName      !== undefined) sendToJuce('setPadName',      { index, value: patch.padName });
  if (patch.midiNote     !== undefined) sendToJuce('setMidiNote',     { index, value: patch.midiNote });
  if ('padColor' in patch) {
    if (patch.padColor === undefined) {
      sendToJuce('setPadColor', { index, mode: 'auto' });
    } else {
      const hex = patch.padColor.replace('#', '');
      const argb = 0xff000000 | parseInt(hex.length === 3
        ? hex.split('').map(c => c + c).join('')
        : hex, 16);
      sendToJuce('setPadColor', { index, mode: 'custom', argb: argb >>> 0 });
    }
  }
  if (patch.volume       !== undefined) sendToJuce('setVolume',       { index, value: patch.volume });
  if (patch.padVolume    !== undefined) sendToJuce('setPadVolume',    { index, value: patch.padVolume });
  if (patch.padPan       !== undefined) sendToJuce('setPadPan',       { index, value: patch.padPan });
  if (patch.padPitch     !== undefined) sendToJuce('setPadPitch',     { index, value: patch.padPitch });
  if (patch.pan          !== undefined) sendToJuce('setPan',          { index, value: patch.pan });
  if (patch.pitch        !== undefined) sendToJuce('setPitch',        { index, value: patch.pitch });
  if (patch.mute         !== undefined) sendToJuce('setMute',         { index, value: patch.mute });
  if (patch.solo         !== undefined) sendToJuce('setSolo',         { index, value: patch.solo });
  if (patch.reverse      !== undefined) sendToJuce('setReverse',      { index, value: patch.reverse });
  if (patch.playMode     !== undefined) sendToJuce('setPlaybackMode', { index, value: patch.playMode });
  if (patch.chokeGroup   !== undefined) sendToJuce('setChoke',        { index, value: patch.chokeGroup });
  if (patch.outputAssign !== undefined) sendToJuce('setOutput',       { index, value: patch.outputAssign });
  if (patch.attack       !== undefined) sendToJuce('setAttack',       { index, value: patch.attack });
  if (patch.release      !== undefined) sendToJuce('setRelease',      { index, value: patch.release });
  if (patch.velocitySens !== undefined) sendToJuce('setVelocitySens', { index, value: patch.velocitySens });
  if (patch.humanize     !== undefined) sendToJuce('setHumanize',     { index, value: patch.humanize });
  if (patch.polyphony    !== undefined) sendToJuce('setPolyphony',    { index, value: patch.polyphony });
  if (patch.voiceSteal   !== undefined) sendToJuce('setVoiceSteal',   { index, value: patch.voiceSteal });
  if (patch.velCurve     !== undefined) {
    // preset 名を C++ 用 0..5 に変換 (UI 側と C++ 側で順序を一致させる)
    const presetIndex: Record<string, number> = {
      'Linear': 0, 'Soft': 1, 'Hard': 2, 'Less Dynamics': 3, 'More Dynamics': 4, 'Custom': 5,
    };
    const c = patch.velCurve;
    sendToJuce('setVelCurve', {
      index,
      value: {
        preset: presetIndex[c.preset] ?? 5,
        p1x: c.p1.x, p1y: c.p1.y, p2x: c.p2.x, p2y: c.p2.y,
      },
    });
  }

  // ── Trim: ms → normalised 0..1 ────────────────────────────────────────
  // Use the post-patch merged pad so start/end changes are consistent.
  if (patch.startMs !== undefined || patch.endMs !== undefined ||
      patch.fadeInMs !== undefined || patch.fadeOutMs !== undefined) {
    const trim = normalizePadTrimPatch(currentPad, patch);
    sendToJuce('setSampleTrim', {
      index,
      ...trimToNormalized(trim),
    });
  }

  // ── Layers diff — UI→C++ で Layer 構造変化と Layer 個別パラメータを送る ──
  // ・現状 (currentPad.layers) と patch.layers を比較し、必要なメッセージだけ送出。
  // ・Layer 0 (MAIN) は上記の flat-field 経路でカバー済みなので、ここでは L2+ の
  //   per-layer プロパティに着目する。velocityMin/Max と mute/solo はどの layer
  //   でも個別 message を出す (flat に対応フィールドがないため)。
  if (patch.layers !== undefined) {
    sendLayerPatches(index, patch.layers, currentPad.layers);
  }

  // ── Selected layer tracking ────────────────────────────────────────────
  // Layer 構造変更後の index を C++ に渡す。削除前に送ると、削除後の選択番号と
  // 一瞬ずれるため、layers diff の後で送る。
  if (patch.selectedLayerIndex !== undefined) {
    sendToJuce('selectLayer', { index, layerIndex: patch.selectedLayerIndex });
  }
}

function sendLayerPatches(
  index: number,
  nextLayers: NonNullable<PadParams['layers']>,
  prevLayers: PadParams['layers'] | undefined,
): void {
  const prev = prevLayers ?? [];
  const prevCount = prev.length;
  const nextCount = nextLayers.length;

  // Layer 追加 / 削除 (構造変更)
  if (nextCount > prevCount) {
    const added = nextCount - prevCount;
    for (let i = 0; i < added; i++) {
      sendToJuce('addLayer', { index, copyFromIndex: Math.max(0, prevCount - 1 + i), clearSample: true });
    }
  } else if (nextCount < prevCount) {
    // 削除 — patchRemoveLayer は配列から指定 index を抜くので、残った object 参照を
    // 見て削除位置を推定する。これで L2 など中間 layer の × も正しく消える。
    const removed = prevCount - nextCount;
    let searchPrev = prev;
    let searchNext = nextLayers;
    for (let i = 0; i < removed; i++) {
      let removeIndex = searchPrev.findIndex((layer, layerIndex) => layer !== searchNext[layerIndex]);
      if (removeIndex < 0) removeIndex = searchPrev.length - 1;
      sendToJuce('removeLayer', { index, layerIndex: removeIndex });
      searchPrev = searchPrev.filter((_, layerIndex) => layerIndex !== removeIndex);
      searchNext = searchNext.slice();
    }
  }

  // 各 Layer のフィールド比較。共通範囲のみ走査 (構造変更分は上で処理済)。
  const commonCount = Math.min(prevCount, nextCount);
  for (let li = 0; li < commonCount; li++) {
    const a = prev[li];
    const b = nextLayers[li];
    if (!a || !b) continue;

    // velocity range / mute / solo はどの layer でも個別 message
    if (a.velocityMin !== b.velocityMin || a.velocityMax !== b.velocityMax) {
      sendToJuce('setLayerVelocityRange', {
        index, layerIndex: li, min: b.velocityMin, max: b.velocityMax,
      });
    }
    if (a.mute !== b.mute) {
      sendToJuce('setLayerMute', { index, layerIndex: li, value: b.mute });
    }
    if (a.solo !== b.solo) {
      sendToJuce('setLayerSolo', { index, layerIndex: li, value: b.solo });
    }
    if (!eqEqual(a.eq, b.eq)) {
      sendToJuce('setLayerEq', {
        index,
        layerIndex: li,
        ...flattenEqPatch(normalizeEq(b.eq)),
      });
    }
    if (!fxChainEqual(a.fxChain, b.fxChain)) {
      sendToJuce('setLayerFxChain', {
        index,
        layerIndex: li,
        slots: serializeFxChain(b.fxChain ?? []),
      });
    }

    // L2+ の音作りパラメータは flat 側を通らないので、ここから per-layer message
    // を出す。Layer 0 は flat 経由 (setVolume 等) で既に C++ に届いている。
    if (li === 0) continue;

    if (a.volume !== b.volume)
      sendToJuce('setLayerVolume', { index, layerIndex: li, value: b.volume });
    if (a.pan !== b.pan)
      sendToJuce('setLayerPan', { index, layerIndex: li, value: b.pan });
    if (a.pitch !== b.pitch)
      sendToJuce('setLayerPitch', { index, layerIndex: li, value: b.pitch });
    if (a.attack !== b.attack)
      sendToJuce('setLayerAttack', { index, layerIndex: li, value: b.attack });
    if (a.release !== b.release)
      sendToJuce('setLayerRelease', { index, layerIndex: li, value: b.release });
    if (a.reverse !== b.reverse)
      sendToJuce('setLayerReverse', { index, layerIndex: li, value: b.reverse });

    if (a.startMs !== b.startMs || a.endMs !== b.endMs
        || a.fadeInMs !== b.fadeInMs || a.fadeOutMs !== b.fadeOutMs) {
      // ms → normalised 0..1 に変換。sampleLengthMs があれば使う。
      const len = Math.max(1, b.sampleLengthMs ?? a.sampleLengthMs ?? FALLBACK_SAMPLE_LENGTH_MS);
      const startPosition = juceClamp(b.startMs / len);
      const endPosition   = juceClamp(b.endMs / len);
      const playRange     = Math.max(1, b.endMs - b.startMs);
      const fadeIn        = juceClamp((b.fadeInMs ?? 0)  / playRange);
      const fadeOut       = juceClamp((b.fadeOutMs ?? 0) / playRange);
      sendToJuce('setLayerSampleTrim', {
        index, layerIndex: li, startPosition, endPosition, fadeIn, fadeOut,
      });
    }
  }
}

function eqEqual(a: EqParams | undefined, b: EqParams | undefined): boolean {
  const ea = normalizeEq(a);
  const eb = normalizeEq(b);
  return ea.bypassed === eb.bypassed
    && ea.lowMode === eb.lowMode
    && ea.highMode === eb.highMode
    && bandEqual(ea.low, eb.low)
    && bandEqual(ea.lowMid, eb.lowMid)
    && bandEqual(ea.highMid, eb.highMid)
    && bandEqual(ea.high, eb.high);
}

function fxChainEqual(a: FxSlot[] | undefined, b: FxSlot[] | undefined): boolean {
  const aa = normalizeFxChain(a) ?? [];
  const bb = normalizeFxChain(b) ?? [];
  if (aa.length !== bb.length) return false;
  return aa.every((slot, index) => JSON.stringify(slot) === JSON.stringify(bb[index]));
}

function serializeFxChain(chain: FxSlot[]): Array<Record<string, unknown>> {
  return (normalizeFxChain(chain) ?? []).map(slot => ({
    type: slot.type,
    bypassed: slot.bypassed,
    params: slot.params,
  }));
}

function bandEqual(a: EqParams['low'], b: EqParams['low']): boolean {
  return Math.abs(a.freq - b.freq) < 0.0001
    && Math.abs(a.gain - b.gain) < 0.0001
    && Math.abs(a.q - b.q) < 0.0001;
}

function flattenEqPatch(eq: EqParams): Record<string, number | boolean> {
  return {
    bypassed: eq.bypassed,
    lowMode: eq.lowMode === 'cut',
    highMode: eq.highMode === 'cut',
    lowFreq: eq.low.freq,
    lowGain: eq.low.gain,
    lowQ: eq.low.q,
    lowMidFreq: eq.lowMid.freq,
    lowMidGain: eq.lowMid.gain,
    lowMidQ: eq.lowMid.q,
    highMidFreq: eq.highMid.freq,
    highMidGain: eq.highMid.gain,
    highMidQ: eq.highMid.q,
    highFreq: eq.high.freq,
    highGain: eq.high.gain,
    highQ: eq.high.q,
  };
}

function juceClamp(v: number): number {
  if (!Number.isFinite(v)) return 0;
  return Math.max(0, Math.min(1, v));
}
