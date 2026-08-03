/**
 * Pad のすべてのパラメータ。
 * JUCE 側の PadData / KitData と将来同期する想定で、フィールド名は揃えてある。
 */
export type PlayMode = 'OneShot' | 'Gate';

/** 波形プレビュー用の符号付き min/max エンベロープ（L/R 共通スケール）。 */
export interface WaveformChannel {
  min: number[];
  max: number[];
  /** 各バケット内の極値順。0=min→max、1=max→min。 */
  extremeOrder?: number[];
}

/**
 * Layer のすべてのパラメータ（1 Pad は複数 Layer を持てる）。
 * JUCE 側の LayerData と同期する。
 *
 * 信号フロー:
 *   Layer Vol/Pan/Pitch → Pad Vol/Pan → Output Routing → Main/Multi Out
 */
export interface LayerParams {
  // サンプル参照
  sampleFileName: string;
  sampleFilePath: string;
  sampleMissing?: boolean;
  /** 表示名（空ならサンプル名 / "Layer N" を UI 側で派生表示） */
  layerName?: string;

  // ミキサー（Layer 単位）
  volume: number;       // 0..1 (fader position)
  pan: number;          // -1..1
  pitch: number;        // -24..24 semitones
  fine: number;         // -100..100 cents

  // エンベロープ
  attack: number;       // sec
  release: number;      // sec

  // トリム
  startMs: number;
  endMs: number;
  fadeInMs: number;
  fadeOutMs: number;
  sampleLengthMs?: number;
  waveformPeaks?: number[];
  waveformChannels?: WaveformChannel[];

  // 再生
  reverse: boolean;
  smartTrim: boolean;

  // Layer 単位 Mute / Solo（Pad の Mute/Solo とは独立）
  // 優先順位: Pad.mute > Layer.solo（同 Pad 内） > Layer.mute
  mute: boolean;
  solo: boolean;

  // Velocity Range（0..127、デフォルトは 0..127 = 全範囲）
  velocityMin: number;
  velocityMax: number;

  /**
   * 極性反転 (Polarity Invert / "Ø")。
   * true なら該当 Layer の再生音を sample *= -1 で反転する。
   * Layer 単位で保持し、他 Layer / 他 Pad には影響しない。
   * 省略時は false 扱い。
   */
  polarityInvert?: boolean;

  /**
   * Layer 固定 4 band EQ。省略時はニュートラル EQ。
   */
  eq?: EqParams;

  /** Legacy v7 prototype storage. Migrated to `eq` on read. */
  fxChain?: FxSlot[];
}

/** FX Chain の 1 枠。type ごとに params 形状が変わる discriminated union。 */
export type FxSlot =
  | { type: 'EQ'; bypassed: boolean; params: EqParams }
  | { type: 'FILTER'; bypassed: boolean; params: FilterParams }
  | { type: 'DRIVE'; bypassed: boolean; params: DriveParams }
  | { type: 'TRANSIENT'; bypassed: boolean; params: TransientParams }
  | { type: 'COMPRESSOR'; bypassed: boolean; params: CompressorParams };

/** フィルターの傾き (dB/oct)。12=1段, 24=2段, 48=4段の biquad カスケード。 */
export type FilterSlope = 12 | 24 | 48;

export interface FilterParams {
  // HP / LP は完全に独立。傾き(slope)も Q(resonance)もセクションごとに持つ。
  // 両方 ON で band-pass 的な挙動になる。
  hpEnabled: boolean;
  hpCutoff: number;
  hpSlope: FilterSlope;
  hpResonance: number;
  lpEnabled: boolean;
  lpCutoff: number;
  lpSlope: FilterSlope;
  lpResonance: number;
}

export interface DriveParams {
  type?: DriveType;
  amount: number;
  tone: number;
  mix: number;
  output?: number;
}

export type DriveType =
  | 'SOFT_CLIP'
  | 'HARD_CLIP'
  | 'TUBE'
  | 'TAPE'
  | 'FOLD'
  | 'BIT_CRUSH'
  | 'DOWNSAMPLE';

export interface TransientParams {
  attack: number;
  sustain: number;
  output: number;
}

export interface CompressorParams {
  threshold: number;
  ratio: number;
  attack: number;
  release: number;
  makeup: number;
  mix: number;
  output: number;
}

/** ASTER シンプル 4 band EQ パラメータ。Low/High は shelf、Mid は bell。 */
export interface EqParams {
  bypassed: boolean;
  lowMode: EqEdgeMode;
  highMode: EqEdgeMode;
  low: { freq: number; gain: number; q: number };
  lowMid: { freq: number; gain: number; q: number };
  highMid: { freq: number; gain: number; q: number };
  high: { freq: number; gain: number; q: number };
}

export type EqSlope = 6 | 12 | 24 | 48;
export type EqEdgeMode = 'shelf' | 'cut';

/** ニュートラル EQ 初期値 (音に影響しない安全値)。 */
export const NEUTRAL_EQ: EqParams = {
  bypassed: false,
  lowMode: 'shelf',
  highMode: 'shelf',
  low:     { freq: 120,   gain: 0, q: 0.7 },
  lowMid:  { freq: 450,   gain: 0, q: 1.0 },
  highMid: { freq: 2400,  gain: 0, q: 1.0 },
  high:    { freq: 10000, gain: 0, q: 0.7 },
};

/** EQ がニュートラル状態(音に影響しない) かどうか。DSP スキップ判定にも使う。 */
export function isEqNeutral(eq: EqParams): boolean {
  return (
    eq.bypassed ||
    (eq.lowMode === 'shelf' &&
     eq.highMode === 'shelf' &&
     Math.abs(eq.low.gain) <= 0.01 &&
     Math.abs(eq.lowMid.gain) <= 0.01 &&
     Math.abs(eq.highMid.gain) <= 0.01 &&
     Math.abs(eq.high.gain) <= 0.01)
  );
}

export interface PadParams {
  // 名前管理（最重要: padName は自動変更しない、JUCE 側と同じ仕様）
  padName: string;
  sampleFileName: string;
  sampleFilePath: string;   // フルパス（UI 補助情報）。空文字 = 未ロード
  sampleMissing?: boolean;  // Kit 読み込み時に見つからないサンプル
  originalSampleFilePath?: string;

  /**
   * Output 名。
   * 通常は Pad Name に追従するが、ユーザーが個別に上書きできる。
   * 空文字 / undefined の場合は padName をそのまま使う（outputDisplayName ヘルパー参照）
   */
  outputName?: string;

  // 見た目: Pad のカテゴリーカラー（KICK=赤系, HAT=シアン系 など）
  // padColor はユーザーが個別に上書きしたい場合に使う（未指定なら categoryColor を使う）
  categoryColor: string;    // 例: '#c66a55'
  padColor?: string;        // ユーザー上書き

  // MIDI
  midiNote: number;        // 0..127

  // 音量・パン・ピッチ
  volume: number;          // 0..1
  pan: number;             // -1..1
  pitch: number;           // -24..24 semitones
  fine: number;            // -100..100 cents (selected Layer mirror)

  // エンベロープ
  attack: number;          // sec
  release: number;         // sec

  // トリム
  sampleLengthMs?: number; // ms。JUCE 側から実サンプル長が来る場合はそれを使う
  waveformPeaks?: number[]; // 0..1 の軽量ピーク列。空/未定義なら波形なし
  waveformChannels?: WaveformChannel[]; // mono=1、stereo=2 の符号付き min/max 波形
  startMs: number;         // ms
  endMs: number;           // ms
  fadeInMs: number;
  fadeOutMs: number;

  // モード
  playMode: PlayMode;
  chokeGroup: number;      // 0..4
  reverse: boolean;
  smartTrim: boolean;

  // ミキサー
  mute: boolean;
  solo: boolean;

  // パフォーマンス
  velocitySens: number;    // 0..1
  humanize: number;        // 0..1

  // 出力
  outputAssign: number;    // 0..47 → "Out 1" ... "Out 48"
  swapLR?: boolean;        // Pad出力の左右チャンネルを交換

  /**
   * Pad-level Volume（Layer Volume の上位段、Q3: 線形掛け算）
   * デフォルト 0.75 = 0 dB unity。古いプリセットでは省略可能（unity扱い）。
   */
  padVolume?: number;
  /** Pad-level Pan。デフォルト 0 = center */
  padPan?: number;
  /** Pad-level Pitch。全 Layer の相対音程を保ったまま移調する */
  padPitch?: number;
  /** Pad-level Fine Tune。全 Layer を -100..100 cents の範囲で微調整する */
  padFine?: number;

  /**
   * Layer 一覧。1 Pad は常に最低 1 Layer を持つ。
   * undefined または空配列の場合、UI は既存の flat fields
   * (sampleFilePath / volume / pan / ...) を「Layer 0」として扱う
   * （Step 5 で実装する読み取り側のフォールバック）。
   */
  layers?: LayerParams[];

  /** UI 状態: 現在選択中の Layer インデックス（0..layers.length-1） */
  selectedLayerIndex?: number;

  /**
   * Polyphony: この Pad で同時に鳴らせる最大ボイス数。
   * 0 = Unlimited (上限なし、グローバル MAX_VOICES だけが効く)
   * 1..16 = 上限。新規発音時に該当 Pad で既にこの数だけ鳴っていれば voiceSteal で処理。
   */
  polyphony?: number;
  /**
   * Polyphony 上限到達時の挙動:
   * 'oldest'   = 最古のボイスを止める (default)
   * 'quietest' = 最も小さいボイスを止める
   * 'off'      = 新規発音を捨てる (古い音は鳴り続ける)
   */
  voiceSteal?: 'oldest' | 'quietest' | 'off';

  /**
   * Layer-only な EQ / FX Chain / Polarity Invert を patch 経由で routeLayerPatch に
   * 流せるよう、型上だけ Pad 側にも optional で生やしておく。
   * 実体は常に layers[idx] 側で保持する (composePadView では mirror しない)。
   */
  eq?: EqParams;
  fxChain?: FxSlot[];
  polarityInvert?: boolean;

  /**
   * VEL Curve: ベロシティ → 音量カーブ。Pad 単位で保持する。
   * - 起点 (0,0) / 終点 (1,1) は固定、可動ポイント 2 個 (p1 / p2)
   * - preset 名は UI 表示専用 (再選択 / 表示 / Custom 判定用)
   * 省略時はリニア(Linear preset と同じ)扱い。
   */
  velCurve?: VelCurveState;
}

/* ── VEL Curve ───────────────────────────────────────────────────── */
export type VelCurvePreset = 'Linear' | 'Soft' | 'Hard' | 'Less Dynamics' | 'More Dynamics' | 'Custom';

export interface VelCurvePoint {
  /** 入力ベロシティ (0..1) */
  x: number;
  /** 出力レベル (0..1) */
  y: number;
}

export interface VelCurveState {
  preset: VelCurvePreset;
  /** p1.x < p2.x が常に成り立つよう UI 側で制限する。 */
  p1: VelCurvePoint;
  p2: VelCurvePoint;

  /**
   * 直近に発音された MIDI velocity (0..127)。C++ から broadcast される transient な値。
   * VelocityRangeSlider の「Live velocity マーカー」表示に使う。
   * 保存対象ではないので Kit JSON には含めない (juceBridge で stripping)。
   */
  lastTriggerVelocity?: number | null;
}

export interface PreviewPlayback {
  isPreviewPlaying: boolean;
  padIndex: number;
  previewStartedAt: number;
  previewDurationMs: number;
  previewStartPercent: number;
  previewEndPercent: number;
  reverseEnabled: boolean;
  triggerId: number;
}

export interface ResourceStats {
  cpuPercent: number;
  cpuAmount: number;
  cpuLabel: string;
  memMb: number;
  memAmount: number;
  memLabel: string;
}

export type KitPage = 'A' | 'B' | 'C';

/**
 * 出力モード（プラグイン全体）。
 * - Stereo: メイン LR のみ
 * - 16Outs / 32Outs / 48Outs: 個別ステレオ出力数
 */
export type OutputMode = 'Stereo' | '16Outs' | '32Outs' | '48Outs';

export function outputCountOf(mode: OutputMode): number {
  switch (mode) {
    case 'Stereo': return 1;
    case '16Outs': return 16;
    case '32Outs': return 32;
    case '48Outs': return 48;
  }
}

/**
 * 表示用に派生する値 (UI 専用)
 */
export interface PadDerivedView {
  /** 既存パッドにサンプルが入っているか */
  hasSample: boolean;
}
