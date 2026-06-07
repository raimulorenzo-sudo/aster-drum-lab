import { memo, useCallback, useEffect, useMemo, useRef, useState } from 'react';
import styles from './MixerView.module.css';
import { Knob } from '../Knob/Knob';
import { OutputAssignDropdown } from '../OutputAssignDropdown/OutputAssignDropdown';
import { pageRange } from '../../data/padData';
import { defaultPadParam } from '../../data/parameterSpecs';
import { outputCountOf } from '../../types';
import { clamp, formatPan } from '../../utils/parameterFormat';
import { dbToPosition, positionToDb, FADER_DB_FLOOR, FADER_DB_TOP, FADER_SCALE_MARKS, formatFaderDb } from '../../utils/fader';
import { parseNumericText, parsePanInput } from '../../utils/numericInput';
import type { PadParams, KitPage, OutputMode } from '../../types';
import type { PointerEvent as ReactPointerEvent } from 'react';
import type { MouseEvent as ReactMouseEvent } from 'react';
import { MASTER_UNITY } from '../../App';
import { registerMasterMeter, registerPadMeter } from '../../utils/meterRegistry';
import { registerResourceMeter } from '../../utils/resourceRegistry';
import { registerPadClipNode } from '../../utils/clipRegistry';
import { sendToJuce } from '../../utils/juceBridge';

interface MixerViewProps {
  pads: PadParams[];                                    // 全 48 Pad
  page: KitPage;
  onPageChange: (p: KitPage) => void;
  outputMode: OutputMode;
  onOutputModeChange: (m: OutputMode) => void;
  selectedIndex: number;                                // 絶対 index (0..47)
  onSelect: (index: number) => void;                    // 無音で選択
  onAudition: (index: number) => void;                  // 名前エリアから試聴
  onChangePad: (index: number, patch: Partial<PadParams>) => void;  // 絶対 index
  onChangePads: (changes: { index: number; patch: Partial<PadParams> }[]) => void;  // 一括編集
  masterKnob: number;
  onMasterKnobChange: (v: number) => void;
  masterClipHit: boolean;
  onResetMasterClip: () => void;
}

const BANKS: KitPage[] = ['A', 'B', 'C'];
const OUTPUT_MODES: OutputMode[] = ['Stereo', '16Outs', '32Outs', '48Outs'];
// 60 Hz throttle: フェーダードラッグ中の onChange を 60Hz に。
// (旧 30 Hz だと 2 フレームに 1 回しか反映されず "ぬるっとしない")
const CONTINUOUS_UPDATE_MS = 1000 / 60;
const OUTPUT_MODE_LABEL: Record<OutputMode, string> = {
  Stereo: 'STEREO',
  '16Outs': '16 OUTS',
  '32Outs': '32 OUTS',
  '48Outs': '48 OUTS',
};

type RoutingPreset = 'ALL_MAIN' | 'INDIVIDUAL';
type RoutingStatus = RoutingPreset | 'CUSTOM';

function detectRoutingStatus(pads: PadParams[]): RoutingStatus {
  if (pads.every(pad => pad.outputAssign === 0)) return 'ALL_MAIN';
  if (pads.every((pad, i) => pad.outputAssign === i)) return 'INDIVIDUAL';
  return 'CUSTOM';
}

const ROUTING_STATUS_LABEL: Record<RoutingStatus, string> = {
  ALL_MAIN: 'ALL MAIN',
  INDIVIDUAL: 'INDIVIDUAL',
  CUSTOM: 'CUSTOM',
};

function MixerViewComponent({
  pads,
  page,
  onPageChange,
  outputMode,
  onOutputModeChange,
  selectedIndex,
  onSelect,
  onAudition,
  onChangePad,
  onChangePads,
  masterKnob,
  onMasterKnobChange,
  masterClipHit,
  onResetMasterClip,
}: MixerViewProps) {
  const outputCount = outputCountOf(outputMode);
  const [start, end] = pageRange(page);
  const visiblePads = pads.slice(start, end);
  const routingStatus = useMemo(() => detectRoutingStatus(pads), [pads]);
  const [customRoutingSnapshot, setCustomRoutingSnapshot] = useState<number[] | null>(null);
  const canRecallCustomRouting = customRoutingSnapshot !== null && customRoutingSnapshot.length === pads.length;

  // ── Multi-selection editing (一時的。保存しない) ────────────────────────
  // selection: 選択中チャンネルの絶対 index 集合。空 = 単一 selectedIndex のみ扱う。
  // マウント時は現在の selectedIndex を 1 つだけ選択した状態にして既存挙動を保つ。
  const [selection, setSelection] = useState<Set<number>>(() => new Set([selectedIndex]));
  const anchorRef = useRef<number>(selectedIndex);
  // 最新の pads / selection をドラッグ中ハンドラから参照するための ref
  const padsRef = useRef(pads);
  const selectionRef = useRef(selection);
  useEffect(() => { padsRef.current = pads; }, [pads]);
  useEffect(() => { selectionRef.current = selection; }, [selection]);

  // ドラッグ開始時のスナップショット (相対編集の基準値)
  const gestureRef = useRef<{
    kind: 'volume' | 'pan';
    origin: number;
    batch: boolean;
    startById: Map<number, number>;
  } | null>(null);

  const clearSelection = useCallback(() => setSelection(new Set()), []);

  // Esc で選択解除
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if (e.key === 'Escape') clearSelection();
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [clearSelection]);

  // チャンネル本体クリックの選択ロジック (通常 / Shift 範囲 / Cmd-Ctrl トグル)
  const handleChannelMouseDown = useCallback(
    (index: number, e: ReactMouseEvent) => {
      if (e.shiftKey) {
        const from = anchorRef.current;
        const lo = Math.min(from, index);
        const hi = Math.max(from, index);
        const next = new Set<number>();
        for (let i = lo; i <= hi; i++) next.add(i);
        setSelection(next);
        onSelect(index);
      } else if (e.metaKey || e.ctrlKey) {
        // Set 本体は functional updater で最新 prev から増減 (rapid クリックにも頑健)。
        // 副作用 (onSelect) は render 中 setState を避けるため updater の外で。追加か
        // 削除かの判定は selectionRef を使う (離散クリックでは最新)。
        const wasSelected = selectionRef.current.has(index);
        setSelection(prev => {
          const next = new Set(prev);
          if (next.has(index)) next.delete(index);
          else next.add(index);
          return next;
        });
        if (!wasSelected) onSelect(index);
        anchorRef.current = index;
      } else {
        setSelection(new Set([index]));
        anchorRef.current = index;
        onSelect(index);
      }
    },
    [onSelect],
  );

  const handleChannelAudition = useCallback(
    (index: number, e: ReactMouseEvent) => {
      e.stopPropagation();
      handleChannelMouseDown(index, e);
      onAudition(index);
    },
    [handleChannelMouseDown, onAudition],
  );

  // 編集対象の決定: batch なら選択集合全員、そうでなければ origin 単独 (+ origin を選択化)
  const beginGesture = useCallback(
    (kind: 'volume' | 'pan', origin: number) => {
      const sel = selectionRef.current;
      const batch = sel.has(origin) && sel.size >= 2;
      if (!batch) {
        // 未選択チャンネルの操作 → そのチャンネルのみ選択へ切り替え (単体操作)
        setSelection(new Set([origin]));
        anchorRef.current = origin;
        onSelect(origin);
      }
      const targets = batch ? [...sel] : [origin];
      const startById = new Map<number, number>();
      for (const idx of targets) {
        const p = padsRef.current[idx];
        if (!p) continue;
        startById.set(idx, kind === 'volume' ? (p.padVolume ?? 0.75) : (p.padPan ?? 0));
      }
      gestureRef.current = { kind, origin, batch, startById };
    },
    [onSelect],
  );

  const endGesture = useCallback(() => { gestureRef.current = null; }, []);

  // Volume ドラッグ: origin の新フェーダー位置から dB 相対デルタを全選択へ適用。
  // 音量差 (dB) を維持し、上下端は clamp。基準は gesture 開始時のスナップショット
  // なので、端に当たって戻すと差し引きが復元する。
  const commitVolume = useCallback(
    (origin: number, newPos: number) => {
      const g = gestureRef.current;
      if (!g || g.kind !== 'volume' || !g.startById.has(origin)) {
        onChangePad(origin, { padVolume: newPos });
        return;
      }
      const startOrigin = g.startById.get(origin)!;
      const deltaDb = positionToDb(newPos) - positionToDb(startOrigin);
      const changes: { index: number; patch: Partial<PadParams> }[] = [];
      g.startById.forEach((startV, idx) => {
        if (idx === origin) {
          changes.push({ index: idx, patch: { padVolume: newPos } });
        } else {
          const db = Math.max(FADER_DB_FLOOR, Math.min(FADER_DB_TOP, positionToDb(startV) + deltaDb));
          changes.push({ index: idx, patch: { padVolume: dbToPosition(db) } });
        }
      });
      onChangePads(changes);
    },
    [onChangePad, onChangePads],
  );

  // Pan ドラッグ: 線形デルタを全選択へ適用。Pan 差を維持し、L/R 端は clamp。
  const commitPan = useCallback(
    (origin: number, newPan: number) => {
      const g = gestureRef.current;
      if (!g || g.kind !== 'pan' || !g.startById.has(origin)) {
        onChangePad(origin, { padPan: newPan });
        return;
      }
      const deltaPan = newPan - g.startById.get(origin)!;
      const changes: { index: number; patch: Partial<PadParams> }[] = [];
      g.startById.forEach((startP, idx) => {
        const pan = idx === origin ? newPan : Math.max(-1, Math.min(1, startP + deltaPan));
        changes.push({ index: idx, patch: { padPan: pan } });
      });
      onChangePads(changes);
    },
    [onChangePad, onChangePads],
  );

  // Output Assign: 選択全員を同じ値に揃える (gesture 不要)。
  const commitOutput = useCallback(
    (origin: number, value: number) => {
      const sel = selectionRef.current;
      const batch = sel.has(origin) && sel.size >= 2;
      if (batch) {
        onChangePads([...sel].map(idx => ({ index: idx, patch: { outputAssign: value } })));
      } else {
        onChangePad(origin, { outputAssign: value });
        setSelection(new Set([origin]));
        anchorRef.current = origin;
        onSelect(origin);
      }
    },
    [onChangePad, onChangePads, onSelect],
  );

  // Alt(Option)+クリックの初期値リセット。batch なら選択全員を default に揃える。
  const resetParam = useCallback(
    (kind: 'volume' | 'pan', origin: number) => {
      const sel = selectionRef.current;
      const batch = sel.has(origin) && sel.size >= 2;
      const def = kind === 'volume' ? defaultPadParam('volume') : defaultPadParam('pan');
      const patch: Partial<PadParams> = kind === 'volume' ? { padVolume: def } : { padPan: def };
      if (batch) {
        onChangePads([...sel].map(idx => ({ index: idx, patch })));
      } else {
        onChangePad(origin, patch);
      }
    },
    [onChangePad, onChangePads],
  );

  const selectionCount = selection.size;

  // ── ルーティング プリセット ──────────────────────────────────────
  // データには本来のアサイン値をそのまま書く（Output Mode によるクランプは
  // 表示側だけで行うので、Mode を戻せばちゃんと OUT 17, OUT 32 等まで復帰する）
  const applyPreset = (preset: RoutingPreset) => {
    const changes = pads.map((_, i) => {
      switch (preset) {
        case 'ALL_MAIN':
          return { index: i, patch: { outputAssign: 0 } };
        case 'INDIVIDUAL':
          return { index: i, patch: { outputAssign: i } };  // 1→OUT 1, 17→OUT 17, 48→OUT 48
      }
    });
    onChangePads(changes);
  };

  const saveCustomRouting = () => {
    setCustomRoutingSnapshot(pads.map(pad => pad.outputAssign));
  };

  const recallCustomRouting = () => {
    if (!customRoutingSnapshot) return;
    onChangePads(customRoutingSnapshot.map((outputAssign, index) => ({
      index,
      patch: { outputAssign },
    })));
  };

  return (
    <div className={styles.mixer}>
      {/* ── 上段ヘッダー ─────────────────────────────────────────── */}
      <div className={styles.topRow}>
        {/* 左：タイトル */}
        <div className={styles.viewTitle}>
          <span className={styles.titleIcon} aria-hidden>
            <i /><i /><i /><i />
          </span>
          <span className={styles.titleText}>MIXER</span>
        </div>

        {/* 中央：バンクタブ */}
        <div className={styles.bankTabs} role="tablist" aria-label="bank">
          {BANKS.map((bank) => (
            <button
              key={bank}
              role="tab"
              aria-selected={bank === page}
              className={`${styles.bankTab} ${bank === page ? styles.bankTabActive : ''}`}
              onClick={() => onPageChange(bank)}
            >
              {bank}
            </button>
          ))}
        </div>

        {/* 右：Output Mode + ルーティングプリセット + メタ情報 */}
        <div className={styles.topRight}>
          <div className={styles.outputModeRow}>
            <span className={styles.presetLabel}>OUT MODE</span>
            <select
              className={styles.outputModeSelect}
              value={outputMode}
              onChange={(e) => onOutputModeChange(e.target.value as OutputMode)}
            >
              {OUTPUT_MODES.map(m => (
                <option key={m} value={m}>{OUTPUT_MODE_LABEL[m]}</option>
              ))}
            </select>
          </div>

          <div className={styles.routingPresets}>
            <span className={styles.presetLabel}>ROUTING</span>
            <span
              className={`${styles.routingStatus} ${routingStatus === 'CUSTOM' ? styles.routingStatusCustom : ''}`}
              title="現在のルーティング状態"
            >
              {ROUTING_STATUS_LABEL[routingStatus]}
            </span>
            <button
              className={styles.presetBtn}
              onClick={() => applyPreset('ALL_MAIN')}
              title="全 Pad を 1-2 へ"
            >
              ALL MAIN
            </button>
            <button
              className={styles.presetBtn}
              onClick={() => applyPreset('INDIVIDUAL')}
              title="Pad 1→OUT 1, Pad 17→OUT 17, Pad 48→OUT 48 のように連番アサイン"
            >
              INDIVIDUAL
            </button>
            <button
              className={styles.presetBtn}
              onClick={saveCustomRouting}
              title="現在の 48 Pad ルーティングを一時保存"
            >
              SAVE CUSTOM
            </button>
            <button
              className={styles.presetBtn}
              onClick={recallCustomRouting}
              disabled={!canRecallCustomRouting}
              title={canRecallCustomRouting ? '一時保存したルーティングを復元' : '先に SAVE CUSTOM で保存してください'}
            >
              RECALL
            </button>
          </div>

          <div className={styles.topMeta}>
            {selectionCount >= 2 && (
              <>
                <span className={styles.selCount}>{selectionCount} channels selected</span>
                <span className={styles.metaDot}>•</span>
              </>
            )}
            <span className={styles.metaLabel}>PADS</span>
            <span className={styles.metaValue}>{start + 1}–{end}</span>
            <span className={styles.metaDot}>•</span>
            <span className={styles.metaLabel}>BANK</span>
            <span className={styles.metaValue}>{page}</span>
          </div>
        </div>
      </div>

      {/* ── チャンネルストリップ ─────────────────────────────────── */}
      {/* 背景 (チャンネル間の余白) クリックで選択解除 */}
      <div
        className={styles.channels}
        onMouseDown={(e) => { if (e.target === e.currentTarget) clearSelection(); }}
      >
        {visiblePads.map((pad, i) => {
          const absoluteIndex = start + i;
          return (
            <ChannelStripContainer
              key={absoluteIndex}
              absoluteIndex={absoluteIndex}
              pad={pad}
              selected={selection.has(absoluteIndex)}
              outputCount={outputCount}
              onChannelMouseDown={handleChannelMouseDown}
              onChannelAudition={handleChannelAudition}
              onChangeSingle={onChangePad}
              onVolumeGestureStart={beginGesture}
              onVolumeCommit={commitVolume}
              onPanCommit={commitPan}
              onOutputCommit={commitOutput}
              onResetParam={resetParam}
              onGestureEnd={endGesture}
            />
          );
        })}
      </div>

      {/* ── フッター ─────────────────────────────────────────────── */}
      <div className={styles.footer}>
        <button className={styles.gear} aria-label="mixer settings">
          <svg width="18" height="18" viewBox="0 0 18 18" aria-hidden>
            <path d="M9 2.2 10.1 4.1 12.3 4.3 12.8 6.4 14.5 7.8 13.5 9.8 13.9 12 11.9 13 10.7 14.9 8.5 14.3 6.5 15 5.4 13 3.3 12.5 3.6 10.3 2.2 8.7 3.7 7.1 3.9 4.9 6.1 4.5 7.4 2.8Z"
              fill="currentColor" opacity="0.9" />
            <circle cx="9" cy="9" r="2.3" fill="var(--bg-deep)" />
          </svg>
        </button>
        <ResourceMeter kind="cpu" label="CPU" />
        <ResourceMeter kind="mem" label="MEM" />
        <MasterOutputBlock
          masterKnob={masterKnob}
          onMasterKnobChange={onMasterKnobChange}
          masterClipHit={masterClipHit}
          onResetMasterClip={onResetMasterClip}
        />
      </div>
    </div>
  );
}

// ─────────────────────────────────────────────────────────────────
// ChannelStripContainer
//   - 受け取った絶対 index を closure に閉じ込めた安定コールバックを作る
//   - メーターは registerPadMeter() の DOM 直更新に分離し、levelData
//     では React tree を再レンダーしない
// ─────────────────────────────────────────────────────────────────
function ChannelStripContainer({
  absoluteIndex, pad, selected, outputCount,
  onChannelMouseDown, onChannelAudition, onChangeSingle,
  onVolumeGestureStart, onVolumeCommit, onPanCommit, onOutputCommit, onResetParam, onGestureEnd,
}: {
  absoluteIndex: number;
  pad: PadParams;
  selected: boolean;
  outputCount: number;
  onChannelMouseDown: (index: number, e: ReactMouseEvent) => void;
  onChannelAudition: (index: number, e: ReactMouseEvent) => void;
  onChangeSingle: (index: number, patch: Partial<PadParams>) => void;
  onVolumeGestureStart: (kind: 'volume' | 'pan', origin: number) => void;
  onVolumeCommit: (origin: number, newPos: number) => void;
  onPanCommit: (origin: number, newPan: number) => void;
  onOutputCommit: (origin: number, value: number) => void;
  onResetParam: (kind: 'volume' | 'pan', origin: number) => void;
  onGestureEnd: () => void;
}) {
  const mouseDownThis = useCallback((e: ReactMouseEvent) => onChannelMouseDown(absoluteIndex, e),
    [onChannelMouseDown, absoluteIndex]);
  const auditionThis = useCallback((e: ReactMouseEvent) => onChannelAudition(absoluteIndex, e),
    [onChannelAudition, absoluteIndex]);
  const changeThis = useCallback((patch: Partial<PadParams>) => onChangeSingle(absoluteIndex, patch),
    [onChangeSingle, absoluteIndex]);
  const volumeStartThis = useCallback(() => onVolumeGestureStart('volume', absoluteIndex),
    [onVolumeGestureStart, absoluteIndex]);
  const volumeCommitThis = useCallback((newPos: number) => onVolumeCommit(absoluteIndex, newPos),
    [onVolumeCommit, absoluteIndex]);
  const panStartThis = useCallback(() => onVolumeGestureStart('pan', absoluteIndex),
    [onVolumeGestureStart, absoluteIndex]);
  const panCommitThis = useCallback((newPan: number) => onPanCommit(absoluteIndex, newPan),
    [onPanCommit, absoluteIndex]);
  const outputCommitThis = useCallback((value: number) => onOutputCommit(absoluteIndex, value),
    [onOutputCommit, absoluteIndex]);
  const resetVolumeThis = useCallback(() => onResetParam('volume', absoluteIndex),
    [onResetParam, absoluteIndex]);
  const resetPanThis = useCallback(() => onResetParam('pan', absoluteIndex),
    [onResetParam, absoluteIndex]);

  return (
    <ChannelStripBody
      absoluteIndex={absoluteIndex}
      pad={pad}
      selected={selected}
      outputCount={outputCount}
      onMouseDownChannel={mouseDownThis}
      onMouseDownAudition={auditionThis}
      onChangeSingle={changeThis}
      onVolumeGestureStart={volumeStartThis}
      onVolumeCommit={volumeCommitThis}
      onPanGestureStart={panStartThis}
      onPanCommit={panCommitThis}
      onOutputCommit={outputCommitThis}
      onResetVolume={resetVolumeThis}
      onResetPan={resetPanThis}
      onGestureEnd={onGestureEnd}
    />
  );
}

// ─────────────────────────────────────────────────────────────────
// ChannelStripBody — memo on pad/selection/output props. Meter movement is
// outside React, so audio levels do not rebuild the strip body.
// ─────────────────────────────────────────────────────────────────
function ChannelStripBodyImpl({
  absoluteIndex, pad, selected, outputCount,
  onMouseDownChannel, onMouseDownAudition, onChangeSingle,
  onVolumeGestureStart, onVolumeCommit,
  onPanGestureStart, onPanCommit,
  onOutputCommit, onResetVolume, onResetPan, onGestureEnd,
}: {
  absoluteIndex: number;
  pad: PadParams;
  selected: boolean;
  outputCount: number;
  onMouseDownChannel: (e: ReactMouseEvent) => void;
  onMouseDownAudition: (e: ReactMouseEvent) => void;
  onChangeSingle: (patch: Partial<PadParams>) => void;
  onVolumeGestureStart: () => void;
  onVolumeCommit: (newPos: number) => void;
  onPanGestureStart: () => void;
  onPanCommit: (newPan: number) => void;
  onOutputCommit: (value: number) => void;
  onResetVolume: () => void;
  onResetPan: () => void;
  onGestureEnd: () => void;
}) {
  // 単体編集用エイリアス (mute/solo/テキスト入力/リセット等は従来どおり単一チャンネル)
  const onChange = onChangeSingle;
  // padVolume is the Pad-wide gain stage after all layer volumes.
  const padVolume = pad.padVolume ?? 0.75;
  const fader = clamp(padVolume, 0, 1);
  const [editingVolume, setEditingVolume] = useState(false);
  const [volumeDraft, setVolumeDraft] = useState('');

  const pan = pad.padPan ?? 0;
  const panLabel = formatPan(pan);

  const sampleName = pad.sampleFileName;
  const padName = pad.padName;
  const isEmpty = !sampleName;

  const volumeFromDragDelta = (
    clientY: number,
    track: HTMLElement,
    startY: number,
    startVolume: number,
    fine: boolean,
  ) => {
    const rect = track.getBoundingClientRect();
    const usableTop = rect.top + 4;
    const usableBottom = rect.bottom - 4;
    const usableHeight = Math.max(1, usableBottom - usableTop);
    const delta = (startY - clientY) / usableHeight;
    return clamp(startVolume + delta * (fine ? 0.2 : 1), 0, 1);
  };

  const handleFaderPointerDown = (e: ReactPointerEvent<HTMLDivElement>) => {
    if (e.detail >= 2) return;
    e.preventDefault();
    e.stopPropagation();
    if (e.altKey) {
      onResetVolume();   // batch なら選択全員を default に
      return;
    }
    // ドラッグ開始: 選択集合のスナップショットを取る (単体ならこのチャンネルのみ選択化)
    onVolumeGestureStart();
    const track = e.currentTarget;
    track.setPointerCapture(e.pointerId);
    const startY = e.clientY;
    const startVolume = padVolume;
    let lastSentAt = 0;
    let lastSentValue = startVolume;
    let latestValue = startVolume;

    const emitVolume = (clientY: number, fine: boolean, force = false) => {
      const nextValue = volumeFromDragDelta(clientY, track, startY, startVolume, fine);
      latestValue = nextValue;
      const now = performance.now();
      if (!force && now - lastSentAt < CONTINUOUS_UPDATE_MS) return;
      if (lastSentValue === nextValue) return;

      lastSentAt = now;
      lastSentValue = nextValue;
      onVolumeCommit(nextValue);
    };

    const onMove = (ev: PointerEvent) => emitVolume(ev.clientY, ev.metaKey || ev.ctrlKey);
    const onUp = () => {
      if (latestValue !== lastSentValue) onVolumeCommit(latestValue);
      onGestureEnd();
      window.removeEventListener('pointermove', onMove);
      window.removeEventListener('pointerup', onUp);
    };

    window.addEventListener('pointermove', onMove);
    window.addEventListener('pointerup', onUp);
  };

  const beginVolumeEdit = () => {
    setVolumeDraft(formatFaderDb(padVolume));
    setEditingVolume(true);
  };

  const commitVolumeDraft = () => {
    const parsed = parseNumericText(volumeDraft);
    if (parsed !== null) onChange({ padVolume: dbToPosition(parsed) });
    setEditingVolume(false);
  };

  const cancelVolumeDraft = () => {
    setEditingVolume(false);
  };

  const handleFaderDoubleClick = (e: ReactMouseEvent<HTMLDivElement>) => {
    e.preventDefault();
    e.stopPropagation();
    beginVolumeEdit();
  };

  const channelClass = [
    styles.channel,
    selected ? styles.channelSelected : '',
    isEmpty ? styles.channelEmpty : '',
    pad.mute ? styles.channelMuted : '',
  ].join(' ');

  // Pan ドラッグ開始: Knob 自身は gesture 開始を通知しないので、ラッパの
  // pointerdown(capture) でスナップショットを取り、pointerup で gesture を閉じる。
  // Alt+クリックは reset (相対ドラッグではない) なので gesture を張らない。
  const handlePanPointerDownCapture = (e: ReactPointerEvent<HTMLDivElement>) => {
    if (e.altKey) return;
    onPanGestureStart();
    window.addEventListener('pointerup', onGestureEnd, { once: true });
  };

  return (
    <article className={channelClass} onMouseDown={onMouseDownChannel}>
      {/* ── 番号 + 名前 + サンプル ─────────────────────────────── */}
      <div
        className={styles.head}
        onMouseDown={onMouseDownAudition}
        role="button"
        tabIndex={0}
        aria-label={`Audition ${padName}`}
      >
        <div className={styles.numBadge}>{String(absoluteIndex + 1).padStart(2, '0')}</div>
        <div
          className={styles.name}
          style={{ color: pad.padColor ?? pad.categoryColor }}
          title={padName}
        >
          {padName}
        </div>
        <div className={styles.sample} title={sampleName || '— empty —'}>
          {sampleName || '— empty —'}
        </div>
      </div>

      {/* ── Pan ─────────────────────────────────────────────────── */}
      <div className={styles.panBlock} onPointerDownCapture={handlePanPointerDownCapture}>
        <Knob
          size={34}
          value={pan}
          min={-1}
          max={1}
          bipolar
          valueText={null}
          inputText={panLabel}
          defaultValue={defaultPadParam('pan')}
          parseInput={parsePanInput}
          onChange={(v) => onPanCommit(v)}
          onReset={onResetPan}
        />
        <div className={styles.panLabel}>{panLabel}</div>
      </div>

      {/* ── OUTPUT アサイン ──────────────────────────────────────── */}
      <div className={styles.outputRow} onMouseDown={(e) => e.stopPropagation()}>
        <OutputAssignDropdown
          width="100%"
          compact
          value={pad.outputAssign}
          outputCount={outputCount}
          onChange={(value) => onOutputCommit(value)}
        />
      </div>

      {/* ── メーター + フェーダー ────────────────────────────────── */}
      <div className={styles.fmBlock}>
        {/* 目盛り位置は FADER_SCALE_MARKS から自動算出 (utils/fader.ts) —
            数値とフェーダー位置はここで一致が保証される */}
        <div className={styles.scale} aria-hidden>
          {FADER_SCALE_MARKS.map((m) => (
            <span
              key={m.label}
              className={`${styles.scaleMark} ${m.db === 0 ? styles.scaleMarkUnity : ''}`}
              style={{ bottom: `${m.position * 100}%` }}
            >
              {m.label}
            </span>
          ))}
        </div>
        <StripMeter padIndex={absoluteIndex} />
        <div className={styles.fader}>
          <div
            className={styles.faderTrack}
            onPointerDown={handleFaderPointerDown}
            onDoubleClick={handleFaderDoubleClick}
            role="slider"
            aria-label={`${padName} pad volume`}
            aria-valuemin={0}
            aria-valuemax={1}
            aria-valuenow={padVolume}
            tabIndex={0}
          >
            <span className={styles.faderUnity} />
            {/* transform-only な実装に変更:
                - faderFill は固定 height + scaleY (transform-origin: bottom)
                - faderThumb は wrap 要素の translateY() で縦移動
                どちらも `transform` だけが変わるので reflow しない (compositor 動作)。 */}
            <span className={styles.faderFill} style={{ transform: `translateX(-50%) scaleY(${fader})` }} />
            <span className={styles.faderThumbWrap} style={{ transform: `translate(-50%, ${-fader * 100}%)` }}>
              <span className={styles.faderThumb}>
                <i /><i /><i />
              </span>
            </span>
          </div>
        </div>
      </div>

      {/* ── dB (フェーダー位置と同じ変換関数から算出) ───────────────── */}
      <div className={styles.dbRow}>
        {editingVolume ? (
          <input
            className={styles.dbInput}
            value={volumeDraft}
            autoFocus
            onMouseDown={(e) => e.stopPropagation()}
            onChange={(e) => setVolumeDraft(e.currentTarget.value)}
            onBlur={cancelVolumeDraft}
            onKeyDown={(e) => {
              if (e.key === 'Enter') commitVolumeDraft();
              if (e.key === 'Escape') cancelVolumeDraft();
            }}
          />
        ) : (
          <span
            className={styles.dbValue}
            onDoubleClick={(e) => {
              e.preventDefault();
              e.stopPropagation();
              beginVolumeEdit();
            }}
          >
            {formatFaderDb(padVolume)}
          </span>
        )}
        <span className={styles.dbUnit}>dB</span>
      </div>

      {/* ── Mute / Solo ─────────────────────────────────────────── */}
      <div className={styles.msRow}>
        <button
          type="button"
          className={`${styles.msButton} ${pad.mute ? styles.msOnMute : ''}`}
          onMouseDown={(e) => e.stopPropagation()}
          onClick={(e) => { e.stopPropagation(); onChange({ mute: !pad.mute }); }}
          aria-pressed={pad.mute}
        >M</button>
        <button
          type="button"
          className={`${styles.msButton} ${pad.solo ? styles.msOnSolo : ''}`}
          onMouseDown={(e) => e.stopPropagation()}
          onClick={(e) => {
            e.stopPropagation();
            const on = !pad.solo;
            onChange(on ? { solo: true, mute: false } : { solo: false });
          }}
          aria-pressed={pad.solo}
        >S</button>
      </div>
    </article>
  );
}

const ChannelStripBody = memo(ChannelStripBodyImpl);

// ─────────────────────────────────────────────────────────────────
// StripMeter — registered once, then updated imperatively from levelData.
// ─────────────────────────────────────────────────────────────────
const StripMeter = memo(function StripMeter({ padIndex }: { padIndex: number }) {
  const fillRef = useRef<HTMLElement | null>(null);
  const fillInnerRef = useRef<HTMLElement | null>(null);
  const peakRef = useRef<HTMLElement | null>(null);
  const clipRef = useRef<HTMLButtonElement | null>(null);

  useEffect(() => registerPadMeter(padIndex, {
    axis: 'y',
    fill: fillRef.current,
    fillInner: fillInnerRef.current,
    peak: peakRef.current,
  }), [padIndex]);

  // Clip latch LED: data-clip="true|false" を clipRegistry が DOM 直接更新する
  useEffect(() => registerPadClipNode(padIndex, clipRef.current), [padIndex]);

  const onClipClick = (e: ReactMouseEvent<HTMLButtonElement>) => {
    e.stopPropagation();
    sendToJuce('resetPadClip', { index: padIndex });
    // 楽観的に LED を消す (次の levelData broadcast でも上書きされない)
    if (clipRef.current) clipRef.current.dataset.clip = 'false';
  };

  return (
    <div className={styles.meter} aria-hidden>
      <button
        ref={clipRef}
        type="button"
        className={styles.channelClipLed}
        data-clip="false"
        onClick={onClipClick}
        aria-label={`Reset clip for channel ${padIndex + 1}`}
        title="Clip — click to reset"
      />
      <span ref={fillRef} className={styles.meterClip} style={{ transform: 'scaleY(0)' }}>
        <span ref={fillInnerRef} className={styles.meterFill} />
      </span>
      <span ref={peakRef} className={styles.meterPeak} style={{ display: 'none' }} />
      <span className={styles.zeroLine} />
    </div>
  );
});

// ─────────────────────────────────────────────────────────────────
// MasterOutputBlock is split into:
//   - MasterOutputBlock (memoized) : Knob + dB label + clip LED. Re-renders
//     only when masterKnob / masterClipHit change (rare).
//   - MasterMeter registers DOM refs once; levelData mutates only transform.
const MasterOutputBlock = memo(function MasterOutputBlock({
  masterKnob, onMasterKnobChange, masterClipHit, onResetMasterClip,
}: {
  masterKnob: number;
  onMasterKnobChange: (v: number) => void;
  masterClipHit: boolean;
  onResetMasterClip: () => void;
}) {
  // dB label is derived from the same fader curve as the knob position
  // (see utils/fader.ts). The OUTPUT knob value IS the fader position.
  const dbLabel = formatFaderDb(masterKnob);
  const [editingOutput, setEditingOutput] = useState(false);
  const [outputDraft, setOutputDraft] = useState('');

  const beginOutputEdit = () => {
    setOutputDraft(dbLabel);
    setEditingOutput(true);
  };

  const commitOutputDraft = () => {
    const parsed = parseNumericText(outputDraft);
    if (parsed !== null) onMasterKnobChange(dbToPosition(parsed));
    setEditingOutput(false);
  };

  return (
    <div className={styles.outputBlock}>
      <span className={styles.footerLabel}>OUTPUT</span>
      <Knob
        size={42}
        value={masterKnob}
        min={0}
        max={1}
        defaultValue={MASTER_UNITY}
        valueText={null}
        inputText={dbLabel}
        parseInput={text => {
          const parsed = parseNumericText(text);
          return parsed === null ? null : dbToPosition(parsed);
        }}
        onChange={onMasterKnobChange}
      />
      {editingOutput ? (
        <input
          className={styles.outputDbInput}
          value={outputDraft}
          autoFocus
          onChange={(e) => setOutputDraft(e.currentTarget.value)}
          onBlur={() => setEditingOutput(false)}
          onKeyDown={(e) => {
            if (e.key === 'Enter') commitOutputDraft();
            if (e.key === 'Escape') setEditingOutput(false);
          }}
        />
      ) : (
        <span
          className={`${styles.outputDb} ${masterClipHit ? styles.outputDbClip : ''}`}
          onDoubleClick={(e) => {
            e.preventDefault();
            e.stopPropagation();
            beginOutputEdit();
          }}
        >
          {dbLabel === '-inf' ? '-inf dB' : `${dbLabel} dB`}
        </span>
      )}
      <MasterMeter />
      <button
        className={`${styles.clipLed} ${masterClipHit ? styles.clipLedActive : ''}`}
        onClick={onResetMasterClip}
        aria-label="clip indicator (click to reset)"
        title="Clip — click to reset"
      />
    </div>
  );
});

const MasterMeter = memo(function MasterMeter() {
  const fillRef = useRef<HTMLElement | null>(null);
  const fillInnerRef = useRef<HTMLElement | null>(null);
  const peakRef = useRef<HTMLElement | null>(null);

  useEffect(() => registerMasterMeter({
    axis: 'x',
    fill: fillRef.current,
    fillInner: fillInnerRef.current,
    peak: peakRef.current,
  }), []);

  return (
    <span className={styles.outputMeter} aria-hidden>
      <i ref={fillRef} className={styles.outputMeterClip} style={{ transform: 'scaleX(0)' }}>
        <b ref={fillInnerRef} className={styles.outputMeterFill} />
      </i>
      <s ref={peakRef} className={styles.outputMeterPeak} style={{ display: 'none' }} />
      <b className={styles.outputMeterZero} />
    </span>
  );
});

// ─────────────────────────────────────────────────────────────────
const ResourceMeter = memo(function ResourceMeter({ kind, label }: { kind: 'cpu' | 'mem'; label: string }) {
  const fillRef = useRef<HTMLElement | null>(null);
  const valueRef = useRef<HTMLElement | null>(null);

  useEffect(() => registerResourceMeter(kind, {
    fill: fillRef.current,
    value: valueRef.current,
  }), [kind]);

  return (
    <div className={styles.resource}>
      <span className={styles.footerLabel}>{label}</span>
      <span className={styles.resourceBar}><i ref={fillRef} style={{ width: '0%' }} /></span>
      <span ref={valueRef} className={styles.resourceValue}>0%</span>
    </div>
  );
});

// memo: MixerView itself. Audio meter ticks are outside React, so they do not
// cascade into 16 strip rebuilds.
export const MixerView = memo(MixerViewComponent);
