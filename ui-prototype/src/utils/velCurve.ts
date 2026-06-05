/**
 * VEL Curve — ベロシティ反応カーブ。
 *
 * 4 ポイント (端点 2 固定 + 可動 2) の折れ線で構成。
 * C++ 側 DSP も同じピースワイズ線形補間を使うこと (一貫性維持)。
 */
import type { VelCurveState, VelCurvePreset, VelCurvePoint } from '../types';

/** プリセット → 可動ポイント 2 個。x, y は 0..1。 */
export const VEL_CURVE_PRESETS: Record<Exclude<VelCurvePreset, 'Custom'>, { p1: VelCurvePoint; p2: VelCurvePoint }> = {
  // 線形: 真っ直ぐ
  'Linear':        { p1: { x: 0.33, y: 0.33 }, p2: { x: 0.66, y: 0.66 } },
  // 弱いベロシティでも持ち上がる (上に膨らむカーブ)
  'Soft':          { p1: { x: 0.33, y: 0.55 }, p2: { x: 0.66, y: 0.85 } },
  // 強く叩かないと大きくならない (下に膨らむカーブ)
  'Hard':          { p1: { x: 0.33, y: 0.15 }, p2: { x: 0.66, y: 0.45 } },
  // ベロシティ差を圧縮 (中央寄りで平坦)
  'Less Dynamics': { p1: { x: 0.33, y: 0.60 }, p2: { x: 0.66, y: 0.78 } },
  // ベロシティ差を強調 (S字気味)
  'More Dynamics': { p1: { x: 0.33, y: 0.20 }, p2: { x: 0.66, y: 0.80 } },
};

export const DEFAULT_VEL_CURVE: VelCurveState = {
  preset: 'Linear',
  p1: { ...VEL_CURVE_PRESETS.Linear.p1 },
  p2: { ...VEL_CURVE_PRESETS.Linear.p2 },
};

export function presetVelCurve(preset: Exclude<VelCurvePreset, 'Custom'>): VelCurveState {
  const p = VEL_CURVE_PRESETS[preset];
  return { preset, p1: { ...p.p1 }, p2: { ...p.p2 } };
}

/**
 * 現在のポイントがどのプリセットと一致するか判定。
 * 一致しなければ 'Custom'。微小誤差を許容(±0.005)。
 */
export function detectPreset(p1: VelCurvePoint, p2: VelCurvePoint): VelCurvePreset {
  const eps = 0.005;
  const near = (a: number, b: number) => Math.abs(a - b) <= eps;
  for (const name of Object.keys(VEL_CURVE_PRESETS) as Array<Exclude<VelCurvePreset, 'Custom'>>) {
    const r = VEL_CURVE_PRESETS[name];
    if (near(p1.x, r.p1.x) && near(p1.y, r.p1.y) && near(p2.x, r.p2.x) && near(p2.y, r.p2.y)) {
      return name;
    }
  }
  return 'Custom';
}

/**
 * カーブを 0..1 の入力に対して評価。
 * (0,0) → p1 → p2 → (1,1) のピースワイズ線形補間。
 */
export function evaluateVelCurve(curve: VelCurveState | undefined, xIn: number): number {
  const x = Math.max(0, Math.min(1, xIn));
  const c = curve ?? DEFAULT_VEL_CURVE;
  // p1.x <= p2.x を保証 (UI 側で制限するが防御的に並び替え)
  const a = c.p1.x <= c.p2.x ? c.p1 : c.p2;
  const b = c.p1.x <= c.p2.x ? c.p2 : c.p1;

  // (0,0) → a
  if (x <= a.x) {
    if (a.x <= 1e-6) return a.y;
    return (a.y * x) / a.x;
  }
  // a → b
  if (x <= b.x) {
    const span = Math.max(1e-6, b.x - a.x);
    return a.y + (b.y - a.y) * ((x - a.x) / span);
  }
  // b → (1,1)
  const span = Math.max(1e-6, 1 - b.x);
  return b.y + (1 - b.y) * ((x - b.x) / span);
}

/** カーブを N 個サンプリングして SVG path 描画用の polyline 配列を作る。 */
export function sampleVelCurve(curve: VelCurveState, samples = 64): Array<{ x: number; y: number }> {
  const out: Array<{ x: number; y: number }> = [];
  for (let i = 0; i < samples; i++) {
    const x = i / (samples - 1);
    out.push({ x, y: evaluateVelCurve(curve, x) });
  }
  return out;
}

/** ポイント変更時に preset を再判定して新しい state を返す。 */
export function withUpdatedPoint(curve: VelCurveState, which: 'p1' | 'p2', next: VelCurvePoint): VelCurveState {
  // p1 / p2 の x が交差しないようクランプ
  let p1 = which === 'p1' ? next : curve.p1;
  let p2 = which === 'p2' ? next : curve.p2;
  // x 範囲: p1.x in (0, p2.x), p2.x in (p1.x, 1)
  if (which === 'p1') {
    p1 = { x: clamp(p1.x, 0.01, p2.x - 0.01), y: clamp(p1.y, 0, 1) };
  } else {
    p2 = { x: clamp(p2.x, p1.x + 0.01, 0.99), y: clamp(p2.y, 0, 1) };
  }
  const preset = detectPreset(p1, p2);
  return { preset, p1, p2 };
}

function clamp(v: number, lo: number, hi: number): number {
  if (lo > hi) return lo;
  return Math.max(lo, Math.min(hi, v));
}
