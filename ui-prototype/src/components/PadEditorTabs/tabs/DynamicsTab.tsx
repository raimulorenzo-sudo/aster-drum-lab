import styles from './DynamicsTab.module.css';
import { Knob } from '../../Knob/Knob';
import { VelCurveEditor } from '../../VelCurveEditor/VelCurveEditor';
import { defaultPadParam } from '../../../data/parameterSpecs';
import { formatMs } from '../../../utils/parameterFormat';
import { parseNumericText, parsePercentInput } from '../../../utils/numericInput';
import type { PadParams, VelCurveState } from '../../../types';

interface Props {
  pad: PadParams;
  padIndex: number;
  onChange: (patch: Partial<PadParams>) => void;
}

export function DynamicsTab({ pad, padIndex, onChange }: Props) {
  const ownerKey = padIndex;
  const onChangeCurve = (next: VelCurveState) => onChange({ velCurve: next });

  return (
    <div className={styles.root}>
      <div className={styles.section}>
        <div className={styles.sectionLabel}>LAYER ENVELOPE</div>
        <div className={styles.knobCluster}>
          <Knob ownerKey={ownerKey} size={60} label="ATTACK"
            value={pad.attack / 2.0}
            defaultValue={defaultPadParam('attack') / 2.0}
            valueText={formatMs(pad.attack * 1000)}
            parseInput={text => {
              const parsed = parseNumericText(text);
              return parsed === null ? null : (parsed / 1000) / 2.0;
            }}
            onChange={v => onChange({ attack: v * 2.0 })} />
          <Knob ownerKey={ownerKey} size={60} label="RELEASE"
            value={pad.release / 4.0}
            defaultValue={defaultPadParam('release') / 4.0}
            valueText={formatMs(pad.release * 1000, 0)}
            parseInput={text => {
              const parsed = parseNumericText(text);
              return parsed === null ? null : (parsed / 1000) / 4.0;
            }}
            onChange={v => onChange({ release: v * 4.0 })} />
        </div>
      </div>

      <div className={`${styles.section} ${styles.padSection}`}>
        <div className={styles.sectionLabel}>PAD RESPONSE</div>
        <div className={styles.velGroup}>
          <Knob ownerKey={ownerKey} size={60} label="VELOCITY"
            value={pad.velocitySens} defaultValue={defaultPadParam('velocitySens')}
            valueText={`${Math.round(pad.velocitySens * 100)} %`}
            parseInput={parsePercentInput}
            onChange={v => onChange({ velocitySens: v })} />
          <Knob ownerKey={ownerKey} size={60} label="HUMANIZE"
            value={pad.humanize} defaultValue={defaultPadParam('humanize')}
            valueText={`${Math.round(pad.humanize * 100)} %`}
            parseInput={parsePercentInput}
            onChange={v => onChange({ humanize: v })} />
          <div className={styles.curveSlot}>
            <VelCurveEditor curve={pad.velCurve} onChange={onChangeCurve} />
          </div>
        </div>
      </div>
    </div>
  );
}
