import { memo, useCallback, useEffect, useMemo, useRef, useState } from 'react';
import styles from './PadsView.module.css';
import { PadGrid } from '../PadGrid/PadGrid';
import { WaveformEditor } from '../WaveformEditor/WaveformEditor';
import { PadEditorTabs } from '../PadEditorTabs/PadEditorTabs';
import { LayerTabs } from '../LayerTabs/LayerTabs';
import { Knob } from '../Knob/Knob';
import { SettingsMenu } from '../SettingsMenu/SettingsMenu';
import { AutomationModeButton } from '../AutomationAssign/AutomationAssign';
import type { PadParams, KitPage, PreviewPlayback } from '../../types';
import {
  composePadView,
  patchAddLayer,
  patchLayerMuteSolo,
  patchRemoveLayer,
  routeLayerPatch,
  selectedLayerIndexOf,
} from '../../utils/layerView';
import { MASTER_UNITY } from '../../App';
import { dbToPosition, formatFaderDb } from '../../utils/fader';
import { registerMasterMeter } from '../../utils/meterRegistry';
import { registerResourceMeter } from '../../utils/resourceRegistry';
import { parseNumericText } from '../../utils/numericInput';
import { masterAutomationTarget } from '../../utils/automationTarget';

interface PadsViewProps {
  pads: PadParams[];
  page: KitPage;
  selectedIndex: number;
  selectedPad: PadParams;
  onSelect: (i: number) => void;
  onPageChange: (p: KitPage) => void;
  onPadContextMenu?: (index: number, x: number, y: number) => void;
  onChangePad: (index: number, patch: Partial<PadParams>) => void;
  onSampleDrop: (index: number, file: File) => void;
  onWaveformSampleDrop?: (file: File) => void;
  onReanalyzeSelected?: () => void;
  onPadSwap: (sourceIndex: number, targetIndex: number) => void;
  onChangeSelected: (patch: Partial<PadParams>) => void;
  onAddLayer?: () => void;
  onRemoveLayer?: (layerIndex: number) => void;
  onRelinkSelected?: () => void;
  previewPlayback: PreviewPlayback;
  onPreviewFinished: (triggerId: number) => void;
  onWaveformAudition?: (layerIndex: number) => void;
  /** 直近に発音された MIDI velocity (0..127)。VelocityRangeSlider のマーカー表示用。 */
  liveVelocity?: number | null;
  /** Master output knob position 0..1.  60/66 ≈ 0.909 = 0 dB. */
  masterKnob: number;
  onMasterKnobChange: (v: number) => void;
  masterClipHit: boolean;
  onResetMasterClip: () => void;
}

function PadsViewComponent({
  pads,
  page,
  selectedIndex,
  selectedPad,
  onSelect,
  onPageChange,
  onPadContextMenu,
  onChangePad,
  onSampleDrop,
  onWaveformSampleDrop,
  onReanalyzeSelected,
  onPadSwap,
  onChangeSelected,
  onAddLayer: onAddLayerExternal,
  onRemoveLayer: onRemoveLayerExternal,
  onRelinkSelected,
  previewPlayback,
  onPreviewFinished,
  onWaveformAudition,
  liveVelocity,
  masterKnob,
  onMasterKnobChange,
  masterClipHit,
  onResetMasterClip,
}: PadsViewProps) {
  // Layer-aware view: 選択中 Layer の値を flat フィールドに上書きした「仮想 Pad」を
  // 既存の WaveformEditor / PadControlSections に渡す。編集側 patch は
  // routeLayerPatch で Layer-level キーだけ layers[idx] に振り分けてから親へ。
  const viewPad = useMemo(() => composePadView(selectedPad), [selectedPad]);

  const onChangeLayerAware = useCallback(
    (patch: Partial<PadParams>) => {
      onChangeSelected(routeLayerPatch(selectedPad, patch));
    },
    [onChangeSelected, selectedPad],
  );

  const onSelectLayer = useCallback(
    (layerIndex: number) => {
      onChangeSelected({ selectedLayerIndex: layerIndex });
    },
    [onChangeSelected],
  );

  const onAddLayer = useCallback(() => {
    if (onAddLayerExternal) {
      onAddLayerExternal();
      return;
    }
    const patch = patchAddLayer(selectedPad);
    if (patch) onChangeSelected(patch);
  }, [onAddLayerExternal, onChangeSelected, selectedPad]);

  const onRemoveLayer = useCallback(
    (layerIndex: number) => {
      if (onRemoveLayerExternal) {
        onRemoveLayerExternal(layerIndex);
        return;
      }
      const patch = patchRemoveLayer(selectedPad, layerIndex);
      if (patch) onChangeSelected(patch);
    },
    [onChangeSelected, onRemoveLayerExternal, selectedPad],
  );

  const onToggleLayerMute = useCallback(
    (layerIndex: number) => {
      const current = selectedPad.layers?.[layerIndex]?.mute
        ?? (layerIndex === selectedLayerIndexOf(selectedPad) ? false : false);
      onChangeSelected(patchLayerMuteSolo(selectedPad, layerIndex, 'mute', !current));
    },
    [onChangeSelected, selectedPad],
  );

  const onToggleLayerSolo = useCallback(
    (layerIndex: number) => {
      const current = selectedPad.layers?.[layerIndex]?.solo ?? false;
      onChangeSelected(patchLayerMuteSolo(selectedPad, layerIndex, 'solo', !current));
    },
    [onChangeSelected, selectedPad],
  );

  return (
    <div className={styles.padsView}>
      <div className={styles.main}>
        <section className={styles.padPanel}>
          <PadGrid
            pads={pads}
            page={page}
            selectedIndex={selectedIndex}
            onSelect={onSelect}
            onPageChange={onPageChange}
            onPadContextMenu={onPadContextMenu}
            onChangePad={onChangePad}
            onSampleDrop={onSampleDrop}
            onPadSwap={onPadSwap}
          />
        </section>

        <section className={styles.editorPanel}>
          <LayerTabs
            pad={selectedPad}
            padIndex={selectedIndex}
            onSelect={onSelectLayer}
            onAdd={onAddLayer}
            onRemove={onRemoveLayer}
            onToggleMute={onToggleLayerMute}
            onToggleSolo={onToggleLayerSolo}
          />
          <WaveformEditor
            pad={viewPad}
            padIndex={selectedIndex}
            onChange={onChangeLayerAware}
            onSampleDrop={onWaveformSampleDrop}
            onReanalyze={onReanalyzeSelected}
            onRelinkSample={onRelinkSelected}
            previewPlayback={previewPlayback}
            onPreviewFinished={onPreviewFinished}
            onWaveformAudition={() => onWaveformAudition?.(selectedLayerIndexOf(selectedPad))}
          />
          <PadEditorTabs
            pad={viewPad}
            padIndex={selectedIndex}
            onChange={onChangeLayerAware}
            liveVelocity={liveVelocity}
          />
        </section>
      </div>

      <PadFooterBar
        masterKnob={masterKnob}
        onMasterKnobChange={onMasterKnobChange}
        masterClipHit={masterClipHit}
        onResetMasterClip={onResetMasterClip}
      />
    </div>
  );
}

// memo: skips re-render unless one of the listed props actually changes.
// Audio meter ticks update DOM refs directly, so they do not rebuild this view.
export const PadsView = memo(PadsViewComponent);

interface FooterBarProps {
  masterKnob: number;
  onMasterKnobChange: (v: number) => void;
  masterClipHit: boolean;
  onResetMasterClip: () => void;
}

function PadFooterBar({
  masterKnob,
  onMasterKnobChange,
  masterClipHit,
  onResetMasterClip,
}: FooterBarProps) {
  // OUTPUT knob value IS the fader position [0..1].
  // dB label uses the unified fader curve (utils/fader.ts) so the knob,
  // dB number, and Mixer-tab fader stay in lock-step.
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

  const gearRef = useRef<HTMLButtonElement | null>(null);
  const [settingsOpen, setSettingsOpen] = useState(false);

  return (
    <footer className={styles.footer}>
      <button
        ref={gearRef}
        className={`${styles.gear} ${settingsOpen ? styles.gearActive : ''}`}
        aria-label="settings"
        aria-expanded={settingsOpen}
        onClick={() => setSettingsOpen(v => !v)}
      >
        <svg width="18" height="18" viewBox="0 0 18 18" aria-hidden>
          <path d="M9 2.2 10.1 4.1 12.3 4.3 12.8 6.4 14.5 7.8 13.5 9.8 13.9 12 11.9 13 10.7 14.9 8.5 14.3 6.5 15 5.4 13 3.3 12.5 3.6 10.3 2.2 8.7 3.7 7.1 3.9 4.9 6.1 4.5 7.4 2.8Z"
            fill="currentColor" opacity="0.9" />
          <circle cx="9" cy="9" r="2.3" fill="var(--bg-deep)" />
        </svg>
      </button>
      <SettingsMenu anchorRef={gearRef} open={settingsOpen} onClose={() => setSettingsOpen(false)} />
      <AutomationModeButton />

      <Resource kind="cpu" label="CPU" />
      <Resource kind="mem" label="MEM" />

      <div className={styles.output}>
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
          automationTarget={masterAutomationTarget}
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
        {/* Clip LED — latches red; click to reset */}
        <button
          className={`${styles.clipLed} ${masterClipHit ? styles.clipLedActive : ''}`}
          onClick={onResetMasterClip}
          aria-label="clip indicator (click to reset)"
          title="Clip — click to reset"
        />
      </div>
    </footer>
  );
}

const Resource = memo(function Resource({ kind, label }: { kind: 'cpu' | 'mem'; label: string }) {
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
