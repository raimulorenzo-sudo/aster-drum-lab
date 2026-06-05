import { formatFaderDb, formatFaderDbWithUnit, positionToDb } from './fader';

export function clamp(value: number, min: number, max: number) {
  return Math.max(min, Math.min(max, value));
}

/**
 * IMPORTANT — pad.volume / master knob value semantics:
 *   These are FADER POSITIONS in [0..1], NOT linear gains.
 *   See utils/fader.ts for the canonical curve definition (mirrored in
 *   Source/FaderCurve.h). Anything that interprets the value as gain or
 *   converts to/from dB must go through fader.ts so the knob position,
 *   dB label, and scale marks stay in lockstep.
 */

/** Position [0..1] → dB (used by old callers that asked for "volumeToDb"). */
export function volumeToDb(position: number) {
  return positionToDb(position);
}

/** Identity — left for back-compat (pad.volume *is* the fader position). */
export function volumeToFader(position: number) {
  return clamp(position, 0, 1);
}

/** Identity — left for back-compat. */
export function faderToVolume(position: number) {
  return clamp(position, 0, 1);
}

/** "+0.0 dB" / "-12.4 dB" / "-inf dB" formatted from a fader position. */
export function formatVolume(position: number) {
  return formatFaderDbWithUnit(position);
}

/** Re-export for components that just want the numeric label (no "dB"). */
export { formatFaderDb };

export function formatPan(value: number) {
  if (Math.abs(value) < 0.005) return 'C';
  return `${value < 0 ? 'L' : 'R'} ${Math.round(Math.abs(value) * 100)}`;
}

export function formatPitch(value: number) {
  return `${value >= 0 ? '+' : ''}${value.toFixed(0)} st`;
}

export function formatMs(value: number, digits = 1) {
  return `${value.toFixed(digits)} ms`;
}

export function formatPercent(value: number, digits = 1) {
  return `${(value * 100).toFixed(digits)} %`;
}

export function formatTrimPercent(ms: number, totalMs: number) {
  return formatPercent(totalMs > 0 ? ms / totalMs : 0);
}

/**
 * Convert linear amplitude (0..1+) to meter-bar fill position (0..1).
 * Uses the (db + 60) / 60 dBFS scale so that:
 *   0.0 → 0   (-∞ dB, bottom)
 *   -18 dBFS → 0.7
 *   -6 dBFS  → 0.9
 *   -1 dBFS  → 0.983
 *   0 dBFS+  → 1.0 / clip
 */
export function levelToMeter(linear: number): number {
  if (linear <= 0.001) return 0;
  return Math.max(0, Math.min(1, (20 * Math.log10(linear) + 60) / 60));
}
