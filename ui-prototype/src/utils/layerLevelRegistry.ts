import { levelToMeter } from './parameterFormat';

// ─────────────────────────────────────────────────────────────────────────────
// Per-Layer tab flash + level meter registry.
//
// Pattern mirrors padFlashRegistry (Web Animations API, no forced reflow) and
// meterRegistry (二重 transform fill, DOM-direct update from rAF loop).
//
// Usage:
//   Mount:   cleanup = registerLayerTab(padIndex, layerIndex, meterClipEl, meterFillEl, flashOverlayEl)
//   Unmount: cleanup()
//   Flash:   triggerLayerFlash(padIndex, layerIndex)   — one-shot, 220ms ease-out
//   Meter:   updateLayerMeter(padIndex, layerIndex, level) — horizontal bar, blue→yellow→red
//
// Thread safety: all calls must be on the JS / UI thread (rAF loop or event handler).
// ─────────────────────────────────────────────────────────────────────────────

interface LayerTarget {
  // v8+: meterRegistry と同じ二重 transform 方式 (wrapper を scaleX、inner gradient
  // を逆 scaleX)。clip-path を捨て GPU 合成のみで動かす。
  meterClip: HTMLElement | null;   // wrapper: scaleX(level)
  meterFill: HTMLElement | null;   // gradient: scaleX(1/level)
  flashOverlay: HTMLElement | null;
  flashAnim?: Animation;
  _lastMeter?: number;
}

// registry: padIndex → layerIndex → token → LayerTarget
const registry = new Map<number, Map<number, Map<symbol, LayerTarget>>>();

// ── Flash ────────────────────────────────────────────────────────────────────
// Subtle tint overlay (opacity 0.62 → 0) — 220ms, ease-out.
// Does NOT touch box-shadow so the gold tab-active accent is undisturbed.
const flashKeyframes: Keyframe[] = [
  { offset: 0,    opacity: 0.62 },
  { offset: 0.30, opacity: 0.22 },
  { offset: 1,    opacity: 0 },
];

const flashOptions: KeyframeAnimationOptions = {
  duration: 220,
  easing: 'ease-out',
  fill: 'none',
};

// ── Meter fill ───────────────────────────────────────────────────────────────
// Horizontal (x-axis): wrapper を scaleX(level) で伸縮し、内側 gradient を
// scaleX(1/level) で打ち消して色を固定。transform-origin: left (CSS 側)。
const FILL_MIN = 0.0008;
function setFillAmountX(target: LayerTarget, amount: number) {
  const a = amount <= 0 ? 0 : amount >= 1 ? 1 : amount;
  if (a <= FILL_MIN) {
    if (target.meterClip) target.meterClip.style.transform = 'scaleX(0)';
    if (target.meterFill) target.meterFill.style.transform = 'scaleX(1)';
    return;
  }
  if (target.meterClip) target.meterClip.style.transform = `scaleX(${a})`;
  if (target.meterFill) target.meterFill.style.transform = `scaleX(${1 / a})`;
}

// ── Public API ───────────────────────────────────────────────────────────────

/**
 * Register a layer tab's meter fill and flash overlay elements.
 * Returns a cleanup function to call on unmount.
 */
export function registerLayerTab(
  padIndex: number,
  layerIndex: number,
  meterClip: HTMLElement | null,
  meterFill: HTMLElement | null,
  flashOverlay: HTMLElement | null,
): () => void {
  const token = Symbol('layer-tab');

  if (!registry.has(padIndex)) registry.set(padIndex, new Map());
  const padMap = registry.get(padIndex)!;
  if (!padMap.has(layerIndex)) padMap.set(layerIndex, new Map());
  const layerMap = padMap.get(layerIndex)!;

  const target: LayerTarget = { meterClip, meterFill, flashOverlay };
  layerMap.set(token, target);

  // Initialise meter to zero width
  setFillAmountX(target, 0);

  return () => {
    target.flashAnim?.cancel();
    layerMap.delete(token);
    if (layerMap.size === 0) {
      padMap.delete(layerIndex);
      if (padMap.size === 0) registry.delete(padIndex);
    }
  };
}

/**
 * Trigger a one-shot flash on the given layer tab.
 * Call this when the layer actually fires (from the layerTriggers event).
 */
export function triggerLayerFlash(padIndex: number, layerIndex: number) {
  const layerMap = registry.get(padIndex)?.get(layerIndex);
  if (!layerMap) return;

  layerMap.forEach(target => {
    // Cancel any in-progress animation before restarting (rapid retriggering)
    target.flashAnim?.cancel();
    if (target.flashOverlay) {
      target.flashAnim = target.flashOverlay.animate(flashKeyframes, flashOptions);
    }
  });
}

/**
 * Update the level meter for a layer tab.
 * Call this from the rAF loop with the smoothed display level (0..1 linear).
 */
export function updateLayerMeter(padIndex: number, layerIndex: number, level: number) {
  const layerMap = registry.get(padIndex)?.get(layerIndex);
  if (!layerMap) return;

  const meter = level > 0.001 ? levelToMeter(level) : 0;
  layerMap.forEach(target => {
    // 0.5% 未満の変化は skip (meterRegistry と同じ dedupe)
    if (target._lastMeter !== undefined && Math.abs(meter - target._lastMeter) <= 0.005) return;
    target._lastMeter = meter;
    setFillAmountX(target, meter);
  });
}
