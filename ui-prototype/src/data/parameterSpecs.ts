import type { PadParams } from '../types';

export type AutomatablePadParam =
  | 'volume'
  | 'pan'
  | 'pitch'
  | 'attack'
  | 'release'
  | 'startMs'
  | 'endMs'
  | 'fadeInMs'
  | 'fadeOutMs'
  | 'reverse'
  | 'mute'
  | 'solo';

export type PadParameterKey = AutomatablePadParam
  | 'velocitySens'
  | 'humanize'
  | 'chokeGroup'
  | 'outputAssign'
  | 'smartTrim'
  | 'midiNote';

export interface NumericParameterSpec {
  key: PadParameterKey;
  label: string;
  defaultValue: number;
  min: number;
  max: number;
  automation: boolean;
}

export interface BooleanParameterSpec {
  key: PadParameterKey;
  label: string;
  defaultValue: boolean;
  automation: boolean;
}

export type ParameterSpec = NumericParameterSpec | BooleanParameterSpec;

export const PAD_PARAMETER_SPECS = {
  // volume is a FADER POSITION in [0..1]; 0.75 == 0 dB unity (see utils/fader.ts).
  volume: { key: 'volume', label: 'Volume', defaultValue: 0.75, min: 0, max: 1, automation: true },
  pan: { key: 'pan', label: 'Pan', defaultValue: 0, min: -1, max: 1, automation: true },
  pitch: { key: 'pitch', label: 'Pitch', defaultValue: 0, min: -24, max: 24, automation: true },
  attack: { key: 'attack', label: 'Attack', defaultValue: 0.002, min: 0, max: 2, automation: true },
  release: { key: 'release', label: 'Release', defaultValue: 0.05, min: 0, max: 4, automation: true },
  startMs: { key: 'startMs', label: 'Start', defaultValue: 0, min: 0, max: 1, automation: true },
  endMs: { key: 'endMs', label: 'End', defaultValue: 500, min: 0, max: 1, automation: true },
  fadeInMs: { key: 'fadeInMs', label: 'Fade In', defaultValue: 0, min: 0, max: 1, automation: true },
  fadeOutMs: { key: 'fadeOutMs', label: 'Fade Out', defaultValue: 0, min: 0, max: 1, automation: true },
  reverse: { key: 'reverse', label: 'Reverse', defaultValue: false, automation: true },
  mute: { key: 'mute', label: 'Mute', defaultValue: false, automation: true },
  solo: { key: 'solo', label: 'Solo', defaultValue: false, automation: true },

  velocitySens: { key: 'velocitySens', label: 'Velocity', defaultValue: 1, min: 0, max: 1, automation: false },
  humanize: { key: 'humanize', label: 'Humanize', defaultValue: 0, min: 0, max: 1, automation: false },
  chokeGroup: { key: 'chokeGroup', label: 'Choke Group', defaultValue: 0, min: 0, max: 4, automation: false },
  outputAssign: { key: 'outputAssign', label: 'Output Assign', defaultValue: 0, min: 0, max: 47, automation: false },
  smartTrim: { key: 'smartTrim', label: 'Smart Trim', defaultValue: true, automation: false },
  midiNote: { key: 'midiNote', label: 'MIDI Note', defaultValue: 36, min: 0, max: 127, automation: false },
} as const satisfies Record<string, ParameterSpec>;

export const AUTOMATABLE_PAD_PARAMS: AutomatablePadParam[] = [
  'volume',
  'pan',
  'pitch',
  'attack',
  'release',
  'startMs',
  'endMs',
  'fadeInMs',
  'fadeOutMs',
  'reverse',
  'mute',
  'solo',
];

export function defaultPadParam<K extends PadParameterKey>(key: K): PadParams[K] {
  return PAD_PARAMETER_SPECS[key].defaultValue as PadParams[K];
}

export function automationPadLabel(index: number, pad: Pick<PadParams, 'padName'>, key: AutomatablePadParam) {
  const bank = index < 16 ? 'A' : index < 32 ? 'B' : 'C';
  const spec = PAD_PARAMETER_SPECS[key];
  return `${bank}${String(index + 1).padStart(2, '0')} ${pad.padName} ${spec.label}`;
}

export function resettablePadSettings(_padIndex: number): Partial<PadParams> {
  return {
    pan: defaultPadParam('pan'),
    pitch: defaultPadParam('pitch'),
    attack: defaultPadParam('attack'),
    release: defaultPadParam('release'),
    startMs: defaultPadParam('startMs'),
    endMs: defaultPadParam('endMs'),
    fadeInMs: defaultPadParam('fadeInMs'),
    fadeOutMs: defaultPadParam('fadeOutMs'),
    reverse: defaultPadParam('reverse'),
    smartTrim: defaultPadParam('smartTrim'),
    mute: defaultPadParam('mute'),
    solo: defaultPadParam('solo'),
    velocitySens: defaultPadParam('velocitySens'),
    humanize: defaultPadParam('humanize'),
    chokeGroup: defaultPadParam('chokeGroup'),
  };
}
