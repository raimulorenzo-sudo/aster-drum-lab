import { levelToMeter } from './parameterFormat';

type Axis = 'x' | 'y';

interface MeterTarget {
  axis: Axis;
  // v8+: 塗りは「外側 wrapper を scale で伸縮 (= compositor で clip)」+「内側
  // gradient を逆 scale で打ち消す」二重 transform 方式。clip-path と違い repaint
  // が走らず GPU 合成だけで動く。色グラデの dB 対応も維持される。
  fill: HTMLElement | null;        // wrapper: scale(level) で表示量を作る
  fillInner?: HTMLElement | null;  // gradient: scale(1/level) で色を固定
  peak?: HTMLElement | null;
  clipHoldUntil?: number;
  // v7+: DOM 書き込み重複回避のキャッシュ。同値であれば style/dataset 書き込みを
  // skip し、無音時の repaint コストをほぼゼロに。
  _lastFill?:    number;
  _lastPeak?:    number;
  _lastFillClip?:string;
  _lastPeakClip?:string;
  _lastPeakZone?:string;
  _lastPeakShow?:boolean;
}

const masterMeters = new Map<symbol, MeterTarget>();
const padMeters = new Map<number, Map<symbol, MeterTarget>>();

// "同値" 判定の許容誤差。0.5% (= 0.005) 以下の変化は DOM 書き込みを skip。
// 視覚的にほぼ知覚できないレベルなのでフレーム単位で安全に間引ける。
const FILL_EPS = 0.005;
// これ未満は完全に畳む。1/level が発散するのを防ぐ閾値。
const FILL_MIN = 0.0008;

// ピーク線も transform (translate) で動かす。bottom/left の書き換えは layout/paint
// を誘発するが、translate は compositor のみ。線は full-size 要素の border として
// 描画し、要素自身を translate して位置決めする (CSS 側参照)。
function setPeakPosition(node: HTMLElement, axis: Axis, amount: number) {
  const p = Math.max(0, Math.min(1, amount));
  node.style.transform =
    axis === 'x'
      ? `translateX(${p * 100}%)`        // 左端基準で右へ
      : `translateY(${(1 - p) * 100}%)`; // 上端基準を下へ (= 下から p の高さ)
}

function meterZone(linear: number): 'blue' | 'yellow' | 'red' {
  if (linear >= 10 ** (-1 / 20)) return 'red';
  if (linear >= 10 ** (-6 / 20)) return 'yellow';
  return 'blue';
}

function setFillAmount(target: MeterTarget, amount: number) {
  const a = amount <= 0 ? 0 : amount >= 1 ? 1 : amount;
  const axis = target.axis;
  if (a <= FILL_MIN) {
    if (target.fill)      target.fill.style.transform      = axis === 'x' ? 'scaleX(0)' : 'scaleY(0)';
    if (target.fillInner) target.fillInner.style.transform = axis === 'x' ? 'scaleX(1)' : 'scaleY(1)';
    return;
  }
  const inv = 1 / a;
  if (target.fill)      target.fill.style.transform      = axis === 'x' ? `scaleX(${a})`   : `scaleY(${a})`;
  if (target.fillInner) target.fillInner.style.transform = axis === 'x' ? `scaleX(${inv})` : `scaleY(${inv})`;
}

function updateTarget(target: MeterTarget, level: number, peakHold: number) {
  const now = performance.now();
  if (level >= 1 || peakHold >= 1)
    target.clipHoldUntil = now + 1500;

  const clipHeld = (target.clipHoldUntil ?? 0) > now;
  const meter = level > 0.001 ? levelToMeter(level) : 0;
  const peak = peakHold > 0.001 ? levelToMeter(peakHold) : 0;
  const peakDisplay = clipHeld ? 1 : peak;
  const peakZone = clipHeld || peakHold >= 1 ? 'red' : meterZone(peakHold);
  const fillClip = level >= 1 ? 'true' : 'false';
  const peakShow = peakDisplay > 0;
  const peakClip = clipHeld ? 'true' : 'false';

  if (target.fill) {
    // ── Fill amount: 0.5% 以上の変化があれば書き込み (wrapper + inner の二重 transform)
    if (target._lastFill === undefined || Math.abs(meter - target._lastFill) > FILL_EPS) {
      setFillAmount(target, meter);
      target._lastFill = meter;
    }
    if (target._lastFillClip !== fillClip) {
      target.fill.dataset.clip = fillClip;
      target._lastFillClip = fillClip;
    }
  }

  if (target.peak) {
    if (target._lastPeakShow !== peakShow) {
      target.peak.style.display = peakShow ? '' : 'none';
      target._lastPeakShow = peakShow;
    }
    if (target._lastPeakClip !== peakClip) {
      target.peak.dataset.clip = peakClip;
      target._lastPeakClip = peakClip;
    }
    if (target._lastPeakZone !== peakZone) {
      target.peak.dataset.zone = peakZone;
      target._lastPeakZone = peakZone;
    }
    if (peakShow
        && (target._lastPeak === undefined || Math.abs(peakDisplay - target._lastPeak) > FILL_EPS)) {
      setPeakPosition(target.peak, target.axis, peakDisplay);
      target._lastPeak = peakDisplay;
    }
  }
}

function register(
  map: Map<symbol, MeterTarget>,
  target: MeterTarget,
) {
  const token = Symbol('meter-target');
  map.set(token, target);
  updateTarget(target, 0, 0);
  return () => {
    map.delete(token);
  };
}

export function registerMasterMeter(target: MeterTarget) {
  return register(masterMeters, target);
}

export function registerPadMeter(index: number, target: MeterTarget) {
  let targets = padMeters.get(index);
  if (!targets) {
    targets = new Map();
    padMeters.set(index, targets);
  }

  const unregister = register(targets, target);
  return () => {
    unregister();
    if (targets.size === 0) padMeters.delete(index);
  };
}

export function updateMasterMeter(level: number, peakHold: number) {
  masterMeters.forEach(target => updateTarget(target, level, peakHold));
}

export function updatePadMeter(index: number, level: number, peakHold: number) {
  padMeters.get(index)?.forEach(target => updateTarget(target, level, peakHold));
}

/** 登録中の Pad meter index のセットを返す (flushFrame が可視範囲のみループするため)。 */
export function getRegisteredPadIndices(): number[] {
  return [...padMeters.keys()];
}
