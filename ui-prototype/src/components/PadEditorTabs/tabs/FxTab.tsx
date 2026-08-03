import { useCallback, useEffect, useLayoutEffect, useMemo, useRef, useState } from 'react';
import styles from './FxTab.module.css';
import { EqBox } from '../EqBox';
import { FilterCurve } from '../FilterCurve';
import { Knob } from '../../Knob/Knob';
import type { EqParams, FxSlot, PadParams, FilterSlope, FilterParams } from '../../../types';
import { NEUTRAL_EQ } from '../../../types';
import { cloneEq, cloneFxChain, cloneFxSlot, makeFxSlot } from '../../../utils/fxChain';
import { COMP_GR_METER_MARKS, registerCompMeter } from '../../../utils/compMeterRegistry';
import { ensureLayers, selectedLayerIndexOf } from '../../../utils/layerView';
import { parseNumericText } from '../../../utils/numericInput';
import type { DriveType } from '../../../types';

interface Props {
  pad: PadParams;
  onChange: (patch: Partial<PadParams>) => void;
}

const FX_TYPES: Array<{ type: FxSlot['type']; label: string }> = [
  { type: 'EQ', label: 'EQ' },
  { type: 'FILTER', label: 'FILTER' },
  { type: 'DRIVE', label: 'DRIVE' },
  { type: 'TRANSIENT', label: 'TRANSIENT' },
  { type: 'COMPRESSOR', label: 'COMPRESSOR' },
];

const DRIVE_TYPES: Array<{ id: DriveType; label: string }> = [
  { id: 'SOFT_CLIP', label: 'Soft' },
  { id: 'HARD_CLIP', label: 'Hard' },
  { id: 'TUBE', label: 'Tube' },
  { id: 'TAPE', label: 'Tape' },
  { id: 'FOLD', label: 'Fold' },
  { id: 'BIT_CRUSH', label: 'Bit Crush' },
  { id: 'DOWNSAMPLE', label: 'Rate' },
];

function dedupeFxChain(chain: FxSlot[]): FxSlot[] {
  const seen = new Set<FxSlot['type']>();
  return chain.filter(slot => {
    if (seen.has(slot.type)) return false;
    seen.add(slot.type);
    return true;
  });
}

function fxChainOfSelectedLayer(pad: PadParams): FxSlot[] {
  const layers = ensureLayers(pad);
  const layer = layers[selectedLayerIndexOf(pad)];
  return layer?.fxChain ? dedupeFxChain(cloneFxChain(layer.fxChain)) : [];
}

function fmtHz(hz: number): string {
  if (hz >= 1000) return `${(hz / 1000).toFixed(hz >= 10000 ? 0 : 1)}k`;
  return `${Math.round(hz)}`;
}

function fmtPercent(v: number): string {
  return `${Math.round(v * 100)}%`;
}

function fmtDb(v: number): string {
  return `${v >= 0 ? '+' : ''}${v.toFixed(1)}`;
}

function nextDriveType(type: DriveType | undefined): DriveType {
  const current = type ?? 'SOFT_CLIP';
  const index = DRIVE_TYPES.findIndex(t => t.id === current);
  return DRIVE_TYPES[(index + 1) % DRIVE_TYPES.length].id;
}

function driveTypeLabel(type: DriveType | undefined): string {
  return DRIVE_TYPES.find(t => t.id === (type ?? 'SOFT_CLIP'))?.label ?? 'Soft';
}

const FILTER_SLOPES: FilterSlope[] = [12, 24, 48];

// HP / LP それぞれを独立した色分けセクションとして表示する。
function FilterSection({ section, params, onPatch }: {
  section: 'hp' | 'lp';
  params: FilterParams;
  onPatch: (updater: (s: FxSlot) => FxSlot) => void;
}) {
  const isHp = section === 'hp';
  const enabled = isHp ? params.hpEnabled : params.lpEnabled;
  const cutoff = (isHp ? params.hpCutoff : params.lpCutoff) ?? (isHp ? 80 : 18000);
  const slope = (isHp ? params.hpSlope : params.lpSlope) ?? 12;
  const res = (isHp ? params.hpResonance : params.lpResonance) ?? 0.7;

  const setP = (patch: Partial<FilterParams>) =>
    onPatch(s => (s.type === 'FILTER' ? { ...s, params: { ...s.params, ...patch } } : s));

  return (
    <div className={`${styles.filterSection} ${isHp ? styles.secHp : styles.secLp} ${enabled ? '' : styles.secOff}`}>
      <button
        type="button"
        className={styles.secToggle}
        aria-pressed={enabled}
        onClick={() => setP(isHp ? { hpEnabled: !enabled } : { lpEnabled: !enabled })}
        title={isHp ? 'High-pass on/off' : 'Low-pass on/off'}
      >
        <span className={styles.secDot} />
        {isHp ? 'HIGH-PASS' : 'LOW-PASS'}
      </button>
      <div className={styles.secKnobs}>
        <Knob
          size={36}
          label="FREQ"
          value={freqToNorm(cutoff)}
          defaultValue={freqToNorm(isHp ? 80 : 18000)}
          valueText={fmtHz(cutoff)}
          parseInput={text => { const v = parseNumericText(text); return v == null ? null : freqToNorm(v); }}
          onChange={v => setP(isHp ? { hpCutoff: normToFreq(v) } : { lpCutoff: normToFreq(v) })}
        />
        <Knob
          size={36}
          label="RESO"
          value={res}
          min={0.2}
          max={8}
          defaultValue={0.7}
          valueText={res.toFixed(2)}
          parseInput={parseNumericText}
          onChange={v => setP(isHp ? { hpResonance: v } : { lpResonance: v })}
        />
      </div>
      <div className={styles.slopeSeg} role="group" aria-label="slope dB/oct">
        {FILTER_SLOPES.map(s => (
          <button
            key={s}
            type="button"
            className={`${styles.slopeBtn} ${slope === s ? styles.slopeOn : ''}`}
            onClick={() => setP(isHp ? { hpSlope: s } : { lpSlope: s })}
            title={`${s} dB/oct`}
          >
            {s}
          </button>
        ))}
      </div>
    </div>
  );
}

function freqToNorm(hz: number): number {
  const minLog = Math.log10(20);
  const maxLog = Math.log10(20000);
  return (Math.log10(Math.max(20, Math.min(20000, hz))) - minLog) / (maxLog - minLog);
}

function normToFreq(n: number): number {
  const minLog = Math.log10(20);
  const maxLog = Math.log10(20000);
  return Math.pow(10, minLog + Math.max(0, Math.min(1, n)) * (maxLog - minLog));
}

export function FxTab({ pad, onChange }: Props) {
  const [menuOpen, setMenuOpen] = useState(false);
  const [contextMenu, setContextMenu] = useState<{ index: number; x: number; y: number } | null>(null);
  const [copiedSlot, setCopiedSlot] = useState<FxSlot | null>(null);
  const [dragIndex, setDragIndex] = useState<number | null>(null);
  const [addedSlotIndex, setAddedSlotIndex] = useState<number | null>(null);
  const chain = useMemo(() => fxChainOfSelectedLayer(pad), [pad]);
  const chainRef = useRef<HTMLDivElement | null>(null);
  const shouldRevealAddedRef = useRef(false);
  const dragPreviewRef = useRef<HTMLElement | null>(null);
  const stateRef = useRef({ chain, onChange });
  stateRef.current = { chain, onChange };

  const eqFromChain = useCallback((nextChain: FxSlot[]): EqParams => {
    const eqSlot = nextChain.find(slot => slot.type === 'EQ');
    return eqSlot?.type === 'EQ' ? cloneEq(eqSlot.params) : cloneEq(NEUTRAL_EQ);
  }, []);

  const commitChain = useCallback((nextChain: FxSlot[]) => {
    const uniqueChain = dedupeFxChain(nextChain);
    stateRef.current.onChange({
      fxChain: uniqueChain,
      eq: eqFromChain(uniqueChain),
    });
  }, [eqFromChain]);

  const addFx = useCallback((type: FxSlot['type']) => {
    const { chain } = stateRef.current;
    if (chain.some(slot => slot.type === type)) {
      setMenuOpen(false);
      return;
    }
    const nextSlot = makeFxSlot(type);
    const nextChain = [...cloneFxChain(chain), nextSlot];
    shouldRevealAddedRef.current = true;
    setAddedSlotIndex(nextChain.length - 1);
    setMenuOpen(false);
    commitChain(nextChain);
  }, [commitChain]);

  const removeFx = useCallback((index: number) => {
    const { chain } = stateRef.current;
    const nextChain = chain.filter((_, i) => i !== index);
    commitChain(nextChain);
  }, [commitChain]);

  const toggleBypass = useCallback((index: number) => {
    const { chain } = stateRef.current;
    const nextChain = chain.map((slot, i) => {
      if (i !== index) return slot;
      const bypassed = !slot.bypassed;
      if (slot.type === 'EQ') {
        return { ...slot, bypassed, params: { ...slot.params, bypassed } } as FxSlot;
      }
      return { ...slot, bypassed } as FxSlot;
    });
    commitChain(nextChain);
  }, [commitChain]);

  const patchEqSlot = useCallback((index: number, updater: (eq: EqParams) => EqParams) => {
    const { chain } = stateRef.current;
    const nextChain = chain.map((slot, i) =>
      i === index && slot.type === 'EQ'
        ? (() => {
            const nextEq = updater(cloneEq(slot.params));
            return { type: 'EQ' as const, bypassed: nextEq.bypassed, params: cloneEq(nextEq) };
          })()
        : slot,
    );
    commitChain(nextChain);
  }, [commitChain]);

  const patchSlot = useCallback((index: number, updater: (slot: FxSlot) => FxSlot) => {
    const { chain } = stateRef.current;
    commitChain(chain.map((slot, i) => (i === index ? updater(slot) : slot)));
  }, [commitChain]);

  const slotWithCurrentSettings = useCallback((slot: FxSlot): FxSlot => {
    if (slot.type === 'EQ') return { type: 'EQ', bypassed: slot.bypassed, params: cloneEq(slot.params) };
    return cloneFxSlot(slot);
  }, []);

  const copySlotSettings = useCallback((index: number) => {
    const slot = stateRef.current.chain[index];
    if (!slot) return;
    setCopiedSlot(slotWithCurrentSettings(slot));
    setContextMenu(null);
  }, [slotWithCurrentSettings]);

  const pasteSlotSettings = useCallback((index: number) => {
    if (!copiedSlot) return;
    const { chain } = stateRef.current;
    const target = chain[index];
    if (!target || target.type !== copiedSlot.type) return;

    const nextChain = chain.map((slot, i) => {
      if (i !== index) return slot;
      const source = cloneFxSlot(copiedSlot);
      return { ...source, bypassed: slot.bypassed } as FxSlot;
    });
    commitChain(nextChain);
    setContextMenu(null);
  }, [commitChain, copiedSlot]);

  const resetSlotSettings = useCallback((index: number) => {
    const { chain } = stateRef.current;
    const target = chain[index];
    if (!target) return;
    const reset = { ...makeFxSlot(target.type), bypassed: target.bypassed } as FxSlot;
    const nextChain = chain.map((slot, i) => (i === index ? reset : slot));
    commitChain(nextChain);
    setContextMenu(null);
  }, [commitChain]);

  const moveSlot = useCallback((from: number, to: number) => {
    const { chain } = stateRef.current;
    if (from === to || from < 0 || to < 0 || from >= chain.length || to >= chain.length) return;
    const nextChain = cloneFxChain(chain);
    const [slot] = nextChain.splice(from, 1);
    if (!slot) return;
    nextChain.splice(to, 0, slot);
    commitChain(nextChain);
    setContextMenu(null);
  }, [commitChain]);

  const onContextMenu = useCallback((index: number) => (event: React.MouseEvent) => {
    event.preventDefault();
    setMenuOpen(false);
    setContextMenu({ index, x: event.clientX, y: event.clientY });
  }, []);

  const cleanupDragPreview = useCallback(() => {
    dragPreviewRef.current?.remove();
    dragPreviewRef.current = null;
  }, []);

  const onDragStart = useCallback((index: number) => (event: React.DragEvent) => {
    setDragIndex(index);
    event.dataTransfer.effectAllowed = 'move';
    event.dataTransfer.setData('text/plain', String(index));
    cleanupDragPreview();

    const frame = event.currentTarget.closest(`.${styles.slotFrame}`) as HTMLElement | null;
    if (!frame) return;

    const rect = frame.getBoundingClientRect();
    const preview = frame.cloneNode(true) as HTMLElement;
    preview.classList.add(styles.dragPreview);
    preview.style.width = `${rect.width}px`;
    preview.style.height = `${rect.height}px`;
    preview.style.position = 'fixed';
    preview.style.left = '-10000px';
    preview.style.top = '-10000px';
    preview.style.pointerEvents = 'none';
    document.body.appendChild(preview);
    dragPreviewRef.current = preview;
    event.dataTransfer.setDragImage(preview, Math.min(rect.width - 8, 24), 16);
  }, [cleanupDragPreview]);

  const onDragOver = useCallback((index: number) => (event: React.DragEvent) => {
    if (dragIndex == null || dragIndex === index) return;
    event.preventDefault();
    event.dataTransfer.dropEffect = 'move';
  }, [dragIndex]);

  const onDrop = useCallback((index: number) => (event: React.DragEvent) => {
    event.preventDefault();
    const raw = event.dataTransfer.getData('text/plain');
    const from = raw ? Number(raw) : dragIndex;
    if (from != null && Number.isFinite(from)) moveSlot(from, index);
    setDragIndex(null);
    cleanupDragPreview();
  }, [cleanupDragPreview, dragIndex, moveSlot]);

  useLayoutEffect(() => {
    if (!shouldRevealAddedRef.current) return;
    const scroller = chainRef.current;
    if (!scroller) return;

    shouldRevealAddedRef.current = false;
    requestAnimationFrame(() => {
      scroller.scrollTo({ left: scroller.scrollWidth, behavior: 'smooth' });
    });
  }, [chain.length]);

  useEffect(() => {
    if (addedSlotIndex == null) return;
    const timer = window.setTimeout(() => setAddedSlotIndex(null), 900);
    return () => window.clearTimeout(timer);
  }, [addedSlotIndex]);

  useEffect(() => cleanupDragPreview, [cleanupDragPreview]);

  useEffect(() => {
    if (!contextMenu) return;
    const close = () => setContextMenu(null);
    const onKey = (event: KeyboardEvent) => {
      if (event.key === 'Escape') setContextMenu(null);
    };
    window.addEventListener('click', close);
    window.addEventListener('keydown', onKey);
    return () => {
      window.removeEventListener('click', close);
      window.removeEventListener('keydown', onKey);
    };
  }, [contextMenu]);

  const contextSlot = contextMenu ? chain[contextMenu.index] : undefined;
  const canPaste = !!contextSlot && !!copiedSlot && contextSlot.type === copiedSlot.type;

  return (
    <div className={styles.root}>
      <div className={styles.toolbar}>
        <div className={styles.titleBlock}>
          <span className={styles.count}>{chain.length} SLOT{chain.length === 1 ? '' : 'S'}</span>
        </div>
        <div className={styles.addWrap}>
          <button
            type="button"
            className={styles.addButton}
            onClick={() => setMenuOpen(open => !open)}
            aria-expanded={menuOpen}
          >
            + Add FX
          </button>
          {menuOpen && (
            <div className={styles.menu} role="menu">
              {FX_TYPES.map(fx => {
                const exists = chain.some(slot => slot.type === fx.type);
                return (
                  <button
                    key={fx.type}
                    type="button"
                    role="menuitem"
                    disabled={exists}
                    title={exists ? `${fx.label} is already in this layer` : undefined}
                    onClick={() => addFx(fx.type)}
                  >
                    {fx.label}
                    {exists ? <span>ADDED</span> : null}
                  </button>
                );
              })}
            </div>
          )}
        </div>
      </div>

      {chain.length === 0 ? (
        <div className={styles.empty}>
          <span>No FX in this layer</span>
          <button type="button" onClick={() => setMenuOpen(true)}>Add FX</button>
        </div>
      ) : (
        <div className={styles.chain} ref={chainRef}>
          {chain.map((slot, index) => (
            <div
              key={`${slot.type}-${index}`}
              className={`${styles.slotFrame} ${dragIndex === index ? styles.dragging : ''} ${addedSlotIndex === index ? styles.justAdded : ''}`}
              onContextMenu={onContextMenu(index)}
              onDragOver={onDragOver(index)}
              onDrop={onDrop(index)}
            >
              <button
                type="button"
                className={styles.dragHandle}
                draggable
                onDragStart={onDragStart(index)}
                onDragEnd={() => {
                  setDragIndex(null);
                  cleanupDragPreview();
                }}
                title="Drag to reorder"
                aria-label={`Drag ${slot.type} to reorder`}
              >
                |||
              </button>
              {slot.type === 'EQ'
                ? (
                  <EqBox
                    eq={slot.params}
                    patchEq={updater => patchEqSlot(index, updater)}
                    onRemove={() => removeFx(index)}
                  />
                )
                : (
                  <FxModule
                    slot={slot}
                    slotIndex={index}
                    onToggleBypass={() => toggleBypass(index)}
                    onRemove={() => removeFx(index)}
                    onPatch={updater => patchSlot(index, updater)}
                  />
                )}
              {dragIndex === index && (
                <div className={styles.dragOverlay} aria-hidden>
                  <span>Moving {slot.type}</span>
                </div>
              )}
            </div>
          ))}
        </div>
      )}
      {contextMenu && contextSlot && (
        <div
          className={styles.contextMenu}
          style={{ left: contextMenu.x, top: contextMenu.y }}
          role="menu"
          onClick={event => event.stopPropagation()}
        >
          <button type="button" role="menuitem" onClick={() => copySlotSettings(contextMenu.index)}>
            Copy Settings
          </button>
          <button
            type="button"
            role="menuitem"
            disabled={!canPaste}
            onClick={() => pasteSlotSettings(contextMenu.index)}
          >
            Paste Settings
          </button>
          <button type="button" role="menuitem" onClick={() => resetSlotSettings(contextMenu.index)}>
            Reset Settings
          </button>
        </div>
      )}
    </div>
  );
}

function FxModule({
  slot,
  slotIndex,
  onToggleBypass,
  onRemove,
  onPatch,
}: {
  slot: Exclude<FxSlot, { type: 'EQ' }>;
  slotIndex: number;
  onToggleBypass: () => void;
  onRemove: () => void;
  onPatch: (updater: (slot: FxSlot) => FxSlot) => void;
}) {
  const title = slot.type === 'COMPRESSOR' ? 'COMPRESSOR' : slot.type;

  return (
    <div className={`${styles.module} ${slot.bypassed ? styles.bypassed : ''}`}>
      <div className={styles.moduleHeader}>
        <div className={styles.moduleTitle} data-fx={slot.type}>
          <span className={styles.titleDot} />
          <span>{title}</span>
        </div>
        <div className={styles.headerActions}>
          <button
            type="button"
            className={`${styles.iconBtn} ${slot.bypassed ? styles.powerOff : styles.powerOn}`}
            onClick={onToggleBypass}
            aria-pressed={!slot.bypassed}
            title={slot.bypassed ? 'Bypassed - click to activate' : 'Active - click to bypass'}
          >
            <PowerIcon />
          </button>
          <button type="button" className={`${styles.iconBtn} ${styles.removeBtn}`} onClick={onRemove} title={`Remove ${title}`}>
            X
          </button>
        </div>
      </div>
      {slot.type === 'FILTER' && (
        <FilterCurve
          params={slot.params}
          bypassed={slot.bypassed}
          onChange={patch => onPatch(s => s.type === 'FILTER' ? { ...s, params: { ...s.params, ...patch } } : s)}
        />
      )}
      <div className={styles.controls}>
        {slot.type === 'FILTER' && (
          <div className={styles.filterSections}>
            <FilterSection section="hp" params={slot.params} onPatch={onPatch} />
            <FilterSection section="lp" params={slot.params} onPatch={onPatch} />
          </div>
        )}
        {slot.type === 'DRIVE' && (
          <>
            <button
              type="button"
              className={styles.typeButton}
              onClick={() => onPatch(s => s.type === 'DRIVE' ? { ...s, params: { ...s.params, type: nextDriveType(s.params.type) } } : s)}
              title="Drive Type"
            >
              <span>TYPE</span>
              <b>{driveTypeLabel(slot.params.type)}</b>
            </button>
            <Knob
              size={32}
              label="DRIVE"
              value={slot.params.amount}
              defaultValue={0.25}
              valueText={fmtPercent(slot.params.amount)}
              onChange={v => onPatch(s => s.type === 'DRIVE' ? { ...s, params: { ...s.params, amount: v } } : s)}
            />
            <Knob
              size={32}
              label="TONE"
              value={slot.params.tone}
              defaultValue={0.5}
              valueText={fmtPercent(slot.params.tone)}
              onChange={v => onPatch(s => s.type === 'DRIVE' ? { ...s, params: { ...s.params, tone: v } } : s)}
            />
            <Knob
              size={32}
              label="MIX"
              value={slot.params.mix}
              defaultValue={1}
              valueText={fmtPercent(slot.params.mix)}
              onChange={v => onPatch(s => s.type === 'DRIVE' ? { ...s, params: { ...s.params, mix: v } } : s)}
            />
            <Knob
              size={32}
              label="OUTPUT"
              value={slot.params.output ?? 0}
              min={-24}
              max={12}
              defaultValue={0}
              valueText={fmtDb(slot.params.output ?? 0)}
              parseInput={parseNumericText}
              onChange={v => onPatch(s => s.type === 'DRIVE' ? { ...s, params: { ...s.params, output: v } } : s)}
            />
          </>
        )}
        {slot.type === 'TRANSIENT' && (
          <>
            <Knob
              size={38}
              label="ATK"
              value={slot.params.attack}
              min={-1}
              max={1}
              bipolar
              defaultValue={0}
              valueText={`${slot.params.attack >= 0 ? '+' : ''}${Math.round(slot.params.attack * 100)}`}
              parseInput={parseNumericText}
              onChange={v => onPatch(s => s.type === 'TRANSIENT' ? { ...s, params: { ...s.params, attack: v } } : s)}
            />
            <Knob
              size={38}
              label="SUS"
              value={slot.params.sustain}
              min={-1}
              max={1}
              bipolar
              defaultValue={0}
              valueText={`${slot.params.sustain >= 0 ? '+' : ''}${Math.round(slot.params.sustain * 100)}`}
              parseInput={parseNumericText}
              onChange={v => onPatch(s => s.type === 'TRANSIENT' ? { ...s, params: { ...s.params, sustain: v } } : s)}
            />
          </>
        )}
        {slot.type === 'COMPRESSOR' && (
          <div className={styles.compControls}>
            <div className={styles.compDynamicsRow}>
              <Knob
                size={32}
                label="THR"
                value={slot.params.threshold}
                min={-48}
                max={0}
                defaultValue={-12}
                valueText={`${Math.round(slot.params.threshold)} dB`}
                parseInput={parseNumericText}
                onChange={v => onPatch(s => s.type === 'COMPRESSOR' ? { ...s, params: { ...s.params, threshold: v } } : s)}
              />
              <Knob
                size={32}
                label="RATIO"
                value={slot.params.ratio}
                min={1}
                max={20}
                defaultValue={4}
                valueText={`${slot.params.ratio.toFixed(1)}:1`}
                parseInput={parseNumericText}
                onChange={v => onPatch(s => s.type === 'COMPRESSOR' ? { ...s, params: { ...s.params, ratio: v } } : s)}
              />
              <Knob
                size={32}
                label="ATK"
                value={slot.params.attack}
                min={1}
                max={80}
                defaultValue={8}
                valueText={`${Math.round(slot.params.attack)} ms`}
                parseInput={parseNumericText}
                onChange={v => onPatch(s => s.type === 'COMPRESSOR' ? { ...s, params: { ...s.params, attack: v } } : s)}
              />
              <Knob
                size={32}
                label="REL"
                value={slot.params.release}
                min={10}
                max={500}
                defaultValue={80}
                valueText={`${Math.round(slot.params.release)} ms`}
                parseInput={parseNumericText}
                onChange={v => onPatch(s => s.type === 'COMPRESSOR' ? { ...s, params: { ...s.params, release: v } } : s)}
              />
            </div>
            <div className={styles.compFlowRow} aria-label="Compressor gain flow: Make Up, Mix, Output">
              <Knob
                size={34}
                label="MAKE UP"
                value={slot.params.makeup}
                min={0}
                max={24}
                defaultValue={0}
                valueText={`${fmtDb(slot.params.makeup)} dB`}
                parseInput={parseNumericText}
                onChange={v => onPatch(s => s.type === 'COMPRESSOR' ? { ...s, params: { ...s.params, makeup: v } } : s)}
              />
              <span className={styles.signalArrow} aria-hidden>→</span>
              <Knob
                size={34}
                label="MIX"
                value={slot.params.mix}
                defaultValue={1}
                valueText={fmtPercent(slot.params.mix)}
                onChange={v => onPatch(s => s.type === 'COMPRESSOR' ? { ...s, params: { ...s.params, mix: v } } : s)}
              />
              <span className={styles.signalArrow} aria-hidden>→</span>
              <Knob
                size={34}
                label="OUTPUT"
                value={slot.params.output}
                min={-24}
                max={12}
                defaultValue={0}
                valueText={`${fmtDb(slot.params.output)} dB`}
                parseInput={parseNumericText}
                onChange={v => onPatch(s => s.type === 'COMPRESSOR' ? { ...s, params: { ...s.params, output: v } } : s)}
              />
            </div>
          </div>
        )}
      </div>
      {slot.type === 'COMPRESSOR' && <CompReductionMeter slotIndex={slotIndex} />}
    </div>
  );
}

// Compressor のゲインリダクションメーター (C++ から rAF 経由で driven)
function CompReductionMeter({ slotIndex }: { slotIndex: number }) {
  const fillRef = useRef<HTMLElement | null>(null);
  const peakRef = useRef<HTMLElement | null>(null);
  useEffect(
    () => registerCompMeter(slotIndex, { fill: fillRef.current, peak: peakRef.current, label: null }),
    [slotIndex],
  );
  return (
    <div className={styles.grMeter} aria-label="gain reduction">
      <span className={styles.grLabel}>GR</span>
      <span className={styles.grTrack}>
        <i ref={fillRef} className={styles.grFill} style={{ transform: 'scaleX(0)' }} />
        <s ref={peakRef} className={styles.grPeak} style={{ display: 'none' }} />
        <span className={styles.grScale} aria-hidden>
          {COMP_GR_METER_MARKS.map(mark => (
            <span
              key={mark.db}
              className={styles.grMark}
              style={{ left: `${mark.amount * 100}%` }}
            >
              <i />
              <b>-{mark.db}</b>
            </span>
          ))}
        </span>
      </span>
    </div>
  );
}

function PowerIcon() {
  return (
    <svg width="10" height="10" viewBox="0 0 14 14" aria-hidden>
      <path d="M7 1.5 V 7" stroke="currentColor" strokeWidth="1.6" strokeLinecap="round" fill="none" />
      <path d="M4 4 A 4 4 0 1 0 10 4" stroke="currentColor" strokeWidth="1.4" fill="none" strokeLinecap="round" />
    </svg>
  );
}
