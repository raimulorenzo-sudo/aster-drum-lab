import { useCallback, useState } from 'react';
import shared from '../../PadControlSections/PadControlSections.module.css';
import styles from './PlaybackTab.module.css';
import { Knob } from '../../Knob/Knob';
import { VelocityRangeSlider } from '../../VelocityRangeSlider/VelocityRangeSlider';
import { defaultPadParam } from '../../../data/parameterSpecs';
import { formatPan, formatPitch, formatVolume } from '../../../utils/parameterFormat';
import { dbToPosition } from '../../../utils/fader';
import { parseNumericText, parsePanInput } from '../../../utils/numericInput';
import {
  ensureLayers,
  patchAutoSplitVelocity,
  patchLayerVelocityRange,
  selectedLayerIndexOf,
} from '../../../utils/layerView';
import type { LayerParams, PadParams } from '../../../types';

interface Props {
  pad: PadParams;
  padIndex: number;
  onChange: (patch: Partial<PadParams>) => void;
  liveVelocity?: number | null;
}

export function PlaybackTab({ pad, padIndex, onChange, liveVelocity }: Props) {
  const ownerKey = padIndex;
  const layerIdx = selectedLayerIndexOf(pad);
  const layers   = ensureLayers(pad);
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

      {/* ─── 選択中 Layer 固有の再生パラメータ ─── */}
      <div className={styles.controlsRow}>
        <Knob ownerKey={ownerKey} size={60} label="LAYER VOL"
          value={pad.volume} defaultValue={defaultPadParam('volume')}
          valueText={formatVolume(pad.volume)}
          parseInput={text => { const v = parseNumericText(text); return v == null ? null : dbToPosition(v); }}
          onChange={v => onChange({ volume: v })} />
        <Knob ownerKey={ownerKey} size={60} label="LAYER PAN" bipolar
          value={pad.pan} min={-1} max={1} defaultValue={defaultPadParam('pan')}
          valueText={formatPan(pad.pan)}
          parseInput={parsePanInput}
          onChange={v => onChange({ pan: v })} />
        <Knob ownerKey={ownerKey} size={60} label="LAYER PITCH" bipolar
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

      </div>

      {/* ── Layer velocity range ── */}
      <div className={styles.velocitySection}>
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
