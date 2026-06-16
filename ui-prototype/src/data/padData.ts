import type { PadParams, KitPage } from '../types';

// Default pad colors: 16 muted colors that repeat across all 3 pages.
// Assignment: DEFAULT_PAD_COLORS[padIndex % 16]
const DEFAULT_PAD_COLORS = [
  '#3F5870', // 0  Muted Steel Blue
  '#A8501F', // 1  Burnt Orange
  '#2F4F35', // 2  Deep Forest Green
  '#8A3F5C', // 3  Dusty Mauve Pink
  '#B57919', // 4  Ochre Yellow
  '#6B4B63', // 5  Dusty Purple
  '#3D6F78', // 6  Steel Cyan
  '#8F3838', // 7  Muted Brick Red
  '#245C5C', // 8  Dark Teal
  '#A65A3E', // 9  Muted Terracotta
  '#342B49', // 10 Dark Plum Indigo
  '#5C6F2F', // 11 Olive Green
  '#8A4650', // 12 Rose Brown
  '#526A7A', // 13 Cool Blue Gray
  '#7A4F1F', // 14 Warm Golden Brown
  '#4F5558', // 15 Cool Gray
] as const;

export const MIDI_NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'] as const;

export function clampMidiNote(note: number): number {
  return Math.max(0, Math.min(127, Math.round(note)));
}

export function defaultMidiNoteForPad(padIndex: number): number {
  return clampMidiNote(36 + padIndex);
}

export function midiNoteName(midiNote: number): string {
  const note = clampMidiNote(midiNote);
  return `${MIDI_NOTE_NAMES[note % 12]}${Math.floor(note / 12) - 2}`;
}

export function midiNoteParts(midiNote: number): { octave: number; noteIndex: number } {
  const note = clampMidiNote(midiNote);
  return {
    octave: Math.floor(note / 12) - 2,
    noteIndex: note % 12,
  };
}

export function midiNoteFromParts(octave: number, noteIndex: number): number {
  return clampMidiNote((octave + 2) * 12 + noteIndex);
}

/**
 * Page A (1..16) — Basic Kit
 */
const PAGE_A: Array<{ padName: string; sampleFileName: string; midiNote: number; extra?: Partial<PadParams> }> = [
  { padName: 'Kick',       sampleFileName: '',  midiNote: 36 },
  { padName: 'Rim',        sampleFileName: '',  midiNote: 37 },
  { padName: 'Snare',      sampleFileName: '',  midiNote: 38 },
  { padName: 'Clap',       sampleFileName: '',  midiNote: 39 },
  { padName: 'Closed Hat', sampleFileName: '',  midiNote: 40 },
  { padName: 'Open Hat',   sampleFileName: '',  midiNote: 41 },
  { padName: 'Snap',       sampleFileName: '',  midiNote: 42 },
  { padName: 'Shaker',     sampleFileName: '',  midiNote: 43 },
  { padName: 'Floor Tom',  sampleFileName: '',  midiNote: 44 },
  { padName: 'Low Tom',    sampleFileName: '',  midiNote: 45 },
  { padName: 'Mid Tom',    sampleFileName: '',  midiNote: 46 },
  { padName: 'Hi Tom',     sampleFileName: '',  midiNote: 47 },
  { padName: 'Ride',       sampleFileName: '',  midiNote: 48 },
  { padName: 'Crash',      sampleFileName: '',  midiNote: 49 },
  { padName: 'Perc 1',     sampleFileName: '',  midiNote: 50 },
  { padName: 'FX',         sampleFileName: '',  midiNote: 51 },
];

/**
 * Page B (17..32) — Modern / Alt Kit
 */
const PAGE_B: Array<{ padName: string; sampleFileName: string; midiNote: number; extra?: Partial<PadParams> }> = [
  { padName: '808',        sampleFileName: '',  midiNote: 52 },
  { padName: 'Kick 2',     sampleFileName: '',  midiNote: 53 },
  { padName: 'Snare 2',    sampleFileName: '',  midiNote: 54 },
  { padName: 'Clap 2',     sampleFileName: '',  midiNote: 55 },
  { padName: 'Hat 2',      sampleFileName: '',  midiNote: 56 },
  { padName: 'Hat 3',      sampleFileName: '',  midiNote: 57 },
  { padName: 'Perc 2',     sampleFileName: '',  midiNote: 58 },
  { padName: 'Stick',      sampleFileName: '',  midiNote: 59 },
  { padName: 'Tamb',       sampleFileName: '',  midiNote: 60 },
  { padName: 'Cabasa',     sampleFileName: '',  midiNote: 61 },
  { padName: 'Cowbell',    sampleFileName: '',  midiNote: 62 },
  { padName: 'Bell',       sampleFileName: '',  midiNote: 63 },
  { padName: 'Perc 3',     sampleFileName: '',  midiNote: 64 },
  { padName: 'Perc 4',     sampleFileName: '',  midiNote: 65 },
  { padName: 'Noise',      sampleFileName: '',  midiNote: 66 },
  { padName: 'FX 2',       sampleFileName: '',  midiNote: 67 },
];

/**
 * Page C (33..48) — FX / Vox / Extra
 */
const PAGE_C: Array<{ padName: string; sampleFileName: string; midiNote: number; extra?: Partial<PadParams> }> = [
  { padName: 'Vox 1',      sampleFileName: '',  midiNote: 68 },
  { padName: 'Vox 2',      sampleFileName: '',  midiNote: 69 },
  { padName: 'Chop 1',     sampleFileName: '',  midiNote: 70 },
  { padName: 'Chop 2',     sampleFileName: '',  midiNote: 71 },
  { padName: 'Hit 1',      sampleFileName: '',  midiNote: 72 },
  { padName: 'Hit 2',      sampleFileName: '',  midiNote: 73 },
  { padName: 'Impact',     sampleFileName: '',  midiNote: 74 },
  { padName: 'Riser',      sampleFileName: '',  midiNote: 75 },
  { padName: 'Down FX',    sampleFileName: '',  midiNote: 76 },
  { padName: 'Sweep',      sampleFileName: '',  midiNote: 77 },
  { padName: 'Texture',    sampleFileName: '',  midiNote: 78 },
  { padName: 'Noise 2',    sampleFileName: '',  midiNote: 79 },
  { padName: 'Fill 1',     sampleFileName: '',  midiNote: 80 },
  { padName: 'Fill 2',     sampleFileName: '',  midiNote: 81 },
  { padName: 'Extra 1',    sampleFileName: '',  midiNote: 82 },
  { padName: 'Extra 2',    sampleFileName: '',  midiNote: 83 },
];

const ALL_SEEDS = [...PAGE_A, ...PAGE_B, ...PAGE_C];

function defaultPad(i: number): PadParams {
  return {
    padName: 'PAD',
    sampleFileName: '',
    sampleFilePath: '',
    categoryColor: DEFAULT_PAD_COLORS[i % 16],
    midiNote: 36 + i,
    volume: 0.75,  // fader position 0..1 — 0.75 == 0 dB unity (utils/fader.ts)
    padVolume: 0.75,
    padPan: 0,
    padPitch: 0,
    padFine: 0,
    pan: 0,
    pitch: 0,
    fine: 0,
    attack: 0.002,
    release: 0.05,
    sampleLengthMs: 500,
    waveformPeaks: [],
    startMs: 0,
    endMs: 500,
    fadeInMs: 0,
    fadeOutMs: 0,
    playMode: 'OneShot',
    chokeGroup: 0,
    reverse: false,
    smartTrim: true,
    mute: false,
    solo: false,
    velocitySens: 1.0,
    humanize: 0,
    outputAssign: 0,
    swapLR: false,
  };
}

export const INITIAL_PADS: PadParams[] = ALL_SEEDS.map((seed, i) => ({
  ...defaultPad(i),
  padName: seed.padName,
  sampleFileName: '',
  sampleFilePath: '',
  midiNote: defaultMidiNoteForPad(i),
  categoryColor: DEFAULT_PAD_COLORS[i % 16],
}));

/**
 * Page → 表示する 16 Pad の絶対 index 範囲 [start, end)
 */
export function pageRange(page: KitPage): [number, number] {
  const offset = page === 'A' ? 0 : page === 'B' ? 16 : 32;
  return [offset, offset + 16];
}

export function pageOffset(page: KitPage): number {
  return pageRange(page)[0];
}

/**
 * 絶対 index (0..47) → ページ
 */
export function pageOfIndex(i: number): KitPage {
  if (i < 16) return 'A';
  if (i < 32) return 'B';
  return 'C';
}

/**
 * Pad index (0..47) → 表示用の音階名。
 * MIDIトリガー用の pad.midiNote とは独立して、Pad位置だけから計算する。
 */
export function padNoteName(padIndex: number): string {
  if (padIndex < 0 || padIndex >= 48) return '–';
  return midiNoteName(defaultMidiNoteForPad(padIndex));
}

/**
 * 出力アサインのラベル (Logic Pro のステレオペア表記)
 */
export function outputLabel(idx: number): string {
  return `${idx * 2 + 1}-${idx * 2 + 2}`;
}

/**
 * Output Name の最終表示文字列。
 * outputName が指定されていればそれを、無ければ padName をそのまま使う。
 * （仕様: サンプル読み込みで Output Name は変わらない）
 */
export function outputDisplayName(pad: PadParams): string {
  return (pad.outputName && pad.outputName.length > 0) ? pad.outputName : pad.padName;
}

/**
 * UI で使う Pad の表示色（user override > category）
 */
export function padDisplayColor(pad: PadParams): string {
  return pad.padColor ?? pad.categoryColor;
}
