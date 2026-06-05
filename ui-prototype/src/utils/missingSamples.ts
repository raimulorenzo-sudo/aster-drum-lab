import type { PadParams } from '../types';

export function isMissingSamplePad(pad: PadParams): boolean {
  return Boolean(pad.sampleMissing && pad.sampleFilePath);
}

export function countMissingSamples(pads: PadParams[]): number {
  return pads.reduce((count, pad) => count + (isMissingSamplePad(pad) ? 1 : 0), 0);
}
