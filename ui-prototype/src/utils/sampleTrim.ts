import type { PadParams } from '../types';

export const FALLBACK_SAMPLE_LENGTH_MS = 2000;
export const MIN_TRIM_RANGE_MS = 1;

export type TrimField = 'startMs' | 'endMs' | 'fadeInMs' | 'fadeOutMs';

export interface SampleTrimState {
  sampleLengthMs: number;
  startMs: number;
  endMs: number;
  fadeInMs: number;
  fadeOutMs: number;
}

type TrimPatch = Partial<Pick<PadParams,
  'sampleLengthMs' | 'startMs' | 'endMs' | 'fadeInMs' | 'fadeOutMs'
>>;

export function clamp(value: number, min: number, max: number) {
  if (max < min) return min;
  return Math.max(min, Math.min(max, value));
}

function finiteOr(value: number | undefined, fallback: number) {
  return Number.isFinite(value) ? (value as number) : fallback;
}

export function getPadSampleLengthMs(pad: Pick<PadParams, 'sampleLengthMs' | 'endMs'>) {
  const explicitLength = finiteOr(pad.sampleLengthMs, 0);
  const lengthFromEnd = finiteOr(pad.endMs, 0);
  return Math.max(MIN_TRIM_RANGE_MS * 2, explicitLength, lengthFromEnd);
}

export function hasSampleTrimPatch(patch: TrimPatch) {
  return patch.sampleLengthMs !== undefined ||
    patch.startMs !== undefined ||
    patch.endMs !== undefined ||
    patch.fadeInMs !== undefined ||
    patch.fadeOutMs !== undefined;
}

export function inferTrimField(patch: TrimPatch): TrimField | 'all' {
  const keys: TrimField[] = ['startMs', 'endMs', 'fadeInMs', 'fadeOutMs'];
  const changed = keys.filter(key => patch[key] !== undefined);
  return changed.length === 1 ? changed[0] : 'all';
}

export function trimFromPad(pad: PadParams): SampleTrimState {
  const sampleLengthMs = getPadSampleLengthMs(pad);
  return normalizeSampleTrim({
    sampleLengthMs,
    startMs: pad.startMs,
    endMs: pad.endMs,
    fadeInMs: pad.fadeInMs,
    fadeOutMs: pad.fadeOutMs,
  }, 'all');
}

export function normalizeSampleTrim(
  input: SampleTrimState,
  changed: TrimField | 'all' = 'all',
): SampleTrimState {
  const sampleLengthMs = Math.max(MIN_TRIM_RANGE_MS * 2, finiteOr(input.sampleLengthMs, FALLBACK_SAMPLE_LENGTH_MS));
  let startMs = finiteOr(input.startMs, 0);
  let endMs = finiteOr(input.endMs, sampleLengthMs);
  let fadeInMs = finiteOr(input.fadeInMs, 0);
  let fadeOutMs = finiteOr(input.fadeOutMs, 0);

  fadeInMs = clamp(fadeInMs, 0, sampleLengthMs);
  fadeOutMs = clamp(fadeOutMs, 0, sampleLengthMs);

  if (changed === 'startMs') {
    endMs = clamp(endMs, MIN_TRIM_RANGE_MS, sampleLengthMs);
    const requiredRange = Math.max(MIN_TRIM_RANGE_MS, fadeInMs + fadeOutMs);
    startMs = clamp(startMs, 0, endMs - requiredRange);
  } else if (changed === 'endMs') {
    startMs = clamp(startMs, 0, sampleLengthMs - MIN_TRIM_RANGE_MS);
    const requiredRange = Math.max(MIN_TRIM_RANGE_MS, fadeInMs + fadeOutMs);
    endMs = clamp(endMs, startMs + requiredRange, sampleLengthMs);
  } else {
    startMs = clamp(startMs, 0, sampleLengthMs - MIN_TRIM_RANGE_MS);
    endMs = clamp(endMs, startMs + MIN_TRIM_RANGE_MS, sampleLengthMs);
  }

  const rangeMs = Math.max(MIN_TRIM_RANGE_MS, endMs - startMs);

  if (changed === 'fadeInMs') {
    fadeOutMs = clamp(fadeOutMs, 0, rangeMs);
    fadeInMs = clamp(fadeInMs, 0, Math.max(0, rangeMs - fadeOutMs));
  } else if (changed === 'fadeOutMs') {
    fadeInMs = clamp(fadeInMs, 0, rangeMs);
    fadeOutMs = clamp(fadeOutMs, 0, Math.max(0, rangeMs - fadeInMs));
  } else if (changed === 'startMs' || changed === 'endMs') {
    fadeInMs = clamp(fadeInMs, 0, rangeMs);
    fadeOutMs = clamp(fadeOutMs, 0, rangeMs);
    if (fadeInMs + fadeOutMs > rangeMs) {
      fadeOutMs = clamp(fadeOutMs, 0, Math.max(0, rangeMs - fadeInMs));
    }
  } else {
    fadeInMs = clamp(fadeInMs, 0, rangeMs);
    fadeOutMs = clamp(fadeOutMs, 0, Math.max(0, rangeMs - fadeInMs));
  }

  return { sampleLengthMs, startMs, endMs, fadeInMs, fadeOutMs };
}

export function normalizePadTrimPatch(
  currentPad: PadParams,
  patch: TrimPatch,
  changed: TrimField | 'all' = inferTrimField(patch),
): SampleTrimState {
  const current = trimFromPad(currentPad);
  return normalizeSampleTrim({
    sampleLengthMs: finiteOr(patch.sampleLengthMs, current.sampleLengthMs),
    startMs: finiteOr(patch.startMs, current.startMs),
    endMs: finiteOr(patch.endMs, current.endMs),
    fadeInMs: finiteOr(patch.fadeInMs, current.fadeInMs),
    fadeOutMs: finiteOr(patch.fadeOutMs, current.fadeOutMs),
  }, changed);
}

export function trimToNormalized(trim: SampleTrimState) {
  const sampleLengthMs = Math.max(MIN_TRIM_RANGE_MS, trim.sampleLengthMs);
  const rangeMs = Math.max(MIN_TRIM_RANGE_MS, trim.endMs - trim.startMs);

  return {
    startPosition: clamp(trim.startMs / sampleLengthMs, 0, 1),
    endPosition: clamp(trim.endMs / sampleLengthMs, 0, 1),
    fadeIn: clamp(trim.fadeInMs / rangeMs, 0, 1),
    fadeOut: clamp(trim.fadeOutMs / rangeMs, 0, 1),
  };
}
