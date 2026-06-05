import type { EqParams } from '../types';

const SR = 48000;
const TWO_PI = Math.PI * 2;

export function xToFreq(x: number): number {
  const minLog = Math.log10(20);
  const maxLog = Math.log10(20000);
  return Math.pow(10, minLog + (maxLog - minLog) * Math.max(0, Math.min(1, x)));
}

export function freqToX(freq: number): number {
  const minLog = Math.log10(20);
  const maxLog = Math.log10(20000);
  const f = Math.max(20, Math.min(20000, freq));
  return (Math.log10(f) - minLog) / (maxLog - minLog);
}

export function dbToY(db: number, range = 18): number {
  const clamped = Math.max(-range, Math.min(range, db));
  return 0.5 - clamped / (2 * range);
}

export function yToDb(y: number, range = 18): number {
  return (0.5 - y) * (2 * range);
}

function lin2db(v: number): number {
  if (v <= 1e-9) return -120;
  return 20 * Math.log10(v);
}

function biquadResponseDb(
  f: number,
  b0: number,
  b1: number,
  b2: number,
  a0: number,
  a1: number,
  a2: number,
): number {
  const w = TWO_PI * f / SR;
  const cw = Math.cos(w);
  const c2w = Math.cos(2 * w);
  const sw = Math.sin(w);
  const s2w = Math.sin(2 * w);

  const numRe = b0 + b1 * cw + b2 * c2w;
  const numIm = -(b1 * sw + b2 * s2w);
  const denRe = a0 + a1 * cw + a2 * c2w;
  const denIm = -(a1 * sw + a2 * s2w);

  return lin2db(Math.hypot(numRe, numIm) / Math.max(1e-12, Math.hypot(denRe, denIm)));
}

function peakResponseDb(f: number, fc: number, gainDb: number, q: number): number {
  if (Math.abs(gainDb) < 0.01) return 0;
  const A = Math.pow(10, gainDb / 40);
  const w0 = TWO_PI * Math.max(20, Math.min(20000, fc)) / SR;
  const alpha = Math.sin(w0) / (2 * Math.max(0.2, q));
  const cosw = Math.cos(w0);
  return biquadResponseDb(
    f,
    1 + alpha * A,
    -2 * cosw,
    1 - alpha * A,
    1 + alpha / A,
    -2 * cosw,
    1 - alpha / A,
  );
}

function lowShelfResponseDb(f: number, fc: number, gainDb: number, q: number): number {
  if (Math.abs(gainDb) < 0.01) return 0;
  const A = Math.pow(10, gainDb / 40);
  const w0 = TWO_PI * Math.max(20, Math.min(20000, fc)) / SR;
  const c = Math.cos(w0);
  const alpha = Math.sin(w0) / (2 * Math.max(0.2, q));
  const beta = 2 * Math.sqrt(A) * alpha;
  return biquadResponseDb(
    f,
    A * ((A + 1) - (A - 1) * c + beta),
    2 * A * ((A - 1) - (A + 1) * c),
    A * ((A + 1) - (A - 1) * c - beta),
    (A + 1) + (A - 1) * c + beta,
    -2 * ((A - 1) + (A + 1) * c),
    (A + 1) + (A - 1) * c - beta,
  );
}

function highShelfResponseDb(f: number, fc: number, gainDb: number, q: number): number {
  if (Math.abs(gainDb) < 0.01) return 0;
  const A = Math.pow(10, gainDb / 40);
  const w0 = TWO_PI * Math.max(20, Math.min(20000, fc)) / SR;
  const c = Math.cos(w0);
  const alpha = Math.sin(w0) / (2 * Math.max(0.2, q));
  const beta = 2 * Math.sqrt(A) * alpha;
  return biquadResponseDb(
    f,
    A * ((A + 1) + (A - 1) * c + beta),
    -2 * A * ((A - 1) + (A + 1) * c),
    A * ((A + 1) + (A - 1) * c - beta),
    (A + 1) - (A - 1) * c + beta,
    2 * ((A - 1) - (A + 1) * c),
    (A + 1) - (A - 1) * c - beta,
  );
}

function highPassResponseDb(f: number, fc: number, q: number): number {
  const w0 = TWO_PI * Math.max(20, Math.min(20000, fc)) / SR;
  const c = Math.cos(w0);
  const alpha = Math.sin(w0) / (2 * Math.max(0.2, q));
  return biquadResponseDb(
    f,
    (1 + c) * 0.5,
    -(1 + c),
    (1 + c) * 0.5,
    1 + alpha,
    -2 * c,
    1 - alpha,
  );
}

function lowPassResponseDb(f: number, fc: number, q: number): number {
  const w0 = TWO_PI * Math.max(20, Math.min(20000, fc)) / SR;
  const c = Math.cos(w0);
  const alpha = Math.sin(w0) / (2 * Math.max(0.2, q));
  return biquadResponseDb(
    f,
    (1 - c) * 0.5,
    1 - c,
    (1 - c) * 0.5,
    1 + alpha,
    -2 * c,
    1 - alpha,
  );
}

export function eqResponseDb(eq: EqParams, freq: number): number {
  if (eq.bypassed) return 0;
  return (
    (eq.lowMode === 'cut'
      ? highPassResponseDb(freq, eq.low.freq, eq.low.q)
      : lowShelfResponseDb(freq, eq.low.freq, eq.low.gain, eq.low.q)) +
    peakResponseDb(freq, eq.lowMid.freq, eq.lowMid.gain, eq.lowMid.q) +
    peakResponseDb(freq, eq.highMid.freq, eq.highMid.gain, eq.highMid.q) +
    (eq.highMode === 'cut'
      ? lowPassResponseDb(freq, eq.high.freq, eq.high.q)
      : highShelfResponseDb(freq, eq.high.freq, eq.high.gain, eq.high.q))
  );
}

export function sampleEqCurve(eq: EqParams, samples = 96): { x: number; db: number }[] {
  const out: { x: number; db: number }[] = [];
  for (let i = 0; i < samples; i++) {
    const x = i / (samples - 1);
    out.push({ x, db: eqResponseDb(eq, xToFreq(x)) });
  }
  return out;
}
