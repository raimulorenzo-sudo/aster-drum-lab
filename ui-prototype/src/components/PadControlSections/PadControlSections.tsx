import { memo, useCallback } from 'react';
import styles from './PadControlSections.module.css';
import { Knob } from '../Knob/Knob';
import { OutputAssignDropdown } from '../OutputAssignDropdown/OutputAssignDropdown';
import { VelocityRangeSlider } from '../VelocityRangeSlider/VelocityRangeSlider';
import { defaultPadParam } from '../../data/parameterSpecs';
import { formatMs, formatPan, formatPitch, formatTrimPercent, formatVolume } from '../../utils/parameterFormat';
import { trimFromPad } from '../../utils/sampleTrim';
import { dbToPosition } from '../../utils/fader';
import { parseNumericText, parsePanInput, parsePercentInput } from '../../utils/numericInput';
import {
  ensureLayers,
  layerCountOf,
  patchAutoSplitVelocity,
  patchLayerVelocityRange,
  selectedLayerIndexOf,
} from '../../utils/layerView';
import type { PadParams, PlayMode } from '../../types';

interface PadControlSectionsProps {
  pad: PadParams;
  /**
   * 編集対象 Pad の絶対 index (0..47)。Knob.memo を貫通させて
   * onChange / onReset closure を確実に更新させるための ownerKey に使う。
   * (同じ値のノブが複数 Pad で重なるとき、memo が同 prop と判断して
   *  古い Pad 向け closure を握り続けるバグの予防策)
   */
  padIndex: number;
  onChange: (patch: Partial<PadParams>) => void;
}

function PadControlSectionsComponent({ pad, padIndex, onChange }: PadControlSectionsProps) {
  const trim = trimFromPad(pad);
  const totalMs = trim.sampleLengthMs;
  const playbackRangeMs = Math.max(1, trim.endMs - trim.startMs);
  // ownerKey: Pad が切り替わったら全ノブを必ず1回再レンダーさせる識別子
  const ownerKey = padIndex;

  // 選択中 Layer のインデックス + Layer 配列。VelocityRangeSlider は全 Layer を見せる。
  const layerIdx = selectedLayerIndexOf(pad);
  const layers = ensureLayers(pad);

  // Layer 機能の UI 表出制御。1Layer 状態では L1 バッジ / Velocity Range / Auto Split
  // を出さず、Layer 機能追加前に近い見た目に戻す。
  const layerCount = layerCountOf(pad);
  const isMultiLayer = layerCount >= 2;
  // バッジは L2 以降のみ表示（MAIN=Layer 0 では何も載せない）
  const layerBadge = isMultiLayer && layerIdx >= 1 ? `L${layerIdx + 1}` : undefined;

  // 任意 Layer の velocityMin/Max を更新 (Slider の各行から呼ばれる)
  const onChangeRange = useCallback(
    (targetLayerIndex: number, next: { min: number; max: number }) => {
      onChange(patchLayerVelocityRange(pad, targetLayerIndex, next));
    },
    [onChange, pad],
  );

  // Layer 切替 (行ラベル/トラッククリック)
  const onSelectLayer = useCallback(
    (targetLayerIndex: number) => {
      if (targetLayerIndex === layerIdx) return;
      onChange({ selectedLayerIndex: targetLayerIndex });
    },
    [onChange, layerIdx],
  );

  const onAutoSplit = useCallback(
    (splits: number) => {
      onChange(patchAutoSplitVelocity(pad, splits));
    },
    [onChange, pad],
  );

  return (
    <div
      className={styles.sections}
      // L2 以降を編集中はノブ/スライダーを gold アクセントに切り替える。
      // MAIN(layer 0) と 1Layer 状態では従来通り cyan のまま。
      data-accent={layerIdx >= 1 ? 'gold' : undefined}
    >
      <section className={`${styles.section} ${styles.trim}`}>
        <SectionTitle title="SAMPLE TRIM" layerLabel={layerBadge} />
        <div className={styles.knobGrid4}>
          <Knob ownerKey={ownerKey} size={46} label="START" value={trim.startMs / totalMs} defaultValue={0}
            valueText={formatTrimPercent(trim.startMs, totalMs)}
            parseInput={text => {
              const parsed = text.includes('%') ? parsePercentInput(text) : parseNumericText(text);
              return parsed === null ? null : (text.includes('%') ? parsed : parsed / Math.max(1, totalMs));
            }}
            onChange={v => onChange({ startMs: v * totalMs })}
            onReset={() => onChange({ startMs: defaultPadParam('startMs') })} />
          <Knob ownerKey={ownerKey} size={46} label="END" value={trim.endMs / totalMs} defaultValue={1}
            valueText={formatTrimPercent(trim.endMs, totalMs)}
            parseInput={text => {
              const parsed = text.includes('%') ? parsePercentInput(text) : parseNumericText(text);
              return parsed === null ? null : (text.includes('%') ? parsed : parsed / Math.max(1, totalMs));
            }}
            onChange={v => onChange({ endMs: v * totalMs })}
            onReset={() => onChange({ endMs: trim.sampleLengthMs })} />
          <Knob ownerKey={ownerKey} size={46} label="FADE IN" value={playbackRangeMs > 0 ? trim.fadeInMs / playbackRangeMs : 0} defaultValue={0}
            valueText={formatMs(trim.fadeInMs)}
            parseInput={text => {
              const parsed = parseNumericText(text);
              return parsed === null ? null : parsed / playbackRangeMs;
            }}
            onChange={v => onChange({ fadeInMs: v * playbackRangeMs })}
            onReset={() => onChange({ fadeInMs: defaultPadParam('fadeInMs') })} />
          <Knob ownerKey={ownerKey} size={46} label="FADE OUT" value={playbackRangeMs > 0 ? trim.fadeOutMs / playbackRangeMs : 0} defaultValue={0}
            valueText={formatMs(trim.fadeOutMs)}
            parseInput={text => {
              const parsed = parseNumericText(text);
              return parsed === null ? null : parsed / playbackRangeMs;
            }}
            onChange={v => onChange({ fadeOutMs: v * playbackRangeMs })}
            onReset={() => onChange({ fadeOutMs: defaultPadParam('fadeOutMs') })} />
        </div>
      </section>

      <section className={`${styles.section} ${styles.envelope}`}>
        <SectionTitle title="DYNAMICS" layerLabel={layerBadge} />
        <div className={styles.knobGrid2x2}>
          <Knob ownerKey={ownerKey} size={40} label="ATTACK" value={pad.attack / 2.0} defaultValue={defaultPadParam('attack') / 2.0} valueText={formatMs(pad.attack * 1000)}
            parseInput={text => {
              const parsed = parseNumericText(text);
              return parsed === null ? null : (parsed / 1000) / 2.0;
            }}
            onChange={v => onChange({ attack: v * 2.0 })} />
          <Knob ownerKey={ownerKey} size={40} label="RELEASE" value={pad.release / 4.0} defaultValue={defaultPadParam('release') / 4.0} valueText={formatMs(pad.release * 1000, 0)}
            parseInput={text => {
              const parsed = parseNumericText(text);
              return parsed === null ? null : (parsed / 1000) / 4.0;
            }}
            onChange={v => onChange({ release: v * 4.0 })} />
          <Knob ownerKey={ownerKey} size={40} label="VELOCITY" value={pad.velocitySens} defaultValue={defaultPadParam('velocitySens')} valueText={`${Math.round(pad.velocitySens * 100)} %`}
            parseInput={parsePercentInput}
            onChange={v => onChange({ velocitySens: v })} />
          <Knob ownerKey={ownerKey} size={40} label="HUMANIZE" value={pad.humanize} defaultValue={defaultPadParam('humanize')} valueText={`${Math.round(pad.humanize * 100)} %`}
            parseInput={parsePercentInput}
            onChange={v => onChange({ humanize: v })} />
        </div>
      </section>

      <section className={`${styles.section} ${styles.mixer}`}>
        <SectionTitle title="MIXER" layerLabel={layerBadge} />
        <div className={styles.mixerContent}>
          <Knob ownerKey={ownerKey} size={52} label="VOLUME" value={pad.volume} defaultValue={defaultPadParam('volume')} valueText={formatVolume(pad.volume)}
            parseInput={text => {
              const parsed = parseNumericText(text);
              return parsed === null ? null : dbToPosition(parsed);
            }}
            onChange={v => onChange({ volume: v })} />
          <Knob ownerKey={ownerKey} size={46} label="PAN" value={pad.pan} min={-1} max={1} defaultValue={defaultPadParam('pan')} bipolar
            valueText={formatPan(pad.pan)}
            parseInput={parsePanInput}
            onChange={v => onChange({ pan: v })} />
          <Knob ownerKey={ownerKey} size={46} label="PITCH" value={pad.pitch} min={-24} max={24} defaultValue={defaultPadParam('pitch')} bipolar
            valueText={formatPitch(pad.pitch)}
            parseInput={parseNumericText}
            onChange={v => onChange({ pitch: v })} />
          {layerIdx === 0 && (
            <div className={styles.outputAssign}>
              <span>OUTPUT</span>
              <OutputAssignDropdown
                width={72}
                value={pad.outputAssign}
                onChange={v => onChange({ outputAssign: v })}
              />
            </div>
          )}
        </div>
      </section>

      <section className={`${styles.section} ${styles.playback}`}>
        <SectionTitle title="PLAYBACK / TRIGGER" />
        <div className={styles.playContent}>
          <div className={styles.modeBlock}>
            <span className={styles.fieldLabel}>MODE</span>
            <div className={styles.modeButtons}>
              <button
                className={`${styles.bigButton} ${pad.playMode === 'OneShot' ? styles.activeBlue : ''}`}
                onClick={() => onChange({ playMode: 'OneShot' as PlayMode })}
              >
                ONE SHOT
              </button>
              <button
                className={`${styles.bigButton} ${pad.playMode === 'Gate' ? styles.activeBlue : ''}`}
                onClick={() => onChange({ playMode: 'Gate' as PlayMode })}
              >
                GATE
              </button>
            </div>
          </div>

          <div className={styles.chokeBlock}>
            <span className={styles.fieldLabel}>CHOKE GROUP</span>
            <div className={styles.chokeButtons}>
              {[0, 1, 2, 3, 4].map(v => (
                <button
                  key={v}
                  className={`${styles.chokeButton} ${pad.chokeGroup === v ? styles.activeBlue : ''}`}
                  onClick={() => onChange({ chokeGroup: v })}
                >
                  {v === 0 ? 'OFF' : v}
                </button>
              ))}
            </div>
          </div>

          {isMultiLayer && (
            <div className={styles.velocityRangeBlock}>
              <VelocityRangeSlider
                padIndex={padIndex}
                layers={layers}
                activeLayerIndex={layerIdx}
                onSelectLayer={onSelectLayer}
                onChangeRange={onChangeRange}
                onAutoSplit={onAutoSplit}
              />
            </div>
          )}
        </div>
      </section>
    </div>
  );
}

// memo: SectionTitle below is trivial, but the PadControlSections root tree
// contains ~13 Knobs + many buttons. Skipping re-render when pad/onChange
// haven't changed is a major win when App re-renders for level meters.
export const PadControlSections = memo(PadControlSectionsComponent);

function SectionTitle({ title, layerLabel }: { title: string; layerLabel?: string }) {
  return (
    <h3 className={styles.title}>
      <span aria-hidden />
      {layerLabel && <em className={styles.layerLabel}>{layerLabel}</em>}
      {title}
    </h3>
  );
}
