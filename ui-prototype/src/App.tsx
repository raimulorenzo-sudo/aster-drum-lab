import {
  useEffect,
  useRef,
  useState,
  useCallback,
  useMemo,
  type CSSProperties,
  type KeyboardEvent as ReactKeyboardEvent,
  type PointerEvent as ReactPointerEvent,
} from 'react';
import styles from './App.module.css';
import { Header } from './components/Header/Header';
import type { HeaderKitItem, UiScale } from './components/Header/Header';
import { TabBar } from './components/TabBar/TabBar';
import type { TabId } from './components/TabBar/TabBar';
import { MixerView } from './components/MixerView/MixerView';
import { PadsView } from './components/PadsView/PadsView';
import { MissingSamplesView } from './components/MissingSamplesView/MissingSamplesView';
import { PadContextMenu } from './components/PadContextMenu/PadContextMenu';
import { PadColorPicker } from './components/PadColorPicker/PadColorPicker';
import {
  INITIAL_PADS,
  MIDI_NOTE_NAMES,
  defaultMidiNoteForPad,
  midiNoteFromParts,
  midiNoteName,
  midiNoteParts,
  pageOfIndex,
} from './data/padData';
import { resettablePadSettings } from './data/parameterSpecs';
import type { PadParams, LayerParams, KitPage, OutputMode, PreviewPlayback } from './types';
import { NEUTRAL_EQ } from './types';
import {
  isJuceAvailable,
  sendToJuce,
  onJuceEvent,
  sendPadPatchToJuce,
  jucePadToReact,
  juceKitToReact,
} from './utils/juceBridge';
import type { JuceKitData, JucePadData } from './utils/juceBridge';
import { FALLBACK_SAMPLE_LENGTH_MS, hasSampleTrimPatch, normalizePadTrimPatch, trimFromPad, trimToNormalized } from './utils/sampleTrim';
import { updateMasterMeter, updatePadMeter } from './utils/meterRegistry';
import { triggerPadFlash } from './utils/padFlashRegistry';
import { updatePadClipLatch } from './utils/clipRegistry';
import { triggerLayerFlash, updateLayerMeter } from './utils/layerLevelRegistry';
import { updateCompMeter } from './utils/compMeterRegistry';
import { updateResourceStats } from './utils/resourceRegistry';
import { audioBufferToPeaks, audioBufferToWaveformChannels } from './utils/waveform';
import { useUndoRedo } from './utils/useUndoRedo';
import { countMissingSamples } from './utils/missingSamples';
import { ensureLayers, patchAddLayer, patchRemoveLayer } from './utils/layerView';

const KIT_STORAGE_KEY = 'aster-drum-lab-kit';
/** Bumped when the meaning of pad.volume changed from "linear gain" to
 *  "fader position". loadKit() migrates older saves on the fly. */
const KIT_STORAGE_VERSION = 2;

import { FADER_UNITY_POS, gainToPosition } from './utils/fader';

/** Fader/knob position that corresponds to 0 dB (unity gain).
 *  This MUST match FADER_UNITY_POS in utils/fader.ts and kUnityPos
 *  in Source/FaderCurve.h so the position, dB label, and scale marks
 *  never drift apart. Re-exported here for backward compatibility. */
export const MASTER_UNITY = FADER_UNITY_POS;
const DESIGN_WIDTH = 1400;
const DESIGN_HEIGHT = 852;
const MIN_VIEWPORT_SCALE = 0.5;
const MAX_VIEWPORT_SCALE = 2;

interface SavedKitState {
  /** Schema version. Missing or < 2 → pad.volume is in legacy linear-gain
   *  units and gets migrated to fader-position on load. */
  version?: number;
  kitName: string;
  outputMode: OutputMode;
  selectedIndex: number;
  page: KitPage;
  pads: PadParams[];
}

interface LoadKitOptions {
  samples: boolean;
  padNamesAndColours: boolean;
  padParameters: boolean;
  mixerSettings: boolean;
  routing: boolean;
}

interface LevelData {
  pads?: number[];
  start?: number;
  masterL?: number;
  masterR?: number;
  /** Per-pad clip latched state from C++ (Mixer tab broadcast). */
  padClips?: boolean[];
  /** Layer levels for the currently selected pad (Pads tab only). */
  layerPad?: number;
  layerLevels?: number[];
  /** Compressor gain-reduction (dB, 正値) per FX-chain slot of the selected layer. */
  compReduction?: number[];
}

interface LayerTriggerData {
  padIndex?: number;
  layers?: number[];
}

interface SystemStatsData {
  cpuPercent?: number;
  sampleBytes?: number;
}

interface DemoState {
  isDemo: boolean;
  durationSeconds: number;
  started: boolean;
  remainingSeconds: number;
  expired: boolean;
  offlineRenderBlocked: boolean;
  kitSavingEnabled: boolean;
}

interface PadTriggerData {
  triggers?: Array<{ index?: number; velocity?: number } | number>;
}

interface MidiLearnedData {
  index?: number;
  note?: number;
}

function routingUndoPatch(current: PadParams, restored: PadParams): Partial<PadParams> {
  const patch: Partial<PadParams> = {};

  if (current.outputAssign !== restored.outputAssign) patch.outputAssign = restored.outputAssign;
  if (current.volume !== restored.volume) patch.volume = restored.volume;
  if (current.pan !== restored.pan) patch.pan = restored.pan;
  if (current.mute !== restored.mute) patch.mute = restored.mute;
  if (current.solo !== restored.solo) patch.solo = restored.solo;

  return patch;
}

function getDroppedFilePath(file: File): string {
  const maybePath = (file as File & { path?: string }).path;
  if (typeof maybePath === 'string' && maybePath.length > 0) return normalizeNativeFilePath(maybePath);
  if (file.webkitRelativePath) return normalizeNativeFilePath(file.webkitRelativePath);
  return '';
}

function normalizeNativeFilePath(path: string): string {
  if (path.startsWith('file://')) {
    try {
      return decodeURIComponent(new URL(path).pathname);
    } catch {
      return '';
    }
  }

  if (
    path.startsWith('/') ||
    path.startsWith('\\\\') ||
    /^[a-zA-Z]:[\\/]/.test(path)
  ) {
    return path;
  }

  return '';
}

function knobDebugEnabled() {
  try {
    if (new URLSearchParams(window.location.search).has('debugKnobs')) return true;
    return localStorage.getItem('ASTER_DEBUG_KNOBS') === '1'
      || Boolean((window as Window & { __ASTER_DEBUG_KNOBS?: boolean }).__ASTER_DEBUG_KNOBS);
  } catch {
    return false;
  }
}

function knobDebugLog(message: string, payload: unknown) {
  if (!knobDebugEnabled()) return;
  try {
    console.info(`[ASTER KNOB TRACE] ${message} ${JSON.stringify(payload)}`);
  } catch {
    console.info(`[ASTER KNOB TRACE] ${message}`);
  }
}

function bytesToBase64(bytes: Uint8Array): string {
  const subChunkSize = 0x8000;
  let binary = '';
  for (let i = 0; i < bytes.length; i += subChunkSize) {
    binary += String.fromCharCode(...bytes.subarray(i, i + subChunkSize));
  }
  return btoa(binary);
}

async function sendSampleBytesToJuce(index: number, file: File, layerIndex: number = 0): Promise<void> {
  const transferId = `${Date.now()}-${index}-${Math.random().toString(36).slice(2)}`;
  const bytes = new Uint8Array(await file.arrayBuffer());
  const rawChunkSize = 192 * 1024;
  const totalChunks = Math.max(1, Math.ceil(bytes.length / rawChunkSize));

  sendToJuce('beginSampleBytesDrop', {
    transferId,
    index,
    layerIndex,
    fileName: file.name,
    totalBytes: bytes.length,
    totalChunks,
  });

  for (let chunkIndex = 0; chunkIndex < totalChunks; chunkIndex += 1) {
    const start = chunkIndex * rawChunkSize;
    const end = Math.min(bytes.length, start + rawChunkSize);
    sendToJuce('sampleBytesChunk', {
      transferId,
      chunkIndex,
      data: bytesToBase64(bytes.subarray(start, end)),
    });
  }

  sendToJuce('finishSampleBytesDrop', { transferId });
}

function swapPadSounds(pads: PadParams[], sourceIndex: number, targetIndex: number): PadParams[] {
  if (
    sourceIndex === targetIndex ||
    sourceIndex < 0 ||
    sourceIndex >= pads.length ||
    targetIndex < 0 ||
    targetIndex >= pads.length
  ) {
    return pads;
  }

  const sourcePad = pads[sourceIndex];
  const targetPad = pads[targetIndex];
  const next = [...pads];

  next[sourceIndex] = {
    ...targetPad,
    midiNote: sourcePad.midiNote,
    outputAssign: sourcePad.outputAssign,
  };
  next[targetIndex] = {
    ...sourcePad,
    midiNote: targetPad.midiNote,
    outputAssign: targetPad.outputAssign,
  };

  return next;
}

async function analyzeBrowserAudioFile(file: File): Promise<Partial<PadParams>> {
  try {
    const AudioContextClass = window.AudioContext || (window as unknown as { webkitAudioContext?: typeof AudioContext }).webkitAudioContext;
    if (!AudioContextClass) return {};

    const context = new AudioContextClass();
    const arrayBuffer = await file.arrayBuffer();
    const audioBuffer = await context.decodeAudioData(arrayBuffer);
    await context.close();

    return {
      sampleLengthMs: audioBuffer.duration * 1000,
      startMs: 0,
      endMs: audioBuffer.duration * 1000,
      fadeInMs: 0,
      fadeOutMs: 0,
      waveformPeaks: audioBufferToPeaks(audioBuffer, 600),
      waveformChannels: audioBufferToWaveformChannels(audioBuffer, 2000),
    };
  } catch {
    return {};
  }
}

export default function App() {
  // ── State ────────────────────────────────────────────────────────────
  const [pads, setPads] = useState<PadParams[]>(INITIAL_PADS);
  const [demoState, setDemoState] = useState<DemoState>({
    isDemo: false,
    durationSeconds: 1200,
    started: false,
    remainingSeconds: 1200,
    expired: false,
    offlineRenderBlocked: false,
    kitSavingEnabled: true,
  });
  const demoWarningsShownRef = useRef({ fiveMinutes: false, oneMinute: false });
  /** Always points to the latest pads state — safe to read inside callbacks */
  const padsRef = useRef(pads);
  const [selectedIndex, setSelectedIndex] = useState<number>(4); // 初期: OPEN HAT

  // ── Undo / Redo (pads スナップショット履歴) ─────────────────────────
  // ノブ連続変更や wheel スクロールで履歴が爆発しないよう 350ms debounce。
  const restorePadsForUndo = (restoredPads: PadParams[]) => {
    const currentPads = padsRef.current;
    restoredPads.forEach((restored, index) => {
      const current = currentPads[index];
      if (!current) return;
      const patch = routingUndoPatch(current, restored);
      if (Object.keys(patch).length > 0) sendPadPatchToJuce(index, patch, current);
    });
    setKitDirty(true);
  };
  const { undo, redo, canUndo, canRedo } = useUndoRedo<PadParams[]>(
    pads,
    setPads,
    { onRestore: restorePadsForUndo },
  );

  // Cmd/Ctrl+Z = Undo / Cmd/Ctrl+Shift+Z = Redo (DAW 慣習)
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      const mod = e.metaKey || e.ctrlKey;
      if (!mod) return;
      // text input 内では操作を奪わない
      const target = e.target as HTMLElement | null;
      const tag = target?.tagName?.toLowerCase();
      if (tag === 'input' || tag === 'textarea' || target?.isContentEditable) return;
      if (e.key === 'z' && !e.shiftKey) {
        e.preventDefault();
        if (canUndo) undo();
      } else if ((e.key === 'z' && e.shiftKey) || e.key === 'y') {
        e.preventDefault();
        if (canRedo) redo();
      }
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [undo, redo, canUndo, canRedo]);
  // Live MIDI velocity (0..127) ごく直近に発音されたパッドだけ値を持つ。
  // C++ から padTriggers イベントを受けて更新し、~700ms 後にクリアして fade-out。
  // PadParams に乗せず別 state にすることで、Kit 保存や JUCE patch flow と干渉しない。
  const [liveVelocities, setLiveVelocities] = useState<Record<number, number>>({});
  const liveVelocityTimers = useRef<Record<number, number>>({});
  const [page, setPage] = useState<KitPage>('A');
  const [activeTab, setActiveTab] = useState<TabId>('PADS');
  const [kitName, setKitName] = useState<string>('Default');
  const [kitDirty, setKitDirty] = useState(false);
  const [currentKitPath, setCurrentKitPath] = useState('');
  const [kitItems, setKitItems] = useState<HeaderKitItem[]>([
    { name: 'Empty Kit', path: '', isDefault: true },
  ]);
  const [pendingKitSwitch, setPendingKitSwitch] = useState<{ path: string } | null>(null);
  const [loadKitTarget, setLoadKitTarget] = useState<{ path: string } | null>(null);
  const [loadKitOptions, setLoadKitOptions] = useState<LoadKitOptions>({
    samples: true,
    padNamesAndColours: true,
    padParameters: true,
    mixerSettings: true,
    routing: true,
  });
  const [outputMode, setOutputMode] = useState<OutputMode>('48Outs');
  const [uiScale, setUiScale] = useState<UiScale>(1);
  const [viewportScale, setViewportScale] = useState(1);
  const [isWindowResizing, setIsWindowResizing] = useState(false);
  const resizeDragRef = useRef<{
    pointerId: number;
    startScreenX: number;
    startScreenY: number;
    startScale: number;
  } | null>(null);
  const pendingResizeScaleRef = useRef<number | null>(null);
  const resizeFrameRef = useRef<number | null>(null);
  const [masterKnob, setMasterKnob] = useState(MASTER_UNITY);
  const [masterClipHit, setMasterClipHit] = useState(false);
  const [previewPlayback, setPreviewPlayback] = useState<PreviewPlayback>({
    isPreviewPlaying: false,
    padIndex: -1,
    previewStartedAt: 0,
    previewDurationMs: 0,
    previewStartPercent: 0,
    previewEndPercent: 1,
    reverseEnabled: false,
    triggerId: 0,
  });

  // ── レベルメーター (C++ からの実音量) ────────────────────────────────
  // Visual meter movement is written directly to meter DOM refs via
  // meterRegistry, so levelData does not re-render the App tree.
  // Peak hold refs (ピーク最大値 & 最後に更新した時刻)
  const padPeakHoldRef  = useRef<number[]>(new Array(48).fill(0));
  const padPeakTimeRef  = useRef<number[]>(new Array(48).fill(0));
  const padLevelsRef    = useRef<number[]>(new Array(48).fill(0));
  const masterPeakRef   = useRef(0);
  const masterPeakTime  = useRef(0);
  const masterLevelRef   = useRef(0);
  // masterClipHit setState のガード用 ref。levelData 受信時に同値 setState を
  // 連発しないようにし、React reconciliation を毎フレーム走らせない。
  // resetMasterClip ハンドラと App state の双方向で同期する。
  const masterClipHitRef = useRef(false);
  const activeTabRef    = useRef<TabId>('PADS');
  const selectedIndexRef = useRef(selectedIndex);
  // Mixer メーターのフラッシュループで「現在ページ」の 16 Pad だけ処理するための ref。
  // 0=A 1=B 2=C → 16 Pad オフセットに変換して使う。
  const pageRef = useRef<0 | 1 | 2>(0);

  const [padClipboard, setPadClipboard] = useState<PadParams | null>(null);
  const [contextMenu, setContextMenu] = useState<{ index: number; x: number; y: number } | null>(null);
  const [colorPickerTarget, setColorPickerTarget] = useState<{ index: number; initialColor: string; startedFromAuto: boolean } | null>(null);
  const [renameDialog, setRenameDialog] = useState<{ index: number; draft: string } | null>(null);
  const [midiNoteDialog, setMidiNoteDialog] = useState<{ index: number; draftNote: number; learning: boolean; warning: string } | null>(null);
  const [toastMessage, setToastMessage] = useState('');
  const [fileTarget, setFileTarget] = useState<{ index: number; relink: boolean } | null>(null);
  const fileInputRef = useRef<HTMLInputElement | null>(null);

  // ── Refs for bridge callbacks (avoid stale closures) ─────────────────
  useEffect(() => { padsRef.current = pads; }, [pads]);
  useEffect(() => {
    const unsubscribe = onJuceEvent('demoState', raw => {
      const next = raw as Partial<DemoState>;
      if (typeof next.isDemo !== 'boolean') return;
      setDemoState({
        isDemo: next.isDemo,
        durationSeconds: typeof next.durationSeconds === 'number' ? next.durationSeconds : 1200,
        started: Boolean(next.started),
        remainingSeconds: typeof next.remainingSeconds === 'number' ? next.remainingSeconds : 1200,
        expired: Boolean(next.expired),
        offlineRenderBlocked: Boolean(next.offlineRenderBlocked),
        kitSavingEnabled: next.kitSavingEnabled !== false,
      });
    });
    sendToJuce('requestDemoState', {});
    return unsubscribe;
  }, []);
  useEffect(() => { activeTabRef.current = activeTab; }, [activeTab]);

  useEffect(() => {
    if (!demoState.isDemo || !demoState.started || demoState.expired) return;
    if (demoState.durationSeconds >= 300
        && demoState.remainingSeconds <= 60
        && !demoWarningsShownRef.current.oneMinute) {
      demoWarningsShownRef.current.oneMinute = true;
      setToastMessage('Demo: 1 minute of audio time remaining.');
    } else if (demoState.durationSeconds >= 300
               && demoState.remainingSeconds <= 300
               && !demoWarningsShownRef.current.fiveMinutes) {
      demoWarningsShownRef.current.fiveMinutes = true;
      setToastMessage('Demo: 5 minutes of audio time remaining.');
    }
  }, [demoState]);

  useEffect(() => { selectedIndexRef.current = selectedIndex; }, [selectedIndex]);
  useEffect(() => {
    pageRef.current = page === 'A' ? 0 : page === 'B' ? 1 : 2;
  }, [page]);

  useEffect(() => {
    const updateViewportScale = () => {
      const scale = Math.min(
        window.innerWidth / DESIGN_WIDTH,
        window.innerHeight / DESIGN_HEIGHT,
      );
      setViewportScale(Math.max(MIN_VIEWPORT_SCALE, Math.min(MAX_VIEWPORT_SCALE, scale)));
    };

    updateViewportScale();
    window.addEventListener('resize', updateViewportScale);
    return () => window.removeEventListener('resize', updateViewportScale);
  }, []);

  const requestEditorScale = useCallback((scale: number) => {
    pendingResizeScaleRef.current = Math.max(
      MIN_VIEWPORT_SCALE,
      Math.min(MAX_VIEWPORT_SCALE, scale),
    );

    if (resizeFrameRef.current !== null) return;
    resizeFrameRef.current = window.requestAnimationFrame(() => {
      resizeFrameRef.current = null;
      const pendingScale = pendingResizeScaleRef.current;
      pendingResizeScaleRef.current = null;
      if (pendingScale !== null)
        sendToJuce('setUiScale', { scale: pendingScale });
    });
  }, []);

  const flushEditorScale = useCallback(() => {
    if (resizeFrameRef.current !== null) {
      window.cancelAnimationFrame(resizeFrameRef.current);
      resizeFrameRef.current = null;
    }

    const pendingScale = pendingResizeScaleRef.current;
    pendingResizeScaleRef.current = null;
    if (pendingScale !== null)
      sendToJuce('setUiScale', { scale: pendingScale });
  }, []);

  useEffect(() => () => {
    if (resizeFrameRef.current !== null)
      window.cancelAnimationFrame(resizeFrameRef.current);
  }, []);

  const handleResizePointerDown = useCallback((event: ReactPointerEvent<HTMLDivElement>) => {
    event.preventDefault();
    event.stopPropagation();
    event.currentTarget.setPointerCapture(event.pointerId);

    resizeDragRef.current = {
      pointerId: event.pointerId,
      startScreenX: event.screenX,
      startScreenY: event.screenY,
      startScale: Math.max(
        MIN_VIEWPORT_SCALE,
        Math.min(
          MAX_VIEWPORT_SCALE,
          Math.min(window.innerWidth / DESIGN_WIDTH, window.innerHeight / DESIGN_HEIGHT),
        ),
      ),
    };
    setIsWindowResizing(true);
  }, []);

  const handleResizePointerMove = useCallback((event: ReactPointerEvent<HTMLDivElement>) => {
    const drag = resizeDragRef.current;
    if (!drag || drag.pointerId !== event.pointerId) return;

    event.preventDefault();
    const deltaX = event.screenX - drag.startScreenX;
    const deltaY = event.screenY - drag.startScreenY;
    const projectedScaleDelta = (
      deltaX * DESIGN_WIDTH + deltaY * DESIGN_HEIGHT
    ) / (
      DESIGN_WIDTH * DESIGN_WIDTH + DESIGN_HEIGHT * DESIGN_HEIGHT
    );
    requestEditorScale(drag.startScale + projectedScaleDelta);
  }, [requestEditorScale]);

  const finishResizeDrag = useCallback((event: ReactPointerEvent<HTMLDivElement>) => {
    const drag = resizeDragRef.current;
    if (!drag || drag.pointerId !== event.pointerId) return;

    resizeDragRef.current = null;
    flushEditorScale();
    setIsWindowResizing(false);
    if (event.currentTarget.hasPointerCapture(event.pointerId))
      event.currentTarget.releasePointerCapture(event.pointerId);
  }, [flushEditorScale]);

  const handleResizeKeyDown = useCallback((event: ReactKeyboardEvent<HTMLDivElement>) => {
    const direction = event.key === 'ArrowUp' || event.key === 'ArrowRight'
      ? 1
      : event.key === 'ArrowDown' || event.key === 'ArrowLeft'
        ? -1
        : 0;
    if (direction === 0) return;

    event.preventDefault();
    const currentScale = Math.min(
      window.innerWidth / DESIGN_WIDTH,
      window.innerHeight / DESIGN_HEIGHT,
    );
    requestEditorScale(currentScale + direction * 0.05);
  }, [requestEditorScale]);

  /** Index of the pad currently being auditoned (-1 = none) */
  const auditionedPadRef = useRef(-1);

  const selected = pads[selectedIndex];

  /** Missing pads の数（タブバッジ用） */
  const missingCount = useMemo(
    () => countMissingSamples(pads),
    [pads],
  );

  useEffect(() => {
    if (missingCount > 0 || activeTab !== 'MISSING') return;
    setActiveTab('PADS');
    sendToJuce('setTab', { tab: 'PADS' });
  }, [activeTab, missingCount]);

  // ── JUCE bridge setup ────────────────────────────────────────────────
  useEffect(() => {
    // kitData: C++ sends the full kit state (on startup and after kit loads)
    const unsubKit = onJuceEvent('kitData', (raw) => {
      const kit = raw as JuceKitData;
      if (!kit || !Array.isArray(kit.pads) || kit.pads.length !== 48) return;

      const { pads: newPads, page: newPage, selectedIndex: newIdx,
              kitName: newKitName, outputMode: newOutputMode } =
        juceKitToReact(kit, padsRef.current);

      setPads(newPads);
      setPage(newPage);
      setSelectedIndex(newIdx);
      setKitName(newKitName);
      setKitDirty(Boolean(kit.kitDirty));
      setCurrentKitPath(kit.currentKitPath ?? '');
      setOutputMode(newOutputMode);
      if (typeof kit.masterVolume === 'number')
        setMasterKnob(Math.max(0, Math.min(1, kit.masterVolume)));
    });

    const unsubKitList = onJuceEvent('kitList', (raw) => {
      const list = raw as { items?: HeaderKitItem[]; currentKitPath?: string };
      const nextItems = Array.isArray(list.items) && list.items.length > 0
        ? list.items
        : [{ name: 'Empty Kit', path: '', isDefault: true }];
      setKitItems(nextItems);
      setCurrentKitPath(list.currentKitPath ?? '');
    });

    // padUpdated: C++ sends a single-pad update (after parameter changes)
    const unsubPad = onJuceEvent('padUpdated', (raw) => {
      const update = raw as { index: number; pad: JucePadData };
      if (!update || typeof update.index !== 'number' || !update.pad) return;
      const idx = update.index;
      if (idx < 0 || idx >= 48) return;

      setPads(prev => {
        const next = [...prev];
        next[idx] = jucePadToReact(update.pad, prev[idx]);
        return next;
      });
      setKitDirty(true);
    });

    // ── レベルメーター 受信 + DOM 反映を rAF で集約 ──────────────────────
    //
    // 設計理由 (重要):
    //   JUCE → JS の IPC イベントは非同期で届く。JS スレッドが他作業 (GC,
    //   reconciler 等) で 1 フレーム詰まると、levelData が 2-3 個まとめて
    //   キューに溜まり、解放時に連続実行される (= バースト)。
    //   各イベントが同期で DOM 操作すると、その瞬間にカクつきとして見える。
    //
    //   解決: 受信ハンドラは "ref に最新値を書く" だけ。実 DOM 反映は
    //   共通 rAF ループで「フレームあたり 1 回だけ」行う。これで:
    //     - バースト時もフレーム内で最新値だけが反映される (中間値は drop)
    //     - vsync 同期で動くので画面 tearing も発生しない
    //     - Peak hold decay もこのループで同時処理 (旧 setTimeout(60) を排除)
    const PEAK_HOLD_MS = 250;
    const METER_RELEASE_DB_PER_SECOND = 36;
    const PEAK_RELEASE_DB_PER_SECOND = 32;
    const METER_FLOOR_DB = -60;
    const METER_FLOOR = 10 ** (METER_FLOOR_DB / 20);

    // 受信した最新値 (1 フレーム内で何度上書きされても良い)
    let pendingMasterLevel = 0;
    let pendingMasterHasFrame = false;
    let pendingPadStart = -1;
    let pendingPadLevels: number[] | null = null;
    let masterTargetLevel = 0;
    let masterDisplayLevel = 0;
    let lastFrameTime = 0;
    let lastLevelDataTime = 0;
    const padTargetLevels = new Array(48).fill(0);
    const padDisplayLevels = new Array(48).fill(0);

    // Compressor GR メーター (選択 Layer の FX スロット最大 16)。
    // ballistics: target へ即座にアタック、フレームごとに decay でリリース。
    const MAX_FX_SLOTS = 16;
    const COMP_GR_DECAY = 0.965;       // 反応は軽く、ピークラインで読み取りを補う
    let pendingCompReduction: number[] | null = null;
    const compDisplayDb = new Array(MAX_FX_SLOTS).fill(0);

    // Layer level smoothing (selected pad only, 8 slots)
    const MAX_LAYER_SLOTS = 8;
    let pendingLayerPad = -1;
    let pendingLayerLevels: number[] | null = null;
    let layerForPad = -1;
    const layerTargetLevels  = new Array(MAX_LAYER_SLOTS).fill(0);
    const layerDisplayLevels = new Array(MAX_LAYER_SLOTS).fill(0);

    let rafId = 0;
    let rafActive = false;

    // (masterClipHitRef は component-level ref として上で宣言済み)

    const hasDecayWork = () => {
      if (masterPeakRef.current > 0.001) return true;
      // 可視 16 Pad のみチェック (見えない Pad の peakHold が残っていても描画不要)
      const ps = pageRef.current * 16;
      for (let i = ps; i < ps + 16; i++) {
        if (padPeakHoldRef.current[i] > 0.001) return true;
      }
      return false;
    };

    const hasMeterWork = () => {
      if (masterDisplayLevel > METER_FLOOR || masterTargetLevel > METER_FLOOR) return true;
      const ps = pageRef.current * 16;
      for (let i = ps; i < ps + 16; i++) {
        if (padDisplayLevels[i] > METER_FLOOR || padTargetLevels[i] > METER_FLOOR) return true;
      }
      for (let i = 0; i < MAX_LAYER_SLOTS; i++) {
        if (layerDisplayLevels[i] > METER_FLOOR || layerTargetLevels[i] > METER_FLOOR) return true;
      }
      return false;
    };

    const releaseLevelByDb = (current: number, dbPerSecond: number, dtMs: number) => {
      if (current <= METER_FLOOR) return 0;
      const nextDb = 20 * Math.log10(current) - dbPerSecond * (dtMs / 1000);
      return nextDb <= METER_FLOOR_DB ? 0 : 10 ** (nextDb / 20);
    };

    const smoothMeterLevel = (current: number, target: number, dtMs: number) => {
      if (target >= current) return target;

      // Professional peak meters specify fall-back in dB/second. Applying the
      // release in display space keeps the visual speed independent of sample
      // length and signal amplitude while never dropping below the true level.
      return Math.max(target, releaseLevelByDb(current, METER_RELEASE_DB_PER_SECOND, dtMs));
    };

    const flushFrame = () => {
      rafId = 0;
      const now = performance.now();
      const dtMs = lastFrameTime > 0 ? Math.min(80, Math.max(1, now - lastFrameTime)) : 16.7;
      lastFrameTime = now;

      if (lastLevelDataTime > 0 && now - lastLevelDataTime > 120) {
        masterTargetLevel = 0;
        for (let i = 0; i < 48; i++) padTargetLevels[i] = 0;
      }

      // 1) 受信済みの新フレームを表示用 target へ反映 (1 フレーム 1 回だけ)
      if (pendingMasterHasFrame) {
        const ml = pendingMasterLevel;
        lastLevelDataTime = now;
        masterTargetLevel = ml;

        if (ml > 1.0 && !masterClipHitRef.current) {
          masterClipHitRef.current = true;
          setMasterClipHit(true);
        }

        if (pendingPadLevels && activeTabRef.current === 'MIXER') {
          const start = pendingPadStart;
          const arr = pendingPadLevels;
          for (let offset = 0; offset < arr.length; offset++) {
            const idx = start + offset;
            if (idx < 0 || idx >= 48) continue;
            const level = arr[offset];
            padTargetLevels[idx] = level;
            if (level > padPeakHoldRef.current[idx]) {
              padPeakHoldRef.current[idx] = level;
              padPeakTimeRef.current[idx] = now;
            }
          }
        }

        if (ml > masterPeakRef.current) {
          masterPeakRef.current = ml;
          masterPeakTime.current = now;
        }

        pendingMasterHasFrame = false;
        pendingPadLevels = null;

        // Layer levels for selected pad
        if (pendingLayerLevels !== null) {
          if (pendingLayerPad !== layerForPad) {
            // Pad changed — reset layer meters immediately so stale values don't flash
            layerTargetLevels.fill(0);
            layerDisplayLevels.fill(0);
            compDisplayDb.fill(0);
            layerForPad = pendingLayerPad;
          }
          const la = pendingLayerLevels;
          for (let li = 0; li < la.length && li < MAX_LAYER_SLOTS; li++) {
            const lv = la[li];
            layerTargetLevels[li] = lv;
          }
          // Zero out slots beyond what C++ sent (layer count can shrink)
          for (let li = la.length; li < MAX_LAYER_SLOTS; li++) {
            layerTargetLevels[li] = 0;
          }
          pendingLayerLevels = null;
          pendingLayerPad = -1;
        }
      }

      // 2) 表示用メーターを attack/release で滑らかに追従させる。
      let didMeterMove = false;
      masterDisplayLevel = smoothMeterLevel(
        masterDisplayLevel,
        masterTargetLevel,
        dtMs,
      );
      masterLevelRef.current = masterDisplayLevel;
      updateMasterMeter(masterDisplayLevel, masterPeakRef.current);
      didMeterMove = masterDisplayLevel > METER_FLOOR || masterTargetLevel > METER_FLOOR;

      if (activeTabRef.current === 'MIXER') {
        // v7+ perf: 可視ページの 16 Pad のみ smooth + DOM 書き込みする。
        // 旧実装は 0..47 を全 iteration していたが、見えない 32 Pad の
        // smoothMeterLevel / updatePadMeter は無駄 (C++ 側もページ分しか送らない)。
        const pageStart = pageRef.current * 16;
        const pageEnd   = pageStart + 16;
        for (let i = pageStart; i < pageEnd; i++) {
          const next = smoothMeterLevel(
            padDisplayLevels[i],
            padTargetLevels[i],
            dtMs,
          );
          if (next !== padDisplayLevels[i]) didMeterMove = true;
          padDisplayLevels[i] = next;
          padLevelsRef.current[i] = next;
          updatePadMeter(i, next, padPeakHoldRef.current[i]);
        }
      }

      // Layer tab meters — only when on Pads view and we have a valid pad
      if (activeTabRef.current === 'PADS' && layerForPad >= 0) {
        for (let li = 0; li < MAX_LAYER_SLOTS; li++) {
          const next = smoothMeterLevel(
            layerDisplayLevels[li],
            layerTargetLevels[li],
            dtMs,
          );
          if (next !== layerDisplayLevels[li]) didMeterMove = true;
          layerDisplayLevels[li] = next;
          updateLayerMeter(layerForPad, li, next);
        }

        // Compressor GR メーター (fast attack / slow release)
        for (let si = 0; si < MAX_FX_SLOTS; si++) {
          const target = pendingCompReduction && si < pendingCompReduction.length
            ? pendingCompReduction[si] : 0;
          const decayed = compDisplayDb[si] * COMP_GR_DECAY;
          const next = target >= decayed ? target : decayed;  // 即アタック / 緩リリース
          if (Math.abs(next - compDisplayDb[si]) > 0.01) didMeterMove = true;
          compDisplayDb[si] = next;
          updateCompMeter(si, next);
        }
      }

      // 3) Peak hold line: independent hold and dB/second release.
      let didDecay = false;
      if (activeTabRef.current === 'MIXER') {
        const pageStart = pageRef.current * 16;
        const pageEnd   = pageStart + 16;
        for (let i = pageStart; i < pageEnd; i++) {
          if (padPeakHoldRef.current[i] > 0.001 &&
              now - padPeakTimeRef.current[i] > PEAK_HOLD_MS) {
            padPeakHoldRef.current[i] = releaseLevelByDb(
              padPeakHoldRef.current[i],
              PEAK_RELEASE_DB_PER_SECOND,
              dtMs,
            );
            updatePadMeter(i, padDisplayLevels[i], padPeakHoldRef.current[i]);
            didDecay = true;
          }
        }
      }

      if (masterPeakRef.current > 0.001 &&
          now - masterPeakTime.current > PEAK_HOLD_MS) {
        masterPeakRef.current = releaseLevelByDb(
          masterPeakRef.current,
          PEAK_RELEASE_DB_PER_SECOND,
          dtMs,
        );
        updateMasterMeter(masterDisplayLevel, masterPeakRef.current);
        didDecay = true;
      }

      // 4) 次フレームが必要か判定
      if (pendingMasterHasFrame || didMeterMove || didDecay || hasMeterWork() || hasDecayWork()) {
        rafId = window.requestAnimationFrame(flushFrame);
      } else {
        rafActive = false;
        lastFrameTime = 0;
      }
    };

    const scheduleFlush = () => {
      if (!rafActive) {
        rafActive = true;
        rafId = window.requestAnimationFrame(flushFrame);
      }
    };

    const unsubLevels = onJuceEvent('levelData', (raw) => {
      const data = raw as LevelData;
      if (!data) return;

      // 受信ハンドラはひたすら ref に最新値を貯めるだけ
      pendingMasterLevel = Math.max(data.masterL ?? 0, data.masterR ?? 0);
      pendingMasterHasFrame = true;

      if (Array.isArray(data.pads) && data.pads.length > 0) {
        pendingPadStart = Math.max(0, Math.min(47, data.start ?? 0));
        pendingPadLevels = data.pads;
      }

      // Per-pad clip latch (C++ から bool[] で受信、DOM 直更新)
      if (Array.isArray(data.padClips) && data.padClips.length > 0) {
        const startIdx = Math.max(0, Math.min(47, data.start ?? 0));
        for (let i = 0; i < data.padClips.length; i++) {
          const idx = startIdx + i;
          if (idx >= 0 && idx < 48) {
            updatePadClipLatch(idx, !!data.padClips[i]);
          }
        }
      }

      if (typeof data.layerPad === 'number' && Array.isArray(data.layerLevels) && data.layerLevels.length > 0) {
        pendingLayerPad = data.layerPad;
        pendingLayerLevels = data.layerLevels;
      }

      pendingCompReduction = Array.isArray(data.compReduction) ? data.compReduction : null;

      scheduleFlush();
    });

    const unsubSystemStats = onJuceEvent('systemStats', (raw) => {
      const data = raw as SystemStatsData;
      if (!data) return;
      updateResourceStats(data.cpuPercent ?? 0, data.sampleBytes ?? 0);
    });

    const unsubPadTriggers = onJuceEvent('padTriggers', (raw) => {
      const data = raw as PadTriggerData;
      if (!data || !Array.isArray(data.triggers)) return;

      for (const trigger of data.triggers) {
        const index = typeof trigger === 'number' ? trigger : trigger.index;
        if (typeof index === 'number' && index >= 0 && index < 48) {
          triggerPadFlash(index);

          // Live velocity (0..1 from C++) を 0..127 に変換して短時間保持。
          // VelocityRangeSlider の縦線マーカー表示に使う。
          const velocity01 = typeof trigger === 'object' && typeof trigger.velocity === 'number'
            ? trigger.velocity
            : null;
          if (velocity01 !== null) {
            const vel127 = Math.max(0, Math.min(127, Math.round(velocity01 * 127)));
            setLiveVelocities(prev => ({ ...prev, [index]: vel127 }));
            // 既存タイマーをクリアしてから新規スケジュール (チャタリング防止)
            const existing = liveVelocityTimers.current[index];
            if (existing) window.clearTimeout(existing);
            liveVelocityTimers.current[index] = window.setTimeout(() => {
              setLiveVelocities(prev => {
                const next = { ...prev };
                delete next[index];
                return next;
              });
              delete liveVelocityTimers.current[index];
            }, 700);
          }
        }
      }
    });

    // layerTriggers: C++ consumed layerTriggerLevels and found at least one fired.
    // Immediately trigger the flash on those specific layer tabs (no RAF delay needed).
    const unsubLayerTriggers = onJuceEvent('layerTriggers', (raw) => {
      const data = raw as LayerTriggerData;
      if (!data || typeof data.padIndex !== 'number' || !Array.isArray(data.layers)) return;
      const { padIndex, layers } = data;
      for (let li = 0; li < layers.length; li++) {
        if ((layers[li] ?? 0) > 0.001) {
          triggerLayerFlash(padIndex, li);
        }
      }
    });

    const unsubMidiLearned = onJuceEvent('midiLearned', (raw) => {
      const data = raw as MidiLearnedData;
      const index = data?.index;
      const note = data?.note;
      if (
        typeof index !== 'number' ||
        typeof note !== 'number' ||
        index < 0 ||
        index >= 48 ||
        note < 0 ||
        note > 127
      ) {
        return;
      }

      const duplicateIndex = padsRef.current.findIndex((pad, padIndex) =>
        padIndex !== index && pad.midiNote === note,
      );
      const shouldSwap = duplicateIndex >= 0;

      sendPadPatchToJuce(index, { midiNote: note }, padsRef.current[index]);
      setPads(prev => {
        const currentNote = prev[index]?.midiNote;
        if (duplicateIndex < 0 || currentNote === undefined) {
          return prev.map((pad, padIndex) =>
            padIndex === index ? { ...pad, midiNote: note } : pad,
          );
        }

        return prev.map((pad, padIndex) => {
          if (padIndex === index) return { ...pad, midiNote: note };
          if (padIndex === duplicateIndex) return { ...pad, midiNote: currentNote };
          return pad;
        });
      });
      if (shouldSwap) setToastMessage('MIDI notes swapped');
      setKitDirty(true);
      setMidiNoteDialog(prev => prev && prev.index === index
        ? { ...prev, draftNote: note, learning: false, warning: '' }
        : prev);
    });

    // Global pointer release → stop Gate audio and its waveform playhead together.
    const handlePointerRelease = () => {
      const idx = auditionedPadRef.current;
      if (idx >= 0) {
        sendToJuce('auditionOff', { index: idx });
        if (padsRef.current[idx]?.playMode === 'Gate') {
          setPreviewPlayback(prev =>
            prev.isPreviewPlaying && prev.padIndex === idx
              ? { ...prev, isPreviewPlaying: false }
              : prev,
          );
        }
        auditionedPadRef.current = -1;
      }
    };
    window.addEventListener('pointerup', handlePointerRelease);
    window.addEventListener('pointercancel', handlePointerRelease);

    // Tell C++ we're ready — it will respond with kitData
    sendToJuce('ready', {});

    return () => {
      unsubKit();
      unsubKitList();
      unsubPad();
      unsubLevels();
      unsubSystemStats();
      unsubPadTriggers();
      unsubLayerTriggers();
      unsubMidiLearned();
      if (rafId !== 0) window.cancelAnimationFrame(rafId);
      window.removeEventListener('pointerup', handlePointerRelease);
      window.removeEventListener('pointercancel', handlePointerRelease);
    };
  }, []); // run once on mount

  // ── 個別 Pad パラメータを部分更新するヘルパー ────────────────────────
  const updatePad = useCallback((index: number, patch: Partial<PadParams>) => {
    const currentPad = padsRef.current[index];
    if (!currentPad) return;

    const safePatch: Partial<PadParams> = hasSampleTrimPatch(patch)
      ? { ...patch, ...normalizePadTrimPatch(currentPad, patch) }
      : patch;

    if (knobDebugEnabled()) {
      knobDebugLog('updatePad', {
        index,
        selectedIndex: selectedIndexRef.current,
        selectedMismatch: index !== selectedIndexRef.current,
        patch,
        safePatch,
        currentPadName: currentPad.padName,
      });
    }

    const midiDuplicateIndex = safePatch.midiNote === undefined
      ? -1
      : padsRef.current.findIndex((pad, padIndex) =>
          padIndex !== index && pad.midiNote === safePatch.midiNote,
        );

    // 1. Send to C++ before state update so currentPad is still the old value
    sendPadPatchToJuce(index, safePatch, currentPad);
    // 2. Update React state
    setPads(prev => {
      if (safePatch.midiNote === undefined) {
        return prev.map((p, i) => (i === index ? { ...p, ...safePatch } : p));
      }

      const currentNote = prev[index]?.midiNote;
      if (midiDuplicateIndex < 0 || currentNote === undefined) {
        return prev.map((p, i) => (i === index ? { ...p, ...safePatch } : p));
      }

      return prev.map((p, i) => {
        if (i === index) return { ...p, ...safePatch };
        if (i === midiDuplicateIndex) return { ...p, midiNote: currentNote };
        return p;
      });
    });
    if (midiDuplicateIndex >= 0) setToastMessage('MIDI notes swapped');
    setKitDirty(true);
  }, []);

  const updateSelected = useCallback(
    (patch: Partial<PadParams>) => updatePad(selectedIndex, patch),
    [selectedIndex, updatePad],
  );

  // ── 複数 Pad を 1 トランザクションで部分更新 (Mixer の multi-selection 編集) ──
  // 各 Pad を JUCE へ送信しつつ、React state は 1 回の setPads でまとめて更新する。
  // これにより useUndoRedo の debounce スナップショットが「1 ジェスチャ = 1 Undo」に
  // 収束する (個別 updatePad を N 回呼ぶより re-render も少ない)。
  // 想定する patch は padVolume / padPan / outputAssign のみ (midiNote / trim 等の特殊処理は
  // 含めない — Mixer の一括編集はこの 3 つに限定)。
  const updatePads = useCallback(
    (changes: { index: number; patch: Partial<PadParams> }[]) => {
      if (changes.length === 0) return;

      for (const { index, patch } of changes) {
        const currentPad = padsRef.current[index];
        if (currentPad) sendPadPatchToJuce(index, patch, currentPad);
      }

      const patchByIndex = new Map<number, Partial<PadParams>>();
      for (const { index, patch } of changes) patchByIndex.set(index, patch);

      setPads(prev => prev.map((p, i) => {
        const patch = patchByIndex.get(i);
        return patch ? { ...p, ...patch } : p;
      }));
      setKitDirty(true);
    },
    [],
  );

  const selectPadWithoutAudition = useCallback((index: number) => {
    setSelectedIndex(index);
    setPage(pageOfIndex(index));
    sendToJuce('selectPad', { index });
  }, []);

  const selectPad = useCallback((index: number) => {
    const pad = padsRef.current[index];
    selectPadWithoutAudition(index);
    // Audition: trigger sample preview
    auditionedPadRef.current = index;
    sendToJuce('audition', { index, velocity: 0.9 });

    if (pad?.sampleFileName && !pad.sampleMissing) {
      const trim = trimFromPad(pad);
      const normalized = trimToNormalized(trim);
      setPreviewPlayback(prev => ({
        isPreviewPlaying: true,
        padIndex: index,
        previewStartedAt: performance.now(),
        previewDurationMs: Math.max(30, trim.endMs - trim.startMs),
        previewStartPercent: normalized.startPosition,
        previewEndPercent: normalized.endPosition,
        reverseEnabled: pad.reverse,
        triggerId: prev.triggerId + 1,
      }));
    } else {
      setPreviewPlayback(prev => ({ ...prev, isPreviewPlaying: false }));
    }
  }, [selectPadWithoutAudition]);

  const handlePreviewFinished = useCallback((triggerId: number) => {
    setPreviewPlayback(prev =>
      prev.triggerId === triggerId
        ? { ...prev, isPreviewPlaying: false }
        : prev,
    );
  }, []);

  const auditionSelectedLayerFromWaveform = useCallback((layerIndex: number) => {
    const pad = padsRef.current[selectedIndex];
    if (!pad) return;
    const layers = ensureLayers(pad);
    const safeLayerIndex = Math.max(0, Math.min(layers.length - 1, layerIndex));
    const layer = layers[safeLayerIndex];
    if (!layer?.sampleFileName || layer.sampleMissing) {
      setPreviewPlayback(prev => ({ ...prev, isPreviewPlaying: false }));
      return;
    }

    const viewPad = {
      ...pad,
      ...layer,
      selectedLayerIndex: safeLayerIndex,
    };
    const trim = trimFromPad(viewPad);
    const normalized = trimToNormalized(trim);

    sendToJuce('auditionLayer', {
      index: selectedIndex,
      layerIndex: safeLayerIndex,
      velocity: 1.0,
    });

    setPreviewPlayback(prev => ({
      isPreviewPlaying: true,
      padIndex: selectedIndex,
      previewStartedAt: performance.now(),
      previewDurationMs: Math.max(30, trim.endMs - trim.startMs),
      previewStartPercent: normalized.startPosition,
      previewEndPercent: normalized.endPosition,
      reverseEnabled: Boolean(layer.reverse),
      triggerId: prev.triggerId + 1,
    }));
  }, [selectedIndex]);

  // ── Master output knob ───────────────────────────────────────────────
  const handleMasterKnobChange = useCallback((v: number) => {
    setMasterKnob(v);
    setKitDirty(true);
    // フェーダー位置 [0..1] をそのまま C++ へ送る (C++ 側で線形ゲインに変換して適用)
    sendToJuce('setMasterVolume', { value: v });
    // Clip is detected from actual audio level (levelData event), not knob position
  }, []);

  const resetMasterClip = useCallback(() => {
    masterClipHitRef.current = false;
    setMasterClipHit(false);
  }, []);

  // Stable callback for PadGrid → PadCell context menu so memo() can skip.
  const openContextMenu = useCallback(
    (index: number, x: number, y: number) => setContextMenu({ index, x, y }),
    [],
  );
  const handleRelinkSelected = useCallback(
    () => requestSampleFile(selectedIndex, true),
    // selectedIndex is read fresh from state on each call below — we capture
    // it via a closure that's recreated when selectedIndex changes, which is
    // fine (selection happens rarely compared to meter ticks).
    // eslint-disable-next-line react-hooks/exhaustive-deps
    [selectedIndex],
  );

  // ── Page change ──────────────────────────────────────────────────────
  const handlePageChange = useCallback((p: KitPage) => {
    setPage(p);
    const pageNum = p === 'A' ? 0 : p === 'B' ? 1 : 2;
    sendToJuce('setPage', { page: pageNum });
  }, []);

  // ── Tab change ───────────────────────────────────────────────────────
  const handleTabChange = useCallback((t: TabId) => {
    if (t === 'MISSING' && missingCount === 0) return;
    setActiveTab(t);
    sendToJuce('setTab', { tab: t });
  }, [missingCount]);

  const handleUiScaleChange = useCallback((scale: UiScale) => {
    setUiScale(scale);
    sendToJuce('setUiScale', { scale });
  }, []);

  // ── Relink All (MissingSamplesView から) ──────────────────────────────
  const handleRelinkAll = useCallback(
    (updates: Array<{ index: number; fileName: string; filePath: string }>) => {
      setPads(prev => {
        const next = [...prev];
        for (const { index, fileName, filePath } of updates) {
          next[index] = {
            ...next[index],
            sampleFileName: fileName,
            sampleFilePath: filePath,
            sampleMissing: false,
          };
        }
        return next;
      });
      setKitDirty(true);
    },
    [],
  );

  const handleRelinkOneMissing = useCallback((index: number) => {
    if (isJuceAvailable()) {
      sendToJuce('relinkSampleDialog', { index });
      return;
    }
    setFileTarget({ index, relink: true });
    fileInputRef.current?.click();
  }, []);

  const handleRelinkOneMissingFile = useCallback((index: number, fileName: string, filePath: string) => {
    setPads(prev =>
      prev.map((p, i) =>
        i === index
          ? { ...p, sampleFileName: fileName, sampleFilePath: filePath, sampleMissing: false }
          : p,
      ),
    );
    setKitDirty(true);
  }, []);

  const loadSampleForPad = useCallback(async (index: number, file: File, relink: boolean, layerIndex = 0) => {
    const analysis = await analyzeBrowserAudioFile(file);

    setPads(prev => {
      const next = prev.map((p, i) => {
        if (i !== index) return p;
        if (relink) {
          return {
            ...p,
            sampleFileName: file.name,
            sampleFilePath: file.name,
            originalSampleFilePath: file.name,
            sampleMissing: false,
            ...analysis,
          };
        }

        const sampleLengthMs = analysis.sampleLengthMs ?? p.sampleLengthMs ?? 500;

        if (layerIndex > 0) {
          const layers = ensureLayers(p).map(layer => ({ ...layer }));
          const target = layers[layerIndex];
          if (target) {
            layers[layerIndex] = {
              ...target,
              sampleFileName: file.name,
              sampleFilePath: file.name,
              sampleMissing: false,
              sampleLengthMs,
              startMs: 0,
              endMs: sampleLengthMs,
              fadeInMs: 0,
              fadeOutMs: 0,
              waveformPeaks: analysis.waveformPeaks ?? [],
              waveformChannels: analysis.waveformChannels ?? [],
            };
            return { ...p, layers, selectedLayerIndex: layerIndex };
          }
        }

        const reset = resettablePadSettings(index);

        return {
          ...p,
          ...reset,
          volume: p.volume,
          pan: p.pan,
          outputAssign: p.outputAssign,
          playMode: 'OneShot' as const,
          sampleFileName: file.name,
          sampleFilePath: file.name,
          originalSampleFilePath: file.name,
          sampleMissing: false,
          sampleLengthMs,
          startMs: 0,
          endMs: sampleLengthMs,
          fadeInMs: 0,
          fadeOutMs: 0,
          waveformPeaks: analysis.waveformPeaks ?? [],
          waveformChannels: analysis.waveformChannels ?? [],
        };
      });
      if (!relink) return next;

      return next.map((p) => {
        if (!p.sampleMissing || p.sampleFileName !== file.name) return p;
        return {
          ...p,
          sampleFilePath: file.name,
          originalSampleFilePath: file.name,
          sampleMissing: false,
        };
      });
    });
    setKitDirty(true);
  }, []);

  const handleSampleDrop = useCallback(async (index: number, file: File) => {
    // Layer-aware drop routing:
    //   - drop on currently-selected pad → active layer (= MAIN or L2+ being edited)
    //   - drop on another pad → that pad's MAIN (layerIndex 0)
    // 投入先がユーザーの編集文脈に一致するように分岐させる。
    const isSelected = index === selectedIndex;
    const layerIndex = isSelected
      ? Math.max(0, pads[index]?.selectedLayerIndex ?? 0)
      : 0;

    console.info('[ASTER DND] target pad index', index, 'layerIndex', layerIndex);
    console.info('[ASTER DND] bridge call start', { index, layerIndex, fileName: file.name });

    if (isJuceAvailable()) {
      const filePath = getDroppedFilePath(file);
      if (filePath) {
        sendToJuce('loadSampleFromPath', { index, layerIndex, filePath, fileName: file.name });
        console.info('[ASTER DND] bridge call success', { index, layerIndex, filePath });
        return;
      }

      try {
        console.info('[ASTER DND] dropped File has no readable path; sending bytes fallback', {
          index,
          layerIndex,
          fileName: file.name,
          type: file.type,
          size: file.size,
        });
        await sendSampleBytesToJuce(index, file, layerIndex);
        console.info('[ASTER DND] bridge call success', { index, layerIndex, fileName: file.name, mode: 'bytes-fallback' });
      } catch (error) {
        console.error('[ASTER DND] bridge call error: bytes fallback failed', error);
      }
      return;
    }

    try {
      await loadSampleForPad(index, file, false, layerIndex);
      console.info('[ASTER DND] bridge call success', { index, fileName: file.name, mode: 'browser-local' });
    } catch (error) {
      console.error('[ASTER DND] bridge call error', error);
    }
  }, [loadSampleForPad, selectedIndex, pads]);

  const handleWaveformSampleDrop = useCallback(
    (file: File) => {
      void handleSampleDrop(selectedIndex, file);
    },
    [handleSampleDrop, selectedIndex],
  );

  const handleAddSelectedLayer = useCallback(() => {
    const pad = padsRef.current[selectedIndex];
    if (!pad) return;
    const patch = patchAddLayer(pad);
    if (!patch) return;

    if (isJuceAvailable()) {
      const copyFromIndex = Math.max(0, pad.selectedLayerIndex ?? 0);
      sendToJuce('addLayer', { index: selectedIndex, copyFromIndex, clearSample: true });
      if (patch.selectedLayerIndex !== undefined) {
        sendToJuce('selectLayer', { index: selectedIndex, layerIndex: patch.selectedLayerIndex });
      }
    }

    setPads(prev => prev.map((p, i) => (i === selectedIndex ? { ...p, ...patch } : p)));
    setKitDirty(true);
  }, [selectedIndex]);

  const handleRemoveSelectedLayer = useCallback((layerIndex: number) => {
    const pad = padsRef.current[selectedIndex];
    if (!pad) return;
    const patch = patchRemoveLayer(pad, layerIndex);
    if (!patch) return;

    if (isJuceAvailable()) {
      sendToJuce('removeLayer', { index: selectedIndex, layerIndex });
      if (patch.selectedLayerIndex !== undefined) {
        sendToJuce('selectLayer', { index: selectedIndex, layerIndex: patch.selectedLayerIndex });
      }
    }

    setPads(prev => prev.map((p, i) => (i === selectedIndex ? { ...p, ...patch } : p)));
    setKitDirty(true);
  }, [selectedIndex]);

  const handleReanalyzeSelected = useCallback(() => {
    const pad = padsRef.current[selectedIndex];
    if (!pad) return;
    const layerIndex = Math.max(0, pad.selectedLayerIndex ?? 0);
    if (isJuceAvailable()) {
      sendToJuce('reanalyzeLayer', { index: selectedIndex, layerIndex });
      return;
    }
    updatePad(selectedIndex, { smartTrim: true });
  }, [selectedIndex, updatePad]);

  const handlePadSwap = useCallback((sourceIndex: number, targetIndex: number) => {
    console.info('[ASTER PAD SWAP] target pad index', targetIndex);
    console.info('[ASTER PAD SWAP] bridge call start', { sourceIndex, targetIndex });

    if (
      sourceIndex === targetIndex ||
      sourceIndex < 0 ||
      sourceIndex >= 48 ||
      targetIndex < 0 ||
      targetIndex >= 48
    ) {
      console.error('[ASTER PAD SWAP] bridge call error: invalid pad index', { sourceIndex, targetIndex });
      return;
    }

    setPads(prev => swapPadSounds(prev, sourceIndex, targetIndex));
    setKitDirty(true);

    if (isJuceAvailable()) {
      sendToJuce('swapPadSounds', { sourceIndex, targetIndex });
      console.info('[ASTER PAD SWAP] bridge call success', { sourceIndex, targetIndex });
    } else {
      console.info('[ASTER PAD SWAP] bridge call success', { sourceIndex, targetIndex, mode: 'browser-local' });
    }
  }, []);

  const requestSampleFile = useCallback((index: number, relink = false, layerIndex?: number) => {
    // In plugin mode: delegate to C++ file dialog (with active layer)
    if (isJuceAvailable()) {
      const li = layerIndex !== undefined
        ? layerIndex
        : (index === selectedIndex ? Math.max(0, padsRef.current[index]?.selectedLayerIndex ?? 0) : 0);
      sendToJuce('loadSampleDialog', { index, layerIndex: li });
      return;
    }
    // In browser: use HTML file picker
    setFileTarget({ index, relink });
    fileInputRef.current?.click();
  }, [selectedIndex]);

  const handleFilePicked = useCallback(async (file: File | undefined) => {
    if (!file || !fileTarget) return;
    await loadSampleForPad(fileTarget.index, file, fileTarget.relink);
    setFileTarget(null);
    if (fileInputRef.current) fileInputRef.current.value = '';
  }, [fileTarget, loadSampleForPad]);

  const resetPadSettings = useCallback((index: number) => {
    const patch = resettablePadSettings(index);
    sendPadPatchToJuce(index, patch, padsRef.current[index]);
    updatePad(index, patch);
  }, [updatePad]);

  // ── Layer / Pad 一括操作 (v7+) ─────────────────────────────────────
  // Layer 単位の "音作り" デフォルト値 (サンプル参照は含めない)
  const defaultLayerAudioParams = useCallback((sampleLengthMs?: number): Partial<LayerParams> => ({
    volume: 0.75,
    pan: 0,
    pitch: 0,
    fine: 0,
    attack: 0,
    release: 0.05,
    startMs: 0,
    endMs: sampleLengthMs ?? FALLBACK_SAMPLE_LENGTH_MS,
    fadeInMs: 0,
    fadeOutMs: 0,
    reverse: false,
    keepLength: true,
    mute: false,
    solo: false,
    velocityMin: 0,
    velocityMax: 127,
    polarityInvert: false,
    eq: NEUTRAL_EQ,
    fxChain: undefined,
  }), []);

  /** Reset Layer N Settings — サンプルは残し、Layer の音作りパラメータだけ初期化 */
  const resetLayerSettings = useCallback((padIndex: number, layerIndex: number) => {
    const pad = padsRef.current[padIndex];
    if (!pad) return;
    const layers = pad.layers ?? [];
    const target = layers[layerIndex];
    if (!target) return;
    const defaults = defaultLayerAudioParams(target.sampleLengthMs);
    const newLayer: LayerParams = { ...target, ...defaults };
    const newLayers = layers.map((l, i) => (i === layerIndex ? newLayer : l));
    const patch: Partial<PadParams> = { layers: newLayers };
    // Layer 0 を変更したら flat fields にもミラー (既存 routing と整合)
    if (layerIndex === 0) Object.assign(patch, defaults);
    sendPadPatchToJuce(padIndex, patch, pad);
    updatePad(padIndex, patch);
  }, [defaultLayerAudioParams, updatePad]);

  /** Clear Layer N — サンプル + Layer パラメータをまとめてクリア */
  const clearLayer = useCallback((padIndex: number, layerIndex: number) => {
    const pad = padsRef.current[padIndex];
    if (!pad) return;
    const layers = pad.layers ?? [];
    if (layerIndex >= layers.length) return;

    // C++ 側: 該当 Layer のバッファを破棄
    if (isJuceAvailable()) {
      sendToJuce('clearPadSample', { index: padIndex, layerIndex });
    }

    const defaults = defaultLayerAudioParams();
    const newLayer: LayerParams = {
      ...defaults,
      sampleFileName: '',
      sampleFilePath: '',
      sampleMissing: false,
      layerName: undefined,
      sampleLengthMs: undefined,
      waveformPeaks: undefined,
      waveformChannels: undefined,
    } as LayerParams;
    const newLayers = layers.map((l, i) => (i === layerIndex ? newLayer : l));
    const patch: Partial<PadParams> = { layers: newLayers };
    if (layerIndex === 0) {
      Object.assign(patch, defaults, {
        sampleFileName: '',
        sampleFilePath: '',
        originalSampleFilePath: '',
        sampleMissing: false,
        sampleLengthMs: undefined,
        waveformPeaks: undefined,
        waveformChannels: undefined,
      });
    }
    updatePad(padIndex, patch);
  }, [defaultLayerAudioParams, updatePad]);

  /** Clear Pad — 全 Layer + Pad 音作りパラメータをクリア。
   *  維持: midiNote / outputAssign / chokeGroup (Pad 位置に紐づく設定) */
  const clearPad = useCallback((padIndex: number) => {
    const pad = padsRef.current[padIndex];
    if (!pad) return;
    const initialPad = INITIAL_PADS[padIndex];

    // C++ 側: 全 Layer のバッファ破棄 (後ろから消して index 安定)
    if (isJuceAvailable()) {
      const layers = pad.layers ?? [];
      for (let i = Math.max(0, layers.length - 1); i >= 0; i--) {
        sendToJuce('clearPadSample', { index: padIndex, layerIndex: i });
      }
      if (layers.length === 0) {
        sendToJuce('clearPadSample', { index: padIndex, layerIndex: 0 });
      }
    }

    const defaults = defaultLayerAudioParams();
    const emptyLayer: LayerParams = {
      ...defaults,
      sampleFileName: '',
      sampleFilePath: '',
      sampleMissing: false,
      layerName: undefined,
      sampleLengthMs: undefined,
      waveformPeaks: undefined,
      waveformChannels: undefined,
    } as LayerParams;

    const patch: Partial<PadParams> = {
      ...resettablePadSettings(padIndex),     // 再生系 (pan/pitch/attack/release/trim 等)
      sampleFileName: '',
      sampleFilePath: '',
      originalSampleFilePath: '',
      sampleMissing: false,
      sampleLengthMs: undefined,
      waveformPeaks: undefined,
      waveformChannels: undefined,
      padName: initialPad.padName,            // カテゴリ名に戻す
      padColor: undefined,                     // Auto color
      // Pad-level 音作り系
      velocitySens: 1.0,
      humanize: 0,
      velCurve: undefined,                     // Linear default
      // Layer は 1 個の空状態に縮約
      layers: [emptyLayer],
      selectedLayerIndex: 0,
      // 維持: midiNote / outputAssign / chokeGroup は patch に含めない
    };
    updatePad(padIndex, patch);
  }, [defaultLayerAudioParams, updatePad]);

  const renamePad = useCallback((index: number) => {
    const pad = padsRef.current[index];
    if (!pad) return;
    setRenameDialog({ index, draft: pad.padName });
  }, []);

  const openMidiNoteDialog = useCallback((index: number) => {
    const pad = padsRef.current[index];
    if (!pad) return;
    setMidiNoteDialog({ index, draftNote: pad.midiNote, learning: false, warning: '' });
  }, []);

  const cancelMidiNoteDialog = useCallback(() => {
    if (midiNoteDialog?.learning) sendToJuce('cancelMidiLearn', {});
    setMidiNoteDialog(null);
  }, [midiNoteDialog]);

  const beginMidiLearn = useCallback(() => {
    if (!midiNoteDialog) return;
    sendToJuce('beginMidiLearn', { index: midiNoteDialog.index });
    setMidiNoteDialog(prev => prev
      ? { ...prev, learning: true, warning: 'Waiting for the next MIDI note...' }
      : prev,
    );
  }, [midiNoteDialog]);

  const resetMidiNoteDraft = useCallback(() => {
    setMidiNoteDialog(prev => prev
      ? {
          ...prev,
          draftNote: defaultMidiNoteForPad(prev.index),
          warning: '',
        }
      : prev,
    );
  }, []);

  const applyMidiNoteDialog = useCallback(() => {
    if (!midiNoteDialog) return;

    if (midiNoteDialog.learning) sendToJuce('cancelMidiLearn', {});
    const { index, draftNote } = midiNoteDialog;
    updatePad(index, { midiNote: draftNote });
    setMidiNoteDialog(null);
  }, [midiNoteDialog, updatePad]);

  useEffect(() => {
    if (!toastMessage) return;
    const timer = window.setTimeout(() => setToastMessage(''), 1800);
    return () => window.clearTimeout(timer);
  }, [toastMessage]);

  const commitRenameDialog = useCallback(() => {
    if (!renameDialog) return;
    const trimmed = renameDialog.draft.trim();
    if (trimmed) updatePad(renameDialog.index, { padName: trimmed });
    setRenameDialog(null);
  }, [renameDialog, updatePad]);

  const cancelRenameDialog = useCallback(() => {
    setRenameDialog(null);
  }, []);

  useEffect(() => {
    if (!midiNoteDialog) return;
    const onKeyDown = (event: KeyboardEvent) => {
      if (event.key === 'Escape') cancelMidiNoteDialog();
      if (event.key === 'Enter') applyMidiNoteDialog();
    };
    window.addEventListener('keydown', onKeyDown);
    return () => window.removeEventListener('keydown', onKeyDown);
  }, [applyMidiNoteDialog, cancelMidiNoteDialog, midiNoteDialog]);

  const saveKit = useCallback(() => {
    sendToJuce('saveKit', {});
    if (isJuceAvailable()) return;

    let nextKitName = kitName;
    if (kitName.trim().toLowerCase() === 'default') {
      const nextName = window.prompt('Save Kit As', 'My Kit')?.trim();
      if (!nextName || nextName.toLowerCase() === 'default') return;
      nextKitName = nextName;
    } else {
      setKitDirty(false);
    }

    const state: SavedKitState = {
      version: KIT_STORAGE_VERSION,
      kitName: nextKitName, outputMode, selectedIndex, page, pads,
    };
    localStorage.setItem(KIT_STORAGE_KEY, JSON.stringify(state));
    setKitName(nextKitName);
    setCurrentKitPath(nextKitName);
    setKitItems([
      { name: 'Empty Kit', path: '', isDefault: true },
      { name: nextKitName, path: nextKitName, isDefault: false },
    ]);
    setKitDirty(false);
  }, [kitName, outputMode, selectedIndex, page, pads]);

  const saveKitAs = useCallback(() => {
    sendToJuce('saveKitAs', {});
    if (isJuceAvailable()) return;

    const nextName = window.prompt('Save Kit As', kitName === 'Default' ? 'My Kit' : kitName)?.trim();
    if (!nextName || nextName.toLowerCase() === 'default') return;

    const state: SavedKitState = {
      version: KIT_STORAGE_VERSION,
      kitName: nextName, outputMode, selectedIndex, page, pads,
    };
    localStorage.setItem(KIT_STORAGE_KEY, JSON.stringify(state));
    setKitName(nextName);
    setCurrentKitPath(nextName);
    setKitItems([
      { name: 'Empty Kit', path: '', isDefault: true },
      { name: nextName, path: nextName, isDefault: false },
    ]);
    setKitDirty(false);
  }, [kitName, outputMode, selectedIndex, page, pads]);

  // Kit selection flow (Kit dropdown is the only entry point):
  //   1) requestKitSwitch(path)
  //   2) if kitDirty  → pendingKitSwitch dialog (Save/Don't Save/Cancel)
  //   3a) path === '' (Empty Kit) → full reset immediately, no options dialog
  //   3b) User Kit → loadKitTarget dialog (category checkboxes → Load/Cancel)
  //   4) sendToJuce('loadKitByPath', { path, ...options })
  // C++ treats "all categories on" as a full load (currentKit = target,
  // dirty=false). Partial load keeps currentKit identity, sets dirty=true.
  const resetToEmptyKit = useCallback(() => {
    if (isJuceAvailable()) {
      sendToJuce('loadKitByPath', { path: '' });
      return;
    }
    setPads(INITIAL_PADS);
    setKitName('Default');
    setCurrentKitPath('');
    setKitDirty(false);
  }, []);

  const openLoadOptionsForPath = useCallback((path: string) => {
    if (path === '') {
      resetToEmptyKit();
      return;
    }
    setLoadKitTarget({ path });
  }, [resetToEmptyKit]);

  const requestKitSwitch = useCallback((path: string) => {
    if (path === currentKitPath && !kitDirty) {
      // Same kit, no pending edits — reloading would be a no-op for the user.
      return;
    }
    if (kitDirty) {
      setPendingKitSwitch({ path });
      return;
    }
    openLoadOptionsForPath(path);
  }, [currentKitPath, kitDirty, openLoadOptionsForPath]);

  const confirmPendingKitSwitch = useCallback((action: 'save' | 'discard' | 'cancel') => {
    if (!pendingKitSwitch) return;
    const { path } = pendingKitSwitch;

    if (action === 'cancel') {
      setPendingKitSwitch(null);
      return;
    }

    if (action === 'save') {
      // saveKit() handles JUCE/local + the prompt-for-name case for Default.
      saveKit();
    }

    setPendingKitSwitch(null);
    openLoadOptionsForPath(path);
  }, [pendingKitSwitch, saveKit, openLoadOptionsForPath]);

  const confirmLoadKit = useCallback(() => {
    if (!loadKitTarget) return;
    const { path } = loadKitTarget;
    setLoadKitTarget(null);

    if (isJuceAvailable()) {
      sendToJuce('loadKitByPath', { path, ...loadKitOptions });
      return;
    }

    // Browser-only fallback (no JUCE bridge): only the full-load path is
    // meaningfully supported via localStorage. Partial merges aren't
    // exercised in the prototype harness.
    if (path === '') {
      setPads(INITIAL_PADS);
      setKitName('Default');
      setCurrentKitPath('');
      setKitDirty(false);
      return;
    }

    const raw = localStorage.getItem(KIT_STORAGE_KEY);
    if (!raw) return;
    try {
      const state = JSON.parse(raw) as Partial<SavedKitState>;
      if (state.kitName !== path || !Array.isArray(state.pads) || state.pads.length !== 48) return;

      const needsVolumeMigration = (state.version ?? 1) < 2;

      setPads(state.pads.map(p => {
        const migrated = {
          ...p,
          volume: needsVolumeMigration ? gainToPosition(p.volume) : p.volume,
          sampleLengthMs: p.sampleLengthMs ?? Math.max(1, p.endMs),
          sampleMissing: Boolean(p.sampleFilePath && p.sampleMissing),
          originalSampleFilePath: p.originalSampleFilePath ?? p.sampleFilePath,
        };
        return {
          ...migrated,
          ...normalizePadTrimPatch(migrated, {
            sampleLengthMs: migrated.sampleLengthMs,
            startMs: migrated.startMs,
            endMs: migrated.endMs,
            fadeInMs: migrated.fadeInMs,
            fadeOutMs: migrated.fadeOutMs,
          }, 'all'),
        };
      }));
      setKitName(state.kitName ?? 'Default');
      setCurrentKitPath(path);
      setKitDirty(false);
      setOutputMode(state.outputMode ?? '48Outs');

      const nextIndex = Math.max(0, Math.min(47, state.selectedIndex ?? 0));
      setSelectedIndex(nextIndex);
      setPage(state.page ?? pageOfIndex(nextIndex));
    } catch {
      localStorage.removeItem(KIT_STORAGE_KEY);
    }
  }, [loadKitOptions, loadKitTarget]);

  const cancelLoadKit = useCallback(() => {
    setLoadKitTarget(null);
  }, []);

  const handleOutputModeChange = useCallback((mode: OutputMode) => {
    setOutputMode(mode);
    setKitDirty(true);
    sendToJuce('setOutputMode', { value: mode });
  }, []);

  return (
    <div className={styles.app}>
      <div
        className={styles.scaleSurface}
        style={{ '--fit-scale': viewportScale } as CSSProperties}
      >
        <Header
          isDemo={demoState.isDemo}
          kitName={`${
            // Derive the header label from the same data the dropdown uses
            // (kitItems + currentKitPath) so the checked entry and the label
            // can never disagree. Fall back to "Empty Kit" if the active
            // path isn't in the list (should not happen — C++ guarantees the
            // current kit is always present — but keeps the UI consistent).
            (kitItems.find(item =>
              item.isDefault ? currentKitPath === '' : item.path === currentKitPath,
            )?.name) ?? 'Empty Kit'
          }${kitDirty ? ' *' : ''}`}
          kitItems={kitItems}
          currentKitPath={currentKitPath}
          uiScale={uiScale}
          onSelectKit={requestKitSwitch}
          onRescanKitFolder={() => sendToJuce('rescanKitFolder', {})}
          onRevealKitFolder={() => sendToJuce('revealKitFolder', {})}
          onSaveKit={saveKit}
          onSaveKitAs={saveKitAs}
          onUiScaleChange={handleUiScaleChange}
          onUndo={undo}
          onRedo={redo}
          canUndo={canUndo}
          canRedo={canRedo}
        />

        <TabBar
          active={activeTab}
          onChange={handleTabChange}
          missingCount={missingCount}
        />

        <main className={`${styles.body} ${
          activeTab === 'MIXER'   ? styles.bodyMixer   :
          activeTab === 'MISSING' && missingCount > 0 ? styles.bodyMissing :
                                    styles.bodyPads
        }`}>
          {activeTab === 'PADS' && (
            <section className={styles.pads}>
              <PadsView
                pads={pads}
                page={page}
                selectedIndex={selectedIndex}
                selectedPad={selected}
                onSelect={selectPad}
                onPageChange={handlePageChange}
                onPadContextMenu={openContextMenu}
                onChangePad={updatePad}
                onSampleDrop={handleSampleDrop}
                onWaveformSampleDrop={handleWaveformSampleDrop}
                onReanalyzeSelected={handleReanalyzeSelected}
                onPadSwap={handlePadSwap}
                onChangeSelected={updateSelected}
                onAddLayer={handleAddSelectedLayer}
                onRemoveLayer={handleRemoveSelectedLayer}
                onRelinkSelected={handleRelinkSelected}
                previewPlayback={previewPlayback}
                onPreviewFinished={handlePreviewFinished}
                onWaveformAudition={auditionSelectedLayerFromWaveform}
                liveVelocity={liveVelocities[selectedIndex] ?? null}
                masterKnob={masterKnob}
                onMasterKnobChange={handleMasterKnobChange}
                masterClipHit={masterClipHit}
                onResetMasterClip={resetMasterClip}
              />
            </section>
          )}

          {activeTab === 'MIXER' && (
            <section className={styles.mixer}>
              <MixerView
                pads={pads}
                page={page}
                onPageChange={handlePageChange}
                outputMode={outputMode}
                onOutputModeChange={handleOutputModeChange}
                selectedIndex={selectedIndex}
                onSelect={selectPadWithoutAudition}
                onAudition={selectPad}
                onChangePad={updatePad}
                onChangePads={updatePads}
                masterKnob={masterKnob}
                onMasterKnobChange={handleMasterKnobChange}
                masterClipHit={masterClipHit}
                onResetMasterClip={resetMasterClip}
              />
            </section>
          )}

          {activeTab === 'MISSING' && missingCount > 0 && (
            <section className={styles.missing}>
              <MissingSamplesView
                pads={pads}
                useNativeRelink={isJuceAvailable()}
                onRelinkPad={handleRelinkOneMissing}
                onRelinkPadFile={handleRelinkOneMissingFile}
                onRelinkAll={handleRelinkAll}
              />
            </section>
          )}
        </main>
      </div>

      <div
        className={`${styles.resizeGrip} ${isWindowResizing ? styles.resizeGripActive : ''}`}
        role="separator"
        aria-label="Resize plugin window"
        aria-orientation="horizontal"
        tabIndex={0}
        title="Drag to resize"
        onPointerDown={handleResizePointerDown}
        onPointerMove={handleResizePointerMove}
        onPointerUp={finishResizeDrag}
        onPointerCancel={finishResizeDrag}
        onKeyDown={handleResizeKeyDown}
      >
        <svg viewBox="0 0 24 24" aria-hidden="true">
          <path d="M4 21L21 4" />
          <path d="M10 21L21 10" />
          <path d="M16 21L21 16" />
        </svg>
      </div>

      {demoState.isDemo && (demoState.expired || demoState.offlineRenderBlocked) && (
        <div className={styles.demoNotice} role="status" aria-live="polite">
          {demoState.offlineRenderBlocked
            ? 'Demo: offline export is available in the Full version.'
            : 'Demo audio time has ended. Restart your DAW to continue testing.'}
        </div>
      )}

      {/* HTML file picker — only used in browser dev mode */}
      <input
        ref={fileInputRef}
        type="file"
        accept="audio/*,.wav,.aif,.aiff,.mp3,.flac"
        hidden
        onChange={(event) => handleFilePicked(event.currentTarget.files?.[0])}
      />

      {contextMenu && (() => {
        // 右クリックされた Pad に対する「現在の編集対象 Layer」を決める:
        //   - その Pad が現在選択中 → 選択中 Layer (active layer)
        //   - 別 Pad → 0 (MAIN) として扱う
        const ctxPad = pads[contextMenu.index];
        const ctxIsSelected = contextMenu.index === selectedIndex;
        const ctxLayerIndex = ctxIsSelected
          ? Math.max(0, ctxPad?.selectedLayerIndex ?? 0)
          : 0;
        const ctxLayerCount = ctxPad?.layers?.length ?? 1;
        return (
        <PadContextMenu
          x={contextMenu.x}
          y={contextMenu.y}
          pad={ctxPad}
          activeLayerIndex={ctxLayerIndex}
          layerCount={ctxLayerCount}
          maxLayers={8}
          canPaste={Boolean(padClipboard)}
          onClose={() => setContextMenu(null)}
          onLoadSample={() => requestSampleFile(contextMenu.index, false, ctxLayerIndex)}
          onReplaceSample={() => requestSampleFile(contextMenu.index, false, ctxLayerIndex)}
          onClearSample={() => {
            if (isJuceAvailable()) {
              sendToJuce('clearPadSample', { index: contextMenu.index, layerIndex: ctxLayerIndex });
              return;
            }
            // browser dev mode: 簡易対応 — flat fields のみクリア
            updatePad(contextMenu.index, {
              sampleFileName: '',
              sampleFilePath: '',
              originalSampleFilePath: '',
              sampleMissing: false,
              waveformPeaks: [],
              waveformChannels: [],
            });
          }}
          onAddLayer={() => {
            // 同じ Pad に新規 Layer を追加 (Sample 参照は MAIN を継承)
            if (ctxLayerCount >= 8) return;
            sendToJuce('addLayer', { index: contextMenu.index, copyFromIndex: ctxLayerIndex });
            // UI 側 state も足す (C++ から broadcast されるが、楽観更新で即反映)
            setPads(prev => prev.map((p, i) => {
              if (i !== contextMenu.index) return p;
              const layers = p.layers ?? [];
              const src = layers[ctxLayerIndex] ?? layers[0];
              if (!src) return p;
              const nextLayer = { ...src, layerName: undefined, mute: false, solo: false };
              return { ...p, layers: [...layers, nextLayer], selectedLayerIndex: layers.length };
            }));
          }}
          onDuplicateLayer={() => {
            // 現在 Layer をそのままコピーした新 Layer を末尾に追加
            if (ctxLayerCount >= 8) return;
            sendToJuce('addLayer', { index: contextMenu.index, copyFromIndex: ctxLayerIndex });
            setPads(prev => prev.map((p, i) => {
              if (i !== contextMenu.index) return p;
              const layers = p.layers ?? [];
              const src = layers[ctxLayerIndex] ?? layers[0];
              if (!src) return p;
              return { ...p, layers: [...layers, { ...src }], selectedLayerIndex: layers.length };
            }));
          }}
          onCopyPad={() => {
            setPadClipboard(pads[contextMenu.index]);
            sendToJuce('copyPad', { index: contextMenu.index });
          }}
          onPastePad={() => {
            if (!padClipboard) return;
            if (isJuceAvailable()) {
              sendToJuce('pastePad', { index: contextMenu.index });
              return;
            }
            setPads(prev => prev.map((p, i) => (
              i === contextMenu.index
                ? {
                    ...padClipboard,
                    midiNote: p.midiNote,
                    outputAssign: p.outputAssign,
                  }
                : p
            )));
            setKitDirty(true);
          }}
          onResetSettings={() => resetPadSettings(contextMenu.index)}
          onResetLayerSettings={() => resetLayerSettings(contextMenu.index, ctxLayerIndex)}
          onClearLayer={() => clearLayer(contextMenu.index, ctxLayerIndex)}
          onClearPad={() => clearPad(contextMenu.index)}
          canClearPad={(() => {
            const p = ctxPad;
            const padHasSample = Boolean(p.sampleFileName || p.sampleFilePath);
            const layerHasSample = (p.layers ?? []).some(l =>
              Boolean(l.sampleFileName || l.sampleFilePath));
            return padHasSample || layerHasSample;
          })()}
          onRenamePad={() => renamePad(contextMenu.index)}
          onSetMidiNote={() => openMidiNoteDialog(contextMenu.index)}
          onOpenCustomColorPicker={() => {
            const p = pads[contextMenu.index];
            setColorPickerTarget({
              index: contextMenu.index,
              initialColor: p.padColor ?? p.categoryColor,
              startedFromAuto: !p.padColor,
            });
          }}
          onRevealSample={() => {
            if (isJuceAvailable()) {
              sendToJuce('revealSample', { index: contextMenu.index, layerIndex: ctxLayerIndex });
              return;
            }
            const layerPath = ctxLayerIndex > 0
              ? ctxPad?.layers?.[ctxLayerIndex]?.sampleFilePath
              : ctxPad?.sampleFilePath;
            window.alert(layerPath || 'No sample path');
          }}
        />
        );
      })()}

      {colorPickerTarget && (
        <PadColorPicker
          initialColor={colorPickerTarget.initialColor}
          startedFromAuto={colorPickerTarget.startedFromAuto}
          onApply={(color) => {
            updatePad(colorPickerTarget.index, { padColor: color });
            setColorPickerTarget(null);
          }}
          onResetToAuto={() => {
            updatePad(colorPickerTarget.index, { padColor: undefined });
            setColorPickerTarget(null);
          }}
          onCancel={() => setColorPickerTarget(null)}
        />
      )}

      {midiNoteDialog && (() => {
        const parts = midiNoteParts(midiNoteDialog.draftNote);
        const octaveOptions = Array.from({ length: 11 }, (_, i) => i - 2);
        const currentPad = pads[midiNoteDialog.index];
        const noteUsageLabel = (note: number) => {
          const usedIndex = pads.findIndex((pad, padIndex) =>
            padIndex !== midiNoteDialog.index && pad.midiNote === note,
          );
          if (usedIndex < 0) return '';
          return `  Pad ${String(usedIndex + 1).padStart(2, '0')} ${pads[usedIndex].padName}`;
        };
        return (
          <div className={styles.modalBackdrop} role="presentation" onMouseDown={cancelMidiNoteDialog}>
            <div
              className={styles.midiNoteDialog}
              role="dialog"
              aria-modal="true"
              aria-label="Set MIDI Note"
              onMouseDown={(event) => event.stopPropagation()}
            >
              <div className={styles.dialogTitle}>Set MIDI Note</div>
              <div className={styles.currentNote}>
                Current Note: <strong>{midiNoteName(currentPad?.midiNote ?? midiNoteDialog.draftNote)}</strong>
              </div>
              <button
                type="button"
                className={`${styles.learnButton} ${midiNoteDialog.learning ? styles.learnButtonActive : ''}`}
                onClick={beginMidiLearn}
              >
                {midiNoteDialog.learning ? 'Learning...' : 'Learn MIDI Note'}
              </button>
              <div className={styles.notePickerGrid}>
                <label>
                  <span>Octave</span>
                  <select
                    value={parts.octave}
                    onChange={(event) => {
                      const nextOctave = Number(event.currentTarget.value);
                      setMidiNoteDialog(prev => prev
                        ? { ...prev, draftNote: midiNoteFromParts(nextOctave, midiNoteParts(prev.draftNote).noteIndex), warning: '' }
                        : prev,
                      );
                    }}
                  >
                    {octaveOptions.map(octave => (
                      <option key={octave} value={octave}>{octave}</option>
                    ))}
                  </select>
                </label>
                <label>
                  <span>Note</span>
                  <select
                    value={parts.noteIndex}
                    onChange={(event) => {
                      const nextNoteIndex = Number(event.currentTarget.value);
                      setMidiNoteDialog(prev => prev
                        ? { ...prev, draftNote: midiNoteFromParts(midiNoteParts(prev.draftNote).octave, nextNoteIndex), warning: '' }
                        : prev,
                      );
                    }}
                  >
                    {MIDI_NOTE_NAMES.map((noteName, noteIndex) => (
                      <option key={noteName} value={noteIndex} disabled={parts.octave === 8 && noteIndex > 7}>
                        {noteName}{noteUsageLabel(midiNoteFromParts(parts.octave, noteIndex))}
                      </option>
                    ))}
                  </select>
                </label>
              </div>
              <button type="button" className={styles.resetNoteButton} onClick={resetMidiNoteDraft}>
                Reset to Default Note
              </button>
              {midiNoteDialog.warning && (
                <div className={midiNoteDialog.learning ? styles.learnStatus : styles.dialogWarning}>
                  {midiNoteDialog.warning}
                </div>
              )}
              <div className={styles.dialogActions}>
                <button type="button" onClick={cancelMidiNoteDialog}>Cancel</button>
                <button type="button" onClick={applyMidiNoteDialog}>Apply</button>
              </div>
            </div>
          </div>
        );
      })()}

      {toastMessage && (
        <div className={styles.toast} role="status">
          {toastMessage}
        </div>
      )}

      {renameDialog && (
        <div className={styles.modalBackdrop} role="presentation" onMouseDown={cancelRenameDialog}>
          <div
            className={styles.renameDialog}
            role="dialog"
            aria-modal="true"
            aria-label="Rename Pad"
            onMouseDown={(event) => event.stopPropagation()}
          >
            <div className={styles.dialogTitle}>Rename Pad</div>
            <input
              className={styles.renameInput}
              value={renameDialog.draft}
              autoFocus
              onChange={(event) => {
                const nextDraft = event.currentTarget.value;
                setRenameDialog(prev => prev ? { ...prev, draft: nextDraft } : prev);
              }}
              onKeyDown={(event) => {
                if (event.key === 'Enter') commitRenameDialog();
                if (event.key === 'Escape') cancelRenameDialog();
              }}
            />
            <div className={styles.dialogActions}>
              <button type="button" onClick={cancelRenameDialog}>Cancel</button>
              <button
                type="button"
                onClick={commitRenameDialog}
                disabled={renameDialog.draft.trim().length === 0}
              >
                Rename
              </button>
            </div>
          </div>
        </div>
      )}

      {pendingKitSwitch && (
        <div className={styles.modalBackdrop} role="presentation" onMouseDown={() => confirmPendingKitSwitch('cancel')}>
          <div className={styles.loadKitDialog} role="dialog" aria-modal="true" aria-label="Unsaved Kit Changes" onMouseDown={(event) => event.stopPropagation()}>
            <div className={styles.dialogTitle}>Unsaved Kit Changes</div>
            <div className={styles.dialogText}>
              {demoState.isDemo
                ? 'Kit saving is unavailable in the Demo. Discard these changes and switch kits?'
                : `Save changes to ${kitName === 'Default' ? 'Empty Kit' : kitName} before switching kits?`}
            </div>
            <div className={styles.dialogActions}>
              <button type="button" onClick={() => confirmPendingKitSwitch('cancel')}>Cancel</button>
              <button type="button" onClick={() => confirmPendingKitSwitch('discard')}>Don&apos;t Save</button>
              {!demoState.isDemo && (
                <button type="button" onClick={() => confirmPendingKitSwitch('save')}>Save</button>
              )}
            </div>
          </div>
        </div>
      )}

      {loadKitTarget && (
        <div className={styles.modalBackdrop} role="presentation" onMouseDown={cancelLoadKit}>
          <div className={styles.loadKitDialog} role="dialog" aria-modal="true" aria-label="Load Kit" onMouseDown={(event) => event.stopPropagation()}>
            <div className={styles.dialogTitle}>Load Kit</div>
            <label className={styles.dialogCheck}>
              <input
                type="checkbox"
                checked={loadKitOptions.samples}
                onChange={(event) => {
                  const checked = event.currentTarget.checked;
                  setLoadKitOptions(prev => ({ ...prev, samples: checked }));
                }}
              />
              <span>Samples</span>
            </label>
            <label className={styles.dialogCheck}>
              <input
                type="checkbox"
                checked={loadKitOptions.padNamesAndColours}
                onChange={(event) => {
                  const checked = event.currentTarget.checked;
                  setLoadKitOptions(prev => ({ ...prev, padNamesAndColours: checked }));
                }}
              />
              <span>Pad names & colors</span>
            </label>
            <label className={styles.dialogCheck}>
              <input
                type="checkbox"
                checked={loadKitOptions.padParameters}
                onChange={(event) => {
                  const checked = event.currentTarget.checked;
                  setLoadKitOptions(prev => ({ ...prev, padParameters: checked }));
                }}
              />
              <span>Pad parameters</span>
            </label>
            <label className={styles.dialogCheck}>
              <input
                type="checkbox"
                checked={loadKitOptions.mixerSettings}
                onChange={(event) => {
                  const checked = event.currentTarget.checked;
                  setLoadKitOptions(prev => ({ ...prev, mixerSettings: checked }));
                }}
              />
              <span>Mixer settings</span>
            </label>
            <label className={styles.dialogCheck}>
              <input
                type="checkbox"
                checked={loadKitOptions.routing}
                onChange={(event) => {
                  const checked = event.currentTarget.checked;
                  setLoadKitOptions(prev => ({ ...prev, routing: checked }));
                }}
              />
              <span>Routing</span>
            </label>
            <div className={styles.dialogActions}>
              <button type="button" onClick={cancelLoadKit}>Cancel</button>
              <button type="button" onClick={confirmLoadKit}>Load Kit</button>
            </div>
          </div>
        </div>
      )}
    </div>
  );
}
