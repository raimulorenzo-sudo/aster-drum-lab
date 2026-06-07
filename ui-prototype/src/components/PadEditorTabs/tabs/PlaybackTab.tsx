import { useCallback, useState } from 'react';
import shared from '../../PadControlSections/PadControlSections.module.css';
import styles from './PlaybackTab.module.css';
import { Knob } from '../../Knob/Knob';
import { OutputAssignDropdown } from '../../OutputAssignDropdown/OutputAssignDropdown';
import { VelocityRangeSlider } from '../../VelocityRangeSlider/VelocityRangeSlider';
import { defaultPadParam } from '../../../data/parameterSpecs';
import { formatPan, formatPitch, formatVolume } from '../../../utils/parameterFormat';
import { dbToPosition } from '../../../utils/fader';
import { parseNumericText, parsePanInput } from '../../../utils/numericInput';
import {
  ensureLayers,
  layerCountOf,
  patchAutoSplitVelocity,
  patchLayerVelocityRange,
  selectedLayerIndexOf,
} from '../../../utils/layerView';
import type { LayerParams, PadParams, PlayMode } from '../../../types';

interface Props {
  pad: PadParams;
  padIndex: number;
  onChange: (patch: Partial<PadParams>) => void;
  liveVelocity?: number | null;
}

const PLAY_MODES: { id: PlayMode; label: string }[] = [
  { id: 'OneShot', label: 'ONE SHOT' },
  { id: 'Gate',    label: 'GATE'     },
];

export function PlaybackTab({ pad, padIndex, onChange, liveVelocity }: Props) {
  const ownerKey = padIndex;
  const layerIdx = selectedLayerIndexOf(pad);
  const layers   = ensureLayers(pad);
  const isMultiLayer = layerCountOf(pad) >= 2;
  const polarityOn   = !!layers[layerIdx]?.polarityInvert;
  const [velOpen, setVelOpen] = useState(false);

  const onChangeRange = useCallback(
    (targetIdx: number, next: { min: number; max: number }) =>
      onChange(patchLayerVelocityRange(pad, targetIdx, next)),
    [onChange, pad],
  );
  const onSelectLayer = useCallback(
    (targetIdx: number) => {
      if (targetIdx !== layerIdx) onChange({ selectedLayerIndex: targetIdx });
    },
    [onChange, layerIdx],
  );
  const onAutoSplit = useCallback(
    (splits: number) => onChange(patchAutoSplitVelocity(pad, splits)),
    [onChange, pad],
  );
  const togglePolarity = useCallback(
    () => onChange({ polarityInvert: !polarityOn }),
    [onChange, polarityOn],
  );

  return (
    <div className={styles.root}>

      {/* ─── 上段: Layer / Pad volume と音作り・出力を全幅で均等配置 ─── */}
      <div className={styles.controlsRow}>
        <Knob ownerKey={ownerKey} size={60} label="LAYER VOL"
          value={pad.volume} defaultValue={defaultPadParam('volume')}
          valueText={formatVolume(pad.volume)}
          parseInput={text => { const v = parseNumericText(text); return v == null ? null : dbToPosition(v); }}
          onChange={v => onChange({ volume: v })} />
        <Knob ownerKey={ownerKey} size={48} label="PAD VOL"
          value={pad.padVolume ?? 0.75} defaultValue={defaultPadParam('volume')}
          valueText={formatVolume(pad.padVolume ?? 0.75)}
          parseInput={text => { const v = parseNumericText(text); return v == null ? null : dbToPosition(v); }}
          onChange={v => onChange({ padVolume: v })} />
        <Knob ownerKey={ownerKey} size={54} label="PAN" bipolar
          value={pad.pan} min={-1} max={1} defaultValue={defaultPadParam('pan')}
          valueText={formatPan(pad.pan)}
          parseInput={parsePanInput}
          onChange={v => onChange({ pan: v })} />
        <Knob ownerKey={ownerKey} size={54} label="PITCH" bipolar
          value={pad.pitch} min={-24} max={24} defaultValue={defaultPadParam('pitch')}
          valueText={formatPitch(pad.pitch)}
          parseInput={parseNumericText}
          onChange={v => onChange({ pitch: v })} />

        {/* PHASE (内部名 polarityInvert, Layer 単位) */}
        <div className={styles.slot}>
          <span className={styles.slotLabel}>PHASE</span>
          <button
            type="button"
            className={`${styles.polarityBtn} ${polarityOn ? styles.polarityOn : ''}`}
            onClick={togglePolarity}
            aria-pressed={polarityOn}
            aria-label="Polarity Invert"
            title="Polarity Invert (Ø) — flip this layer's polarity"
          >
            Ø
          </button>
        </div>

        {/* OUTPUT */}
        <div className={styles.slot}>
          <span className={styles.slotLabel}>OUTPUT</span>
          <OutputAssignDropdown
            width={92}
            value={pad.outputAssign}
            onChange={v => onChange({ outputAssign: v })}
          />
        </div>
      </div>

      <div className={styles.divider} />

      {/* ─── 下段: 左 トリガー設定 / 右 VEL RANGE (multi-layer のみ) ─── */}
      <div className={`${styles.lower} ${isMultiLayer ? '' : styles.lowerSingle}`}>

        <div className={styles.segStack}>
          {/* MODE */}
          <div className={styles.modeBlock}>
            <span className={`${shared.fieldLabel} ${styles.inlineLabel}`}>MODE</span>
            <div className={styles.segGroup}>
              {PLAY_MODES.map(m => (
                <button key={m.id}
                  className={`${styles.segBtn} ${pad.playMode === m.id ? styles.segBtnActive : ''}`}
                  onClick={() => onChange({ playMode: m.id })}
                >
                  {m.label}
                </button>
              ))}
            </div>
          </div>

          {/* CHOKE GROUP */}
          <div className={styles.chokeBlock}>
            <span className={`${shared.fieldLabel} ${styles.inlineLabel}`}>CHOKE</span>
            <div className={styles.segGroup}>
              {[0,1,2,3,4].map(v => (
                <button key={v}
                  className={`${styles.segBtn} ${styles.segBtnNarrow} ${pad.chokeGroup === v ? styles.segBtnActive : ''}`}
                  onClick={() => onChange({ chokeGroup: v })}
                >
                  {v === 0 ? 'OFF' : v}
                </button>
              ))}
            </div>
          </div>

          {/* VOICES (Polyphony + Voice Steal) */}
          <div className={styles.chokeBlock}>
            <span className={`${shared.fieldLabel} ${styles.inlineLabel}`}>VOICES</span>
            <div className={styles.segGroup}>
              {[
                { v: 1,  l: '1' },
                { v: 2,  l: '2' },
                { v: 4,  l: '4' },
                { v: 8,  l: '8' },
                { v: 16, l: '16' },
                { v: 0,  l: '∞' },
              ].map(o => (
                <button key={o.v}
                  className={`${styles.segBtn} ${styles.segBtnNarrow} ${(pad.polyphony ?? 0) === o.v ? styles.segBtnActive : ''}`}
                  onClick={() => onChange({ polyphony: o.v })}
                  title={`Polyphony: ${o.l}`}
                >
                  {o.l}
                </button>
              ))}
            </div>
            <span className={styles.stealLabel}>STEAL</span>
            <div className={styles.segGroup}>
              {[
                { v: 'oldest'   as const, l: 'OLD'   },
                { v: 'quietest' as const, l: 'QUIET' },
                { v: 'off'      as const, l: 'OFF'   },
              ].map(o => {
                const cur = pad.voiceSteal ?? 'oldest';
                const disabled = (pad.polyphony ?? 0) === 0;
                return (
                  <button key={o.v}
                    className={`${styles.segBtn} ${styles.segBtnNarrow} ${cur === o.v ? styles.segBtnActive : ''} ${disabled ? styles.segBtnDimmed : ''}`}
                    onClick={() => { if (!disabled) onChange({ voiceSteal: o.v }); }}
                    title={disabled ? 'Polyphony が Unlimited のため無効' : `Voice steal: ${o.v}`}
                  >
                    {o.l}
                  </button>
                );
              })}
            </div>
          </div>
        </div>

        {/* ── RIGHT: VEL RANGE (multi-layer のみ。単 Layer 時は列ごと出さない) ── */}
        {isMultiLayer && (
          <div className={styles.rightCol}>
            <button
              type="button"
              className={styles.velToggle}
              onClick={() => setVelOpen(o => !o)}
              aria-expanded={velOpen}
            >
              <span className={shared.fieldLabel}>VEL RANGE</span>
              <svg
                className={`${styles.velChevron} ${velOpen ? styles.velChevronOpen : ''}`}
                width="8" height="8" viewBox="0 0 8 8" aria-hidden
              >
                <path d="M1.5 2.5 L4 5.5 L6.5 2.5"
                  stroke="currentColor" strokeWidth="1.3" fill="none" strokeLinecap="round" />
              </svg>
            </button>

            {!velOpen && (
              <div className={styles.velPreview}>
                <VelChip label="MAIN" layer={layers[0]} active={layerIdx === 0} />
                {layerIdx > 0 && (
                  <VelChip
                    label={`L${layerIdx + 1}`}
                    layer={layers[layerIdx]}
                    active
                  />
                )}
              </div>
            )}

            {velOpen && (
              <div className={styles.velSliderWrap}>
                <VelocityRangeSlider
                  layers={layers}
                  activeLayerIndex={layerIdx}
                  onSelectLayer={onSelectLayer}
                  onChangeRange={onChangeRange}
                  onAutoSplit={onAutoSplit}
                  liveVelocity={liveVelocity}
                />
              </div>
            )}
          </div>
        )}

      </div>
    </div>
  );
}

function VelChip({ label, layer, active }: { label: string; layer: LayerParams | undefined; active: boolean }) {
  const min = layer?.velocityMin ?? 0;
  const max = layer?.velocityMax ?? 127;
  return (
    <div className={`${styles.velChip} ${active ? styles.velChipActive : ''}`}>
      <span className={styles.velChipLabel}>{label}</span>
      <span className={styles.velChipRange}>{min}–{max}</span>
    </div>
  );
}
