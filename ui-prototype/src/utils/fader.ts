/**
 * Unified fader curve — position [0..1] ↔ dB ↔ linear gain.
 *
 * The same math is mirrored in Source/FaderCurve.h so JUCE DSP,
 * DAW automation values, preset save/load, and the React UI all share
 * one curve. This guarantees the fader knob position, dB label, and
 * scale marks can never drift apart.
 *
 * Curve specification (dB-based, "audio taper"):
 *
 *     position = 0                  → -Infinity dB  (gain = 0, hard mute — special case)
 *     position → 0⁺                 → -60 dB        (approached asymptotically)
 *     position = UNITY_POS (0.75)   →   0 dB        (unity gain)
 *     position = 1                  →  +12 dB
 *
 * Below unity:   db = -DB_RANGE_BELOW * (1 - pos/UNITY_POS)^CURVE_EXP
 *                — exponent gives finer resolution near 0 dB.
 *
 * Above unity:   db linear in position over (0 dB .. +12 dB)
 *
 * IMPORTANT properties:
 *   - The ONLY position that produces -Infinity dB is exactly pos = 0.
 *     Any positive pos (even 1e-8) yields a finite dB just above -60.
 *   - For UI consumers a small visual "-inf" snap is provided by the
 *     formatFaderDb() threshold (default 1e-4); the underlying numeric
 *     curve never snaps. Audio gain is silent only at exactly pos = 0.
 */

export const FADER_UNITY_POS  = 0.75;     // pos for 0 dB
export const FADER_DB_TOP     = 12;       // top of the rail
export const FADER_DB_UNITY   = 0;        // unity in dB
export const FADER_DB_FLOOR   = -60;      // dB approached as pos → 0⁺
export const FADER_CURVE_EXP  = 2.5;      // shape of the sub-unity curve

/** Threshold below which the dB formatter renders "-inf" instead of a
 *  number. Audio gain itself is only zero at pos === 0 exactly. */
export const FADER_INF_EPSILON = 1e-4;

const DB_RANGE_BELOW = FADER_DB_UNITY - FADER_DB_FLOOR;   // 60
const DB_RANGE_ABOVE = FADER_DB_TOP   - FADER_DB_UNITY;   // 12

/** Fader position [0..1] → dB. Returns -Infinity for pos ≤ 0. */
export function positionToDb(pos: number): number {
  if (pos <= 0) return -Infinity;
  const p = Math.min(1, pos);
  if (p >= FADER_UNITY_POS) {
    const t = (p - FADER_UNITY_POS) / (1 - FADER_UNITY_POS);
    return FADER_DB_UNITY + t * DB_RANGE_ABOVE;
  }
  // p ∈ (0, UNITY_POS): db = -DB_RANGE_BELOW * (1 - p/UNITY)^EXP
  // approaches FADER_DB_FLOOR (= -60) but never reaches it for p > 0.
  const t = p / FADER_UNITY_POS;
  return FADER_DB_UNITY - DB_RANGE_BELOW * Math.pow(1 - t, FADER_CURVE_EXP);
}

/** dB → fader position [0..1]. -Infinity / -60 or below → 0. */
export function dbToPosition(db: number): number {
  if (!isFinite(db) || db <= FADER_DB_FLOOR) return 0;
  if (db >= FADER_DB_TOP) return 1;
  if (db >= FADER_DB_UNITY) {
    return FADER_UNITY_POS + (db - FADER_DB_UNITY) / DB_RANGE_ABOVE * (1 - FADER_UNITY_POS);
  }
  // Inverse of: db = -DB_RANGE_BELOW * (1 - t)^EXP
  // (1 - t)^EXP = -db / DB_RANGE_BELOW   (db < 0 here)
  const ratio = -db / DB_RANGE_BELOW;            // (0, 1]
  const oneMinusT = Math.pow(ratio, 1 / FADER_CURVE_EXP);
  return Math.max(0, (1 - oneMinusT) * FADER_UNITY_POS);
}

/** Fader position [0..1] → linear gain (multiplier). 0 → 0 (silence). */
export function positionToGain(pos: number): number {
  if (pos <= 0) return 0;
  return Math.pow(10, positionToDb(pos) / 20);
}

/** Linear gain → fader position. Used when migrating from legacy
 *  "volume = linear-gain" stored values to the new fader-position scheme. */
export function gainToPosition(gain: number): number {
  if (gain <= 0) return 0;
  if (gain >= Math.pow(10, FADER_DB_TOP / 20)) return 1;
  return dbToPosition(20 * Math.log10(gain));
}

/**
 * Format a fader position as a dB label for the UI.
 *   pos = 0       → '-inf'
 *   pos = 1e-5    → '-inf'  (visual snap; threshold = FADER_INF_EPSILON)
 *   pos = 0.001   → '-59.7'
 *   pos = 0.75    → '+0.0'
 *   pos = 1.0     → '+12.0'
 */
export function formatFaderDb(pos: number, digits = 1): string {
  if (pos <= FADER_INF_EPSILON) return '-inf';
  const db = positionToDb(pos);
  if (!isFinite(db)) return '-inf';
  if (db >= 0) return `+${db.toFixed(digits)}`;
  return db.toFixed(digits);
}

/** Same as formatFaderDb, with " dB" suffix (or "-inf dB"). */
export function formatFaderDbWithUnit(pos: number, digits = 1): string {
  const s = formatFaderDb(pos, digits);
  return s === '-inf' ? '-inf dB' : `${s} dB`;
}

/**
 * Scale marks for the fader rail.
 * Each `position` is computed from the same curve as the knob, so
 * the label "0" always lands at exactly where the knob shows 0 dB.
 */
export interface FaderScaleMark {
  db: number;            // dB value (-Infinity for the bottom)
  label: string;
  /** Position [0..1] from the BOTTOM of the rail. Use as `bottom: pos*100%`. */
  position: number;
}

const SCALE_DB_VALUES: { db: number; label: string }[] = [
  { db: 12,         label: '+12' },
  { db: 6,          label: '+6'  },
  { db: 0,          label: '0'   },
  { db: -6,         label: '-6'  },
  { db: -12,        label: '-12' },
  { db: -24,        label: '-24' },
  { db: -48,        label: '-48' },
  { db: -Infinity,  label: '-∞'  },
];

export const FADER_SCALE_MARKS: FaderScaleMark[] = SCALE_DB_VALUES.map(({ db, label }) => ({
  db,
  label,
  position: dbToPosition(db),
}));
