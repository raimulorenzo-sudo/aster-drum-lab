import { memo, useCallback, useEffect, useMemo, useRef, useState } from 'react';
import styles from './WaveformEditor.module.css';
import { OverflowMarquee } from '../OverflowMarquee/OverflowMarquee';
import type { PadParams, PreviewPlayback, WaveformChannel } from '../../types';
import { waveformChannelToStrokePath, waveformToPath } from '../../utils/waveform';
import { midiNoteName } from '../../data/padData';
import { formatMs, formatTrimPercent } from '../../utils/parameterFormat';
import { trimFromPad } from '../../utils/sampleTrim';
import type { PointerEvent as ReactPointerEvent } from 'react';
import type { MouseEvent as ReactMouseEvent } from 'react';
import type { DragEvent as ReactDragEvent } from 'react';
import { selectedLayerIndexOf } from '../../utils/layerView';
import { padAutomationTarget } from '../../utils/automationTarget';

interface WaveformEditorProps {
  pad: PadParams;
  padIndex: number;
  onChange: (patch: Partial<PadParams>) => void;
  onSampleDrop?: (file: File) => void;
  onReanalyze?: () => void;
  onRelinkSample?: () => void;
  previewPlayback: PreviewPlayback;
  onPreviewFinished: (triggerId: number) => void;
  onWaveformAudition?: () => void;
}

// 60 Hz throttle: ドラッグハンドルや onChange 経由 JUCE 送信用。
// (再生ヘッド描画はこの定数を使わず rAF の毎フレームで動かす — SVG line 1 本なので
//  120Hz でも余裕)
const CONTINUOUS_UPDATE_MS = 1000 / 60;

/* ── Zoom config (横ズーム専用 / 表示専用) ────────────────────────
   - viewStartPct / viewEndPct: 表示している範囲 (0..1 of totalMs)
   - 実際の Start / End / Fade 値とは完全に分離する。
   - Pad / Layer 切替時にリセットする (state は一時的, 保存対象外)。
*/
const MIN_VIEW_WIDTH_PCT = 0.005;   // 最大ズーム: 全体の 0.5% (≈ 200x)
const ZOOM_STEP = 1.6;              // ボタン / wheel 1 step あたりの倍率

function WaveformEditorComponent({
  pad,
  padIndex,
  onChange,
  onSampleDrop,
  onReanalyze,
  onRelinkSample,
  previewPlayback,
  onPreviewFinished,
  onWaveformAudition,
}: WaveformEditorProps) {
  const [editingName, setEditingName] = useState(false);
  const [draftName, setDraftName] = useState(pad.padName);
  const [editingMidi, setEditingMidi] = useState(false);
  const [draggingHandle, setDraggingHandle] = useState<WaveHandle | null>(null);
  const [isSampleDragOver, setIsSampleDragOver] = useState(false);
  const [waveformDisplayMode, setWaveformDisplayMode] = useState<'stereo' | 'sum'>('stereo');

  // ── View state (一時的 / 表示専用 / 保存対象外) ─────────────────
  const [viewStartPct, setViewStartPct] = useState(0);
  const [viewEndPct,   setViewEndPct]   = useState(1);
  const waveBoxRef = useRef<HTMLDivElement | null>(null);

  // Pad/Layer/Sample 切替時にビューをリセット (表示が崩れないように)。
  // sampleFilePath が Layer 切替や Pad 切替で変わる → これをトリガーにする。
  useEffect(() => {
    setViewStartPct(0);
    setViewEndPct(1);
    setWaveformDisplayMode('stereo');
  }, [padIndex, pad.sampleFilePath]);

  // Preview playhead position is updated around 30fps by requestAnimationFrame.
  // Storing it in React state would re-render the entire SVG every frame
  // (>500 path nodes). Instead we keep a ref to the <line> element and
  // mutate its attributes directly — React only needs to render once when
  // the preview starts/stops.
  const playheadRef = useRef<SVGLineElement | null>(null);
  const [previewActive, setPreviewActive] = useState(false);

  useEffect(() => {
    setDraftName(pad.padName);
    setEditingName(false);
    setEditingMidi(false);
  }, [padIndex, pad.padName]);

  const waveformPeaks = pad.waveformPeaks ?? [];
  const waveformChannels = pad.waveformChannels ?? [];
  const hasChannelWaveform = waveformChannels.some(channel =>
    channel.min.length > 1 && channel.max.length > 1,
  );
  const hasWaveform = Boolean(
    pad.sampleFileName
    && !pad.sampleMissing
    && (waveformPeaks.length > 1 || hasChannelWaveform),
  );
  const hasStereoWaveform = waveformChannels.length >= 2;
  const emptyWaveformText = pad.sampleMissing
    ? 'SAMPLE MISSING'
    : hasWaveform
      ? null
      : 'DROP SAMPLE HERE';
  const dragOverlayText = hasWaveform ? 'DROP TO REPLACE LAYER SAMPLE' : 'RELEASE TO LOAD';
  const isAudioFile = (file: File) => /\.(wav|aiff?|flac|mp3|ogg)$/i.test(file.name);

  // Reverse 時はサンプル配列を反転して波形描画も逆向きに（仕様の "可能であれば反転"）
  const samples = useMemo(
    () => (pad.reverse ? [...waveformPeaks].reverse() : waveformPeaks),
    [waveformPeaks, pad.reverse],
  );
  const displayChannels = useMemo<WaveformChannel[]>(
    () => waveformChannels.map(channel => pad.reverse
      ? {
          min: [...channel.min].reverse(),
          max: [...channel.max].reverse(),
          extremeOrder: channel.extremeOrder
            ? [...channel.extremeOrder].reverse().map(order => order === 1 ? 0 : 1)
            : undefined,
        }
      : channel),
    [pad.reverse, waveformChannels],
  );

  // 波形 SVG のサイズ (viewBox)
  const W = 1000;
  const H = 260;
  const waveTop = 18;
  const waveBottom = H - 18;
  const waveHeight = waveBottom - waveTop;
  const waveMid = waveTop + waveHeight / 2;
  const stereoGap = 24;
  const stereoLaneHeight = (waveHeight - stereoGap) / 2;
  const stereoCenters = [
    waveTop + stereoLaneHeight / 2,
    waveBottom - stereoLaneHeight / 2,
  ];
  const stereoAmplitude = stereoLaneHeight * 0.46;

  const sumPath = useMemo(
    () => (hasWaveform ? waveformToPath(samples, W, waveHeight) : null),
    [hasWaveform, samples, waveHeight],
  );
  const summedDisplayChannel = useMemo<WaveformChannel | null>(() => {
    if (displayChannels.length === 0) return null;
    if (displayChannels.length === 1) return displayChannels[0];

    const pointCount = Math.min(
      displayChannels[0].min.length,
      displayChannels[0].max.length,
      displayChannels[1].min.length,
      displayChannels[1].max.length,
    );
    return {
      min: Array.from({ length: pointCount }, (_, index) =>
        ((displayChannels[0].min[index] ?? 0) + (displayChannels[1].min[index] ?? 0)) * 0.5),
      max: Array.from({ length: pointCount }, (_, index) =>
        ((displayChannels[0].max[index] ?? 0) + (displayChannels[1].max[index] ?? 0)) * 0.5),
      extremeOrder: displayChannels[0].extremeOrder?.slice(0, pointCount),
    };
  }, [displayChannels]);
  const monoStrokePath = useMemo(
    () => summedDisplayChannel
      ? waveformChannelToStrokePath(summedDisplayChannel, W, waveMid, waveHeight * 0.46)
      : '',
    [summedDisplayChannel, waveHeight, waveMid],
  );
  const stereoStrokePaths = useMemo(
    () => displayChannels.slice(0, 2).map((channel, index) =>
      waveformChannelToStrokePath(channel, W, stereoCenters[index], stereoAmplitude)),
    [displayChannels, stereoAmplitude, stereoCenters[0], stereoCenters[1]],
  );
  const showStereoLanes = hasStereoWaveform && waveformDisplayMode === 'stereo';

  const trim = trimFromPad(pad);
  const totalMs = trim.sampleLengthMs;
  const xFromMs = (ms: number) => (ms / totalMs) * W;

  const startX = xFromMs(trim.startMs);
  const endX = xFromMs(trim.endMs);

  // ms / fade を pixel に
  const fadeInX = startX + xFromMs(trim.fadeInMs);
  const fadeOutX = endX - xFromMs(trim.fadeOutMs);
  const fadeInActive = draggingHandle === 'fadeIn';
  const fadeOutActive = draggingHandle === 'fadeOut';
  const fadeInLabelVisible = trim.fadeInMs > 0.05 || draggingHandle === 'fadeIn';
  const fadeOutLabelVisible = trim.fadeOutMs > 0.05 || draggingHandle === 'fadeOut';

  // ── Playhead animation ──────────────────────────────────────────────
  // SVG <line> 1 本の x1/x2 を書き換えるだけなので、rAF の毎フレームで動かす。
  // ディスプレイ垂直同期 (60Hz / 120Hz) に追従し、自前の 30fps cap は撤去。
  // React state は触らないので、再生中の再レンダーコストは発生しない。
  useEffect(() => {
    if (!previewPlayback.isPreviewPlaying || previewPlayback.padIndex !== padIndex) {
      if (previewActive) setPreviewActive(false);
      return;
    }

    if (!previewActive) setPreviewActive(true);

    let rafId = 0;
    let finished = false;

    const tick = (now: number) => {
      const elapsedMs = now - previewPlayback.previewStartedAt;
      const progress = Math.min(1, elapsedMs / Math.max(1, previewPlayback.previewDurationMs));
      // The waveform itself is mirrored in Reverse mode, so its visual
      // playhead still advances from the displayed start toward the end.
      const from = previewPlayback.previewStartPercent;
      const to = previewPlayback.previewEndPercent;

      const percent = from + (to - from) * progress;
      const x = xFromMs(percent * totalMs);

      // Mutate the line element in place — no React render.
      const node = playheadRef.current;
      if (node) {
        node.setAttribute('x1', String(x));
        node.setAttribute('x2', String(x));
      }

      if (progress >= 1) {
        finished = true;
        setPreviewActive(false);
        onPreviewFinished(previewPlayback.triggerId);
        return;
      }

      rafId = window.requestAnimationFrame(tick);
    };

    rafId = window.requestAnimationFrame(tick);

    return () => {
      if (!finished) window.cancelAnimationFrame(rafId);
    };
    // xFromMs / totalMs depend on `pad`, but they're stable for the duration
    // of one preview (selecting another pad would set isPreviewPlaying=false).
    // Including them would tear down rAF and restart on every parent render.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [
    onPreviewFinished,
    padIndex,
    previewPlayback.isPreviewPlaying,
    previewPlayback.padIndex,
    previewPlayback.previewDurationMs,
    previewPlayback.previewEndPercent,
    previewPlayback.previewStartPercent,
    previewPlayback.previewStartedAt,
    previewPlayback.triggerId,
  ]);

  // 値表示用
  const clampMidi = (note: number) => Math.max(0, Math.min(127, note));
  const commitName = () => {
    const next = draftName.trim();
    if (next.length > 0) onChange({ padName: next });
    else setDraftName(pad.padName);
    setEditingName(false);
  };

  // CSS pixel → ms (ビュー範囲考慮)。
  // ハンドル ドラッグはこの関数で受ける。
  const msFromPointer = useCallback(
    (clientX: number, svg: SVGSVGElement) => {
      const rect = svg.getBoundingClientRect();
      const x = Math.max(0, Math.min(rect.width, clientX - rect.left));
      const localPct = x / Math.max(1, rect.width);
      const samplePct = viewStartPct + localPct * (viewEndPct - viewStartPct);
      return samplePct * totalMs;
    },
    [totalMs, viewStartPct, viewEndPct],
  );

  // ── Zoom helpers ──────────────────────────────────────────────────
  const isFitView = viewStartPct === 0 && viewEndPct === 1;

  /** anchorPct (0..1, 全体に対する比率) を固定したまま倍率を変える。 */
  const applyZoom = useCallback(
    (factor: number, anchorPct?: number) => {
      const curWidth = viewEndPct - viewStartPct;
      const nextWidth = Math.max(MIN_VIEW_WIDTH_PCT, Math.min(1, curWidth / factor));
      // anchor 未指定なら現在の中心を固定
      const anchor = anchorPct ?? (viewStartPct + curWidth / 2);
      // anchor の view 内の相対位置を維持
      const localPos = curWidth > 0 ? (anchor - viewStartPct) / curWidth : 0.5;
      let nextStart = anchor - localPos * nextWidth;
      let nextEnd   = nextStart + nextWidth;
      // クランプ
      if (nextStart < 0) { nextStart = 0; nextEnd = nextWidth; }
      if (nextEnd > 1)   { nextEnd = 1;   nextStart = 1 - nextWidth; }
      setViewStartPct(nextStart);
      setViewEndPct(nextEnd);
    },
    [viewStartPct, viewEndPct],
  );

  const fitView = useCallback(() => {
    setViewStartPct(0);
    setViewEndPct(1);
  }, []);

  const firstAudioFileFromDrag = (dataTransfer: DataTransfer) => {
    const files = Array.from(dataTransfer.files);
    return files.find(isAudioFile) ?? files[0];
  };

  const handleSampleDragOver = useCallback(
    (e: ReactDragEvent<HTMLDivElement>) => {
      if (!onSampleDrop) return;
      // WKWebView/Logic can leave dataTransfer.items empty during Finder drags.
      // Accept the drag here, then filter for audio files on drop.
      e.preventDefault();
      e.stopPropagation();
      e.dataTransfer.dropEffect = 'copy';
      setIsSampleDragOver(true);
    },
    [onSampleDrop],
  );

  const handleSampleDragLeave = useCallback((e: ReactDragEvent<HTMLDivElement>) => {
    if (!e.currentTarget.contains(e.relatedTarget as Node | null)) {
      setIsSampleDragOver(false);
    }
  }, []);

  const handleSampleDrop = useCallback(
    (e: ReactDragEvent<HTMLDivElement>) => {
      if (!onSampleDrop) return;
      e.preventDefault();
      e.stopPropagation();
      setIsSampleDragOver(false);
      const file = firstAudioFileFromDrag(e.dataTransfer);
      if (file) onSampleDrop(file);
    },
    [onSampleDrop],
  );

  /** 横スクロール (Shift+wheel / 通常 wheel) - dy: viewBox pct 単位の移動量 */
  const scrollView = useCallback(
    (dPct: number) => {
      const width = viewEndPct - viewStartPct;
      if (width >= 1) return; // fit 中はスクロール不要
      let nextStart = viewStartPct + dPct;
      if (nextStart < 0) nextStart = 0;
      if (nextStart + width > 1) nextStart = 1 - width;
      setViewStartPct(nextStart);
      setViewEndPct(nextStart + width);
    },
    [viewStartPct, viewEndPct],
  );

  /** wheel event handler (zoom / pan). 非 passive で登録するため useEffect で attach。 */
  useEffect(() => {
    const el = waveBoxRef.current;
    if (!el) return;
    const onWheel = (e: WheelEvent) => {
      const zoomKey = e.ctrlKey || e.metaKey;
      if (zoomKey) {
        e.preventDefault();
        // マウス位置を anchor に
        const rect = el.getBoundingClientRect();
        const localPct = Math.max(0, Math.min(1, (e.clientX - rect.left) / Math.max(1, rect.width)));
        const samplePct = viewStartPct + localPct * (viewEndPct - viewStartPct);
        const dir = e.deltaY < 0 ? 1 : -1; // up = zoom in
        const stepFactor = dir > 0 ? ZOOM_STEP : 1 / ZOOM_STEP;
        applyZoom(stepFactor, samplePct);
        return;
      }
      // Shift + wheel または横方向スクロールで pan
      const dy = e.deltaX !== 0 ? e.deltaX : (e.shiftKey ? e.deltaY : 0);
      if (dy === 0) return;
      e.preventDefault();
      const width = viewEndPct - viewStartPct;
      // 1 ホイール 100px ≈ ビュー幅の 12% 移動
      const dPct = (dy / 800) * width;
      scrollView(dPct);
    };
    el.addEventListener('wheel', onWheel, { passive: false });
    return () => el.removeEventListener('wheel', onWheel);
  }, [applyZoom, scrollView, viewStartPct, viewEndPct]);

  // ── viewBox / label remapping ─────────────────────────────────────
  const vbX0 = viewStartPct * W;
  const vbW  = (viewEndPct - viewStartPct) * W;
  const viewBoxAttr = `${vbX0} 0 ${vbW} ${H}`;
  // 全体に対する ms 位置 → 現在ビューの CSS pct 位置 (overlay label 用)
  const cssPctFromMs = (ms: number) => {
    const samplePct = ms / Math.max(1, totalMs);
    if (viewEndPct === viewStartPct) return 0;
    return ((samplePct - viewStartPct) / (viewEndPct - viewStartPct)) * 100;
  };
  const isMsInView = (ms: number) => {
    const samplePct = ms / Math.max(1, totalMs);
    return samplePct >= viewStartPct && samplePct <= viewEndPct;
  };

  // ── View Range Bar (下部の細いバーで現在ビュー範囲を pan) ──────────
  const handleRangePointerDown = useCallback(
    (e: ReactPointerEvent<HTMLDivElement>) => {
      e.preventDefault();
      e.stopPropagation();
      const track = e.currentTarget.parentElement as HTMLDivElement | null;
      if (!track) return;
      const rect = track.getBoundingClientRect();
      if (rect.width <= 0) return;
      const width = viewEndPct - viewStartPct;
      if (width >= 1) return; // fit 状態では pan 不要
      const startPointerPct = (e.clientX - rect.left) / rect.width;
      const startViewStart = viewStartPct;
      try { e.currentTarget.setPointerCapture(e.pointerId); } catch { /* noop */ }

      const onMove = (ev: PointerEvent) => {
        const pct = (ev.clientX - rect.left) / rect.width;
        let nextStart = startViewStart + (pct - startPointerPct);
        if (nextStart < 0) nextStart = 0;
        if (nextStart + width > 1) nextStart = 1 - width;
        setViewStartPct(nextStart);
        setViewEndPct(nextStart + width);
      };
      const onUp = () => {
        window.removeEventListener('pointermove', onMove);
        window.removeEventListener('pointerup', onUp);
      };
      window.addEventListener('pointermove', onMove);
      window.addEventListener('pointerup', onUp);
    },
    [viewStartPct, viewEndPct],
  );

  const startTrimDrag = useCallback(
    (e: ReactPointerEvent<SVGElement>, handle: WaveHandle) => {
      e.preventDefault();
      e.stopPropagation();
      const svg = e.currentTarget.ownerSVGElement ?? (e.currentTarget as SVGSVGElement);
      const startValue = handle === 'start'
        ? trim.startMs
        : handle === 'end'
          ? trim.endMs
          : handle === 'fadeIn'
            ? trim.fadeInMs
            : trim.fadeOutMs;
      const startPointerMs = msFromPointer(e.clientX, svg);
      setDraggingHandle(handle);
      let lastSentAt = 0;
      let latestPatch: Partial<PadParams> | null = null;

      const emitPatch = (patch: Partial<PadParams>, force = false) => {
        latestPatch = patch;
        const now = performance.now();
        if (!force && now - lastSentAt < CONTINUOUS_UPDATE_MS) return;

        lastSentAt = now;
        onChange(patch);
      };

      const patchFromPointer = (clientX: number, fine: boolean): Partial<PadParams> => {
        const pointerMs = msFromPointer(clientX, svg);
        // Keep the grab offset instead of snapping the marker to the pointer.
        // The visible lines/tabs have intentionally wide hit areas, so using
        // the absolute pointer position made a marker jump as soon as a drag
        // started and could look as if it bounced back after normalisation.
        const deltaMs = (pointerMs - startPointerMs) * (fine ? 0.2 : 1.0);
        if (handle === 'start') {
          return { startMs: startValue + deltaMs };
        }
        if (handle === 'end') {
          return { endMs: startValue + deltaMs };
        }
        if (handle === 'fadeIn') {
          return { fadeInMs: startValue + deltaMs };
        }
        return { fadeOutMs: startValue - deltaMs };
      };

      const apply = (clientX: number, fine: boolean, force = false) =>
        emitPatch(patchFromPointer(clientX, fine), force);

      // Commit the unchanged starting value so grabbing any part of the wide
      // hit area never moves the line before the pointer itself moves.
      apply(e.clientX, e.metaKey || e.ctrlKey, true);

      const onMove = (ev: PointerEvent) => apply(ev.clientX, ev.metaKey || ev.ctrlKey);
      const onUp = () => {
        if (latestPatch) onChange(latestPatch);
        setDraggingHandle(null);
        window.removeEventListener('pointermove', onMove);
        window.removeEventListener('pointerup', onUp);
      };

      window.addEventListener('pointermove', onMove);
      window.addEventListener('pointerup', onUp);
    },
    [msFromPointer, onChange, trim.endMs, trim.fadeInMs, trim.fadeOutMs, trim.startMs],
  );

  const resetHandle = useCallback(
    (e: ReactMouseEvent<SVGElement>, handle: WaveHandle) => {
      e.preventDefault();
      e.stopPropagation();
      if (handle === 'start') onChange({ startMs: 0 });
      if (handle === 'end') onChange({ endMs: totalMs });
      if (handle === 'fadeIn') onChange({ fadeInMs: 0 });
      if (handle === 'fadeOut') onChange({ fadeOutMs: 0 });
    },
    [onChange, totalMs],
  );

  const triggerWaveformAudition = useCallback(
    (e: ReactMouseEvent<SVGSVGElement>) => {
      if (!hasWaveform || !onWaveformAudition || draggingHandle) return;
      if (e.button !== 0 || e.detail > 1) return;

      onWaveformAudition();
    },
    [draggingHandle, hasWaveform, onWaveformAudition],
  );

  return (
    <div className={styles.editor}>
      {/* ── タイトル行 ────────────────────────────────────────────────── */}
      <header className={styles.titleRow}>
        <div className={styles.selectedBlock}>
          <span className={styles.selectedLabel}>SELECTED PAD:</span>
          {editingName ? (
            <input
              className={styles.nameInput}
              value={draftName}
              autoFocus
              onChange={(e) => setDraftName(e.target.value)}
              onBlur={commitName}
              onKeyDown={(e) => {
                if (e.key === 'Enter') commitName();
                if (e.key === 'Escape') {
                  setDraftName(pad.padName);
                  setEditingName(false);
                }
              }}
            />
          ) : (
            <button
              type="button"
              className={styles.selectedNameButton}
              onClick={() => setEditingName(true)}
              title="Edit pad name"
            >
              <strong className={styles.selectedName}>{pad.padName}</strong>
            </button>
          )}
          <span className={styles.midiWrap}>
            {editingMidi && (
              <button
                type="button"
                className={styles.midiStep}
                onClick={() => onChange({ midiNote: clampMidi(pad.midiNote - 1) })}
                aria-label="decrease MIDI note"
              >
                −
              </button>
            )}
            <button
              type="button"
              className={styles.midiPill}
              title="MIDI Note"
              onClick={() => setEditingMidi(v => !v)}
            >
              {midiNoteName(pad.midiNote)}
            </button>
            {editingMidi && (
              <button
                type="button"
                className={styles.midiStep}
                onClick={() => onChange({ midiNote: clampMidi(pad.midiNote + 1) })}
                aria-label="increase MIDI note"
              >
                +
              </button>
            )}
          </span>
        </div>

        {/* ── 編集ツール: REVERSE / KEEP LENGTH / SMART TRIM / RE-ANALYZE ─ */}
        <div className={styles.editTools}>
          <button
            type="button"
            className={`${styles.toolBtn} ${pad.reverse ? styles.toolBtnActive : ''}`}
            onClick={() => onChange({ reverse: !pad.reverse })}
            aria-pressed={pad.reverse}
            title="再生方向を反転"
            data-automation-target-id={selectedLayerIndexOf(pad) === 0 ? padAutomationTarget(padIndex, 'reverse', 'Reverse').id : undefined}
            data-automation-target-name={selectedLayerIndexOf(pad) === 0 ? padAutomationTarget(padIndex, 'reverse', 'Reverse').name : undefined}
          >
            REVERSE
          </button>
          <button
            type="button"
            className={`${styles.toolBtn} ${pad.keepLength ? styles.toolBtnActive : ''}`}
            onClick={() => onChange({ keepLength: !pad.keepLength })}
            aria-pressed={pad.keepLength}
            title="Pitchを変更してもサンプルの長さを維持"
          >
            KEEP LENGTH
          </button>
          <button
            type="button"
            className={`${styles.toolBtn} ${pad.smartTrim ? styles.toolBtnActive : ''}`}
            onClick={() => onChange({ smartTrim: !pad.smartTrim })}
            aria-pressed={pad.smartTrim}
            title="先頭の無音や最初のトランジェントを自動検出して Start を合わせる"
          >
            SMART TRIM
          </button>
          <button
            type="button"
            className={styles.toolBtn}
            onClick={onReanalyze}
            disabled={!onReanalyze || !pad.sampleFileName || pad.sampleMissing}
            title="サンプルを再解析"
          >
            RE-ANALYZE
          </button>
        </div>

        <div className={styles.sampleBlock}>
          <span className={styles.sampleNav}>‹ ›</span>
          <span className={styles.sampleLabel}>SAMPLE</span>
          <OverflowMarquee
            className={`${styles.filename} ${pad.sampleMissing ? styles.filenameMissing : ''}`}
            title={pad.sampleFilePath || pad.originalSampleFilePath || pad.sampleFileName || 'no sample'}
            text={pad.sampleMissing ? `Missing: ${pad.sampleFileName || 'sample'}` : pad.sampleFileName || 'no sample'}
            disabled={pad.sampleMissing || !pad.sampleFileName}
          />
          {pad.sampleMissing && (
            <button type="button" className={styles.relinkBtn} onClick={onRelinkSample}>
              RELINK
            </button>
          )}
        </div>
      </header>

      {/* ── 波形表示 ─────────────────────────────────────────────────── */}
      <div
        className={`${styles.waveBox} ${isSampleDragOver ? styles.waveBoxDragOver : ''}`}
        ref={waveBoxRef}
        onDragOverCapture={handleSampleDragOver}
        onDropCapture={handleSampleDrop}
        onDragOver={handleSampleDragOver}
        onDragLeave={handleSampleDragLeave}
        onDrop={handleSampleDrop}
      >
        {/* ── 上部ツールレーン: 編集UIと被らない専用帯 ─────────────── */}
        {hasWaveform && (
          <div className={styles.waveToolLane}>
            {hasStereoWaveform && (
              <div className={styles.channelModeControls} role="group" aria-label="Waveform channel display">
                <button
                  type="button"
                  className={`${styles.channelModeBtn} ${waveformDisplayMode === 'stereo' ? styles.channelModeBtnActive : ''}`}
                  onClick={() => setWaveformDisplayMode('stereo')}
                  aria-pressed={waveformDisplayMode === 'stereo'}
                >
                  STEREO
                </button>
                <button
                  type="button"
                  className={`${styles.channelModeBtn} ${waveformDisplayMode === 'sum' ? styles.channelModeBtnActive : ''}`}
                  onClick={() => setWaveformDisplayMode('sum')}
                  aria-pressed={waveformDisplayMode === 'sum'}
                >
                  SUM
                </button>
              </div>
            )}
            <div className={styles.zoomControls}>
              <button
                type="button"
                className={styles.zoomBtn}
                onClick={() => applyZoom(1 / ZOOM_STEP)}
                disabled={isFitView}
                aria-label="Zoom out"
                title="Zoom out"
              >
                −
              </button>
              <button
                type="button"
                className={styles.zoomBtn}
                onClick={() => applyZoom(ZOOM_STEP)}
                aria-label="Zoom in"
                title="Zoom in (Cmd/Ctrl + wheel)"
              >
                +
              </button>
              <button
                type="button"
                className={`${styles.zoomBtn} ${styles.zoomBtnFit}`}
                onClick={fitView}
                disabled={isFitView}
                aria-label="Fit waveform"
                title="Fit to view (double-click waveform)"
              >
                FIT
              </button>
            </div>
          </div>
        )}

        {/* ── 中段: 波形 + ハンドル + overlay ラベル ─────────────── */}
        <div
          className={styles.waveDisplay}
          onDragOver={handleSampleDragOver}
          onDrop={handleSampleDrop}
        >
        <svg
          className={`${styles.svg} ${hasWaveform && onWaveformAudition ? styles.svgAudition : ''}`}
          viewBox={viewBoxAttr}
          preserveAspectRatio="none"
          onMouseDown={triggerWaveformAudition}
          onDoubleClick={(e) => {
            // ハンドル系の dblclick はそれぞれ stopPropagation していないが、
            // resetHandle 側で preventDefault + stopPropagation しているので
            // ここに来るのは "波形背景" 上の dblclick のみ。
            // Fit へ戻す (表示専用、データは触らない)。
            if (!isFitView) {
              e.preventDefault();
              fitView();
            }
          }}
        >
          <defs>
            <linearGradient id="fadeInRangeFill" x1="0" y1="0" x2="1" y2="0">
              <stop offset="0%" stopColor="rgba(191,163,106,0.02)" />
              <stop offset="100%" stopColor="rgba(191,163,106,0.18)" />
            </linearGradient>
            <linearGradient id="fadeOutRangeFill" x1="0" y1="0" x2="1" y2="0">
              <stop offset="0%" stopColor="rgba(191,163,106,0.18)" />
              <stop offset="100%" stopColor="rgba(191,163,106,0.02)" />
            </linearGradient>
          </defs>

          {/* 参考デザインの薄い時間/振幅グリッド */}
          {[0.25, 0.5, 0.75].map(position => (
            <line
              key={`time-${position}`}
              x1={W * position} y1={waveTop} x2={W * position} y2={waveBottom}
              className={styles.waveGridLine}
            />
          ))}
          {(showStereoLanes
            ? stereoCenters.flatMap(center => [-0.75, -0.375, 0, 0.375, 0.75]
                .map(offset => center + stereoAmplitude * offset))
            : [-0.75, -0.5, -0.25, 0, 0.25, 0.5, 0.75]
                .map(offset => waveMid + waveHeight * 0.46 * offset)
          ).map((position, index) => (
            <line
              key={`level-${index}`}
              x1="0" y1={position} x2={W} y2={position}
              className={showStereoLanes
                ? (index % 5 === 2 ? styles.waveZeroLine : styles.waveGridLine)
                : (index === 3 ? styles.waveZeroLine : styles.waveGridLine)}
            />
          ))}

          {showStereoLanes && (
            <>
              <line
                x1="0" y1={waveMid} x2={W} y2={waveMid}
                className={styles.waveLaneDivider}
              />
              <g className={styles.waveform}>
                {stereoStrokePaths.map((channelPath, index) => channelPath && (
                  <path
                    key={index}
                    d={channelPath}
                    fill="none"
                    stroke="var(--wave-editor-wave-edge)"
                    strokeWidth="0.62"
                    strokeLinecap="round"
                    strokeLinejoin="round"
                  />
                ))}
              </g>
            </>
          )}

          {!showStereoLanes && monoStrokePath && (
            <g className={styles.waveform}>
              <path
                d={monoStrokePath}
                fill="none"
                stroke="var(--wave-editor-wave-edge)"
                strokeWidth="0.62"
                strokeLinecap="round"
                strokeLinejoin="round"
              />
            </g>
          )}

          {!showStereoLanes && !monoStrokePath && hasWaveform && sumPath && (
            <g className={styles.waveform} transform={`translate(0 ${waveTop})`}>
              <path d={sumPath.top} stroke="var(--wave-editor-wave-edge)" strokeWidth="0.62" fill="none" />
              <path d={sumPath.bottom} stroke="var(--wave-editor-wave-edge)" strokeWidth="0.62" fill="none" />
            </g>
          )}

          {hasWaveform && (
            <>
              {/* 範囲外 (start より前 / end より後) を暗く */}
              <rect x={0} y={waveTop} width={startX} height={waveHeight} fill="rgba(0,0,0,0.55)" />
              <rect x={endX} y={waveTop} width={W - endX} height={waveHeight} fill="rgba(0,0,0,0.55)" />
            </>
          )}

          {/* ── Fade In: 1本の自然なカーブ (Start=0 → Full) ─────────────── */}
          {hasWaveform && trim.fadeInMs > 0 && (
            <g style={{ pointerEvents: 'none' }}>
              <rect
                x={startX} y={waveTop}
                width={Math.max(0, fadeInX - startX)} height={waveHeight}
                className={`${styles.fadeRange} ${fadeInActive ? styles.fadeRangeActive : ''}`}
                fill="url(#fadeInRangeFill)"
              />
              <polygon
                points={`${startX},${waveTop} ${fadeInX},${waveTop} ${startX},${waveBottom}`}
                className={`${styles.fadeDarkMask} ${fadeInActive ? styles.fadeDarkMaskActive : ''}`}
              />
              <line
                x1={fadeInX} y1={waveTop} x2={fadeInX} y2={waveBottom}
                className={styles.fadeGuide}
              />
              <path
                d={`M ${startX} ${waveBottom} L ${fadeInX} ${waveTop}`}
                className={styles.fadeCurve}
              />
            </g>
          )}

          {/* ── Fade Out: 1本の自然なカーブ (Full → End=0) ─────────────── */}
          {hasWaveform && trim.fadeOutMs > 0 && (
            <g style={{ pointerEvents: 'none' }}>
              <rect
                x={fadeOutX} y={waveTop}
                width={Math.max(0, endX - fadeOutX)} height={waveHeight}
                className={`${styles.fadeRange} ${fadeOutActive ? styles.fadeRangeActive : ''}`}
                fill="url(#fadeOutRangeFill)"
              />
              <polygon
                points={`${fadeOutX},${waveTop} ${endX},${waveTop} ${endX},${waveBottom}`}
                className={`${styles.fadeDarkMask} ${fadeOutActive ? styles.fadeDarkMaskActive : ''}`}
              />
              <line
                x1={fadeOutX} y1={waveTop} x2={fadeOutX} y2={waveBottom}
                className={styles.fadeGuide}
              />
              <path
                d={`M ${fadeOutX} ${waveTop} L ${endX} ${waveBottom}`}
                className={styles.fadeCurve}
              />
            </g>
          )}

          {/* Preview playhead: position is mutated directly via ref from rAF — no React render per frame. */}
          {hasWaveform && previewActive && (
            <line
              ref={playheadRef}
              x1={0} y1={waveTop} x2={0} y2={waveBottom}
              className={styles.previewPlayhead}
            />
          )}

          {hasWaveform && (
            <>
              {/* ── START 縦ライン ──────────────────────────────────────── */}
              <line
                x1={startX} y1={waveTop} x2={startX} y2={waveBottom}
                className={`${styles.trimMarker} ${styles.startMarker}`}
                onPointerDown={(e) => startTrimDrag(e, 'start')}
                onDoubleClick={(e) => resetHandle(e, 'start')}
              />
              {/* ── END 縦ライン ────────────────────────────────────────── */}
              <line
                x1={endX} y1={waveTop} x2={endX} y2={waveBottom}
                className={`${styles.trimMarker} ${styles.endMarker}`}
                onPointerDown={(e) => startTrimDrag(e, 'end')}
                onDoubleClick={(e) => resetHandle(e, 'end')}
              />

              {/* ── 広いヒットエリア (透明) ──────────────────────────────── */}
              <rect x={startX - 12} y={0} width={24} height={H}
                fill="transparent" className={styles.trimHitArea}
                onPointerDown={(e) => startTrimDrag(e, 'start')}
                onDoubleClick={(e) => resetHandle(e, 'start')}
              />
              <rect x={endX - 12} y={0} width={24} height={H}
                fill="transparent" className={styles.trimHitArea}
                onPointerDown={(e) => startTrimDrag(e, 'end')}
                onDoubleClick={(e) => resetHandle(e, 'end')}
              />
              <rect x={fadeInX - 14} y={H * 0.58} width={28} height={H * 0.42}
                fill="transparent" className={styles.trimHitArea}
                onPointerDown={(e) => startTrimDrag(e, 'fadeIn')}
                onDoubleClick={(e) => resetHandle(e, 'fadeIn')}
              />
              <rect x={fadeOutX - 14} y={H * 0.58} width={28} height={H * 0.42}
                fill="transparent" className={styles.trimHitArea}
                onPointerDown={(e) => startTrimDrag(e, 'fadeOut')}
                onDoubleClick={(e) => resetHandle(e, 'fadeOut')}
              />

              {/* ── START ハンドル タブ (右に張り出す、Logic風) ─────────── */}
              <rect
                x={startX} y={0} width={18} height={15} rx="2"
                className={styles.trimHandle}
                onPointerDown={(e) => startTrimDrag(e, 'start')}
                onDoubleClick={(e) => resetHandle(e, 'start')}
              />
              {/* ── END ハンドル タブ (左に張り出す、Logic風) ───────────── */}
              <rect
                x={endX - 18} y={0} width={18} height={15} rx="2"
                className={styles.trimHandle}
                onPointerDown={(e) => startTrimDrag(e, 'end')}
                onDoubleClick={(e) => resetHandle(e, 'end')}
              />
              {/* ── FADE IN / OUT ハンドル。0ms でも作成できるよう常時表示 ─── */}
              <rect
                x={fadeInX - 7} y={H - 19} width={14} height={13} rx="2"
                className={`${styles.fadeHandle} ${draggingHandle === 'fadeIn' ? styles.fadeHandleActive : ''}`}
                onPointerDown={(e) => startTrimDrag(e, 'fadeIn')}
                onDoubleClick={(e) => resetHandle(e, 'fadeIn')}
              />
              <rect
                x={fadeOutX - 7} y={H - 19} width={14} height={13} rx="2"
                className={`${styles.fadeHandle} ${draggingHandle === 'fadeOut' ? styles.fadeHandleActive : ''}`}
                onPointerDown={(e) => startTrimDrag(e, 'fadeOut')}
                onDoubleClick={(e) => resetHandle(e, 'fadeOut')}
              />
            </>
          )}
        </svg>

        {hasWaveform && (
          <div className={styles.waveChannelLabels} aria-hidden="true">
            {showStereoLanes ? (
              <>
                <span className={styles.waveChannelLabel} style={{ top: '23%' }}>L</span>
                <span className={styles.waveChannelLabel} style={{ top: '72%' }}>R</span>
              </>
            ) : (
              <span className={styles.waveChannelLabel} style={{ top: '47%' }}>
                {hasStereoWaveform ? 'SUM' : 'MONO'}
              </span>
            )}
          </div>
        )}

        {emptyWaveformText && !isSampleDragOver && (
          <div className={styles.emptyWaveformPrompt} aria-hidden="true">
            {emptyWaveformText}
          </div>
        )}

        {/* ── オーバーレイラベル (ビュー範囲外なら非表示) ──────────────── */}
        {hasWaveform && <div className={styles.handleLabels}>
          {/* START: タブの右内側 */}
          {isMsInView(trim.startMs) && (
            <div
              className={styles.handleLabel}
              style={{ left: `${cssPctFromMs(trim.startMs)}%`, transform: 'translateX(2px)' }}
            >
              <span className={styles.handleTitle}>START</span>
              <span className={styles.handleValue}>{formatTrimPercent(trim.startMs, totalMs)}</span>
            </div>
          )}
          {/* END: タブの左内側 */}
          {isMsInView(trim.endMs) && (
            <div
              className={styles.handleLabel}
              style={{ left: `${cssPctFromMs(trim.endMs)}%`, transform: 'translateX(calc(-100% - 2px))' }}
            >
              <span className={styles.handleTitle}>END</span>
              <span className={styles.handleValue}>{formatTrimPercent(trim.endMs, totalMs)}</span>
            </div>
          )}
        </div>}

        {hasWaveform && <div className={styles.fadeLabels}>
          {/* FADE IN — ビュー内のみ。ラベル幅で左右反転判定 */}
          {fadeInLabelVisible && isMsInView(trim.startMs + trim.fadeInMs) && (() => {
            const cssPct = cssPctFromMs(trim.startMs + trim.fadeInMs);
            return (
              <div
                className={styles.fadeLabel}
                style={{
                  left: `${cssPct}%`,
                  // 黄色フェードハンドル (width 14, ±7px) の外側にラベル背景が掛からないように 14px 離す
                  transform: cssPct > 85 ? 'translateX(calc(-100% - 14px))' : 'translateX(14px)',
                }}
              >
                <span className={styles.fadeTitle}>FADE IN</span>
                <span className={styles.fadeValue}>{formatMs(trim.fadeInMs)}</span>
              </div>
            );
          })()}
          {fadeOutLabelVisible && isMsInView(trim.endMs - trim.fadeOutMs) && (() => {
            const cssPct = cssPctFromMs(trim.endMs - trim.fadeOutMs);
            return (
              <div
                className={styles.fadeLabel}
                style={{
                  left: `${cssPct}%`,
                  transform: cssPct < 15 ? 'translateX(14px)' : 'translateX(calc(-100% - 14px))',
                }}
              >
                <span className={styles.fadeTitle}>FADE OUT</span>
                <span className={styles.fadeValue}>{formatMs(trim.fadeOutMs)}</span>
              </div>
            );
          })()}
        </div>}
        {hasWaveform && draggingHandle && (
          <div className={styles.dragValue}>
            {draggingHandle === 'start' && `START ${formatTrimPercent(trim.startMs, totalMs)}`}
            {draggingHandle === 'end' && `END ${formatTrimPercent(trim.endMs, totalMs)}`}
            {draggingHandle === 'fadeIn' && `FADE IN ${formatMs(trim.fadeInMs)}`}
            {draggingHandle === 'fadeOut' && `FADE OUT ${formatMs(trim.fadeOutMs)}`}
          </div>
        )}
        </div>{/* /.waveDisplay */}

        {/* ── 下部レーン: View Range Bar 専用 (フェードハンドルと分離) ── */}
        {hasWaveform && (
          <div
            className={`${styles.waveBottomLane} ${isFitView ? '' : styles.waveBottomLaneActive}`}
          >
            <div
              className={styles.viewRangeBar}
              aria-label="Waveform view range"
            >
              <div
                className={styles.viewRangeHandle}
                style={{
                  left:  `${viewStartPct * 100}%`,
                  width: `${Math.max(0.001, viewEndPct - viewStartPct) * 100}%`,
                }}
                onPointerDown={handleRangePointerDown}
                role="slider"
                aria-valuemin={0}
                aria-valuemax={1}
                aria-valuenow={viewStartPct}
                title="Drag to pan view"
              />
            </div>
          </div>
        )}
        {isSampleDragOver && (
          <div className={styles.dropOverlay} aria-hidden="true">
            {dragOverlayText}
          </div>
        )}
      </div>
    </div>
  );
}

type WaveHandle = 'start' | 'end' | 'fadeIn' | 'fadeOut';

// Memo: skip re-render unless one of the listed props changes. Heavy SVG —
// without this, every parent render rebuilds 600+ path points.
export const WaveformEditor = memo(WaveformEditorComponent);
