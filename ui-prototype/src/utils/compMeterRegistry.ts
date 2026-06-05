// ─────────────────────────────────────────────────────────────────────────────
// Compressor gain-reduction meter registry.
//
// COMPRESSOR FX モジュールが「FX チェーン上のスロット位置」をキーに GR メーターの
// 塗り要素を登録する。App の rAF ループが C++ から届いた GR (dB) を流し込む。
// meterRegistry と同様、React 再レンダーを介さず DOM へ直接 transform を書く。
// ─────────────────────────────────────────────────────────────────────────────

// GR メーターのフルスケール (dB)。これ以上の reduction は振り切れ表示。
export const COMP_GR_FULL_SCALE_DB = 24;
export const COMP_GR_METER_MARKS = [
  { db: 2,  amount: 0.12 },
  { db: 3,  amount: 0.22 },
  { db: 6,  amount: 0.40 },
  { db: 10, amount: 0.60 },
  { db: 12, amount: 0.76 },
  { db: 24, amount: 0.98 },
];

export function grDbToMeterAmount(grDb: number): number {
  const db = Math.max(0, Math.min(COMP_GR_FULL_SCALE_DB, grDb));
  if (db <= 0) return 0;

  let prev = { db: 0, amount: 0 };
  for (const next of COMP_GR_METER_MARKS) {
    if (db <= next.db) {
      const span = next.db - prev.db;
      const t = span > 0 ? (db - prev.db) / span : 0;
      return prev.amount + (next.amount - prev.amount) * t;
    }
    prev = next;
  }

  return 1;
}

interface CompTarget {
  fill: HTMLElement | null;   // scaleX で伸縮する塗り (transform-origin: left)
  peak: HTMLElement | null;   // 最大到達点をしばらく残すマーカー
  label: HTMLElement | null;  // "-X.X dB" テキスト
  _lastAmt?: number;
  _lastPeakAmt?: number;
  _peakAmt?: number;
  _peakHoldUntil?: number;
  _lastText?: string;
  _lastTextAt?: number;
}

// slotIndex → token → target
const registry = new Map<number, Map<symbol, CompTarget>>();

export function registerCompMeter(slotIndex: number, target: CompTarget): () => void {
  const token = Symbol('comp-meter');
  if (!registry.has(slotIndex)) registry.set(slotIndex, new Map());
  const slotMap = registry.get(slotIndex)!;
  slotMap.set(token, target);
  if (target.fill) target.fill.style.transform = 'scaleX(0)';
  if (target.peak) {
    target.peak.style.display = 'none';
    target.peak.style.transform = 'translateX(0%)';
  }
  if (target.label) target.label.textContent = '0.0 dB';
  return () => {
    slotMap.delete(token);
    if (slotMap.size === 0) registry.delete(slotIndex);
  };
}

/** rAF ループから呼ぶ。grDb = 正値のリダクション量 (dB)。 */
export function updateCompMeter(slotIndex: number, grDb: number): void {
  const slotMap = registry.get(slotIndex);
  if (!slotMap) return;
  const amt = grDbToMeterAmount(grDb);
  const now = performance.now();
  const text = grDb < 0.05 ? '0.0 dB' : `-${grDb.toFixed(1)} dB`;
  slotMap.forEach(t => {
    if (t.fill && (t._lastAmt === undefined || Math.abs(amt - t._lastAmt) > 0.004)) {
      t.fill.style.transform = `scaleX(${amt})`;
      t._lastAmt = amt;
    }
    if (t.peak) {
      const prevPeak = t._peakAmt ?? 0;
      if (amt >= prevPeak) {
        t._peakAmt = amt;
        t._peakHoldUntil = now + 850;
      } else if ((t._peakHoldUntil ?? 0) < now) {
        t._peakAmt = Math.max(0, prevPeak * 0.985);
      }

      const peakAmt = t._peakAmt ?? 0;
      const peakVisible = peakAmt > 0.02;
      t.peak.style.display = peakVisible ? '' : 'none';
      if (peakVisible && (t._lastPeakAmt === undefined || Math.abs(peakAmt - t._lastPeakAmt) > 0.003)) {
        t.peak.style.transform = `translateX(${peakAmt * 100}%)`;
        t._lastPeakAmt = peakAmt;
      }
    }
    const lastNumber = t._lastText ? Number(t._lastText.replace(/[^\d.-]/g, '')) : 0;
    const nextNumber = Number(text.replace(/[^\d.-]/g, ''));
    const textChangedEnough = Math.abs(nextNumber - lastNumber) >= 0.2;
    const textIntervalElapsed = t._lastTextAt === undefined || now - t._lastTextAt >= 90;
    if (t.label && t._lastText !== text && (textChangedEnough || textIntervalElapsed)) {
      t.label.textContent = text;
      t._lastText = text;
      t._lastTextAt = now;
    }
  });
}

/** 登録中スロットの集合 (rAF ループが可視分だけ回すため)。 */
export function getRegisteredCompSlots(): number[] {
  return [...registry.keys()];
}
