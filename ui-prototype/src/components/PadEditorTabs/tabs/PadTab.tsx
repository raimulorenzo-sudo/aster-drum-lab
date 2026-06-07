import shared from '../../PadControlSections/PadControlSections.module.css';
import styles from './PadTab.module.css';
import { Knob } from '../../Knob/Knob';
import { OutputAssignDropdown } from '../../OutputAssignDropdown/OutputAssignDropdown';
import { defaultPadParam } from '../../../data/parameterSpecs';
import { formatPan, formatPitch, formatVolume } from '../../../utils/parameterFormat';
import { dbToPosition } from '../../../utils/fader';
import { parseNumericText, parsePanInput } from '../../../utils/numericInput';
import type { PadParams, PlayMode } from '../../../types';

interface Props {
  pad: PadParams;
  padIndex: number;
  onChange: (patch: Partial<PadParams>) => void;
}

const PLAY_MODES: { id: PlayMode; label: string }[] = [
  { id: 'OneShot', label: 'ONE SHOT' },
  { id: 'Gate', label: 'GATE' },
];

export function PadTab({ pad, padIndex, onChange }: Props) {
  const polyphony = pad.polyphony ?? 0;
  const voiceSteal = pad.voiceSteal ?? 'oldest';

  return (
    <div className={styles.root}>
      <div className={styles.mixRow}>
        <Knob ownerKey={padIndex} size={60} label="PAD VOL"
          value={pad.padVolume ?? 0.75} defaultValue={defaultPadParam('volume')}
          valueText={formatVolume(pad.padVolume ?? 0.75)}
          parseInput={text => { const v = parseNumericText(text); return v == null ? null : dbToPosition(v); }}
          onChange={v => onChange({ padVolume: v })} />
        <Knob ownerKey={padIndex} size={60} label="PAD PAN" bipolar
          value={pad.padPan ?? 0} min={-1} max={1} defaultValue={defaultPadParam('pan')}
          valueText={formatPan(pad.padPan ?? 0)}
          parseInput={parsePanInput}
          onChange={v => onChange({ padPan: v })} />
        <Knob ownerKey={padIndex} size={60} label="PAD PITCH" bipolar
          value={pad.padPitch ?? 0} min={-24} max={24} defaultValue={defaultPadParam('pitch')}
          valueText={formatPitch(pad.padPitch ?? 0)}
          parseInput={parseNumericText}
          onChange={v => onChange({ padPitch: v })} />
        <div className={styles.outputSlot}>
          <span className={styles.slotLabel}>OUTPUT</span>
          <OutputAssignDropdown
            width={110}
            value={pad.outputAssign}
            onChange={v => onChange({ outputAssign: v })}
          />
        </div>
      </div>

      <div className={styles.divider} />

      <div className={styles.settings}>
        <div className={styles.settingRow}>
          <span className={`${shared.fieldLabel} ${styles.inlineLabel}`}>MODE</span>
          <div className={styles.segGroup}>
            {PLAY_MODES.map(mode => (
              <button key={mode.id}
                className={`${styles.segBtn} ${pad.playMode === mode.id ? styles.segBtnActive : ''}`}
                onClick={() => onChange({ playMode: mode.id })}
              >
                {mode.label}
              </button>
            ))}
          </div>
        </div>

        <div className={styles.settingRow}>
          <span className={`${shared.fieldLabel} ${styles.inlineLabel}`}>CHOKE</span>
          <div className={styles.segGroup}>
            {[0, 1, 2, 3, 4].map(value => (
              <button key={value}
                className={`${styles.segBtn} ${styles.segBtnNarrow} ${pad.chokeGroup === value ? styles.segBtnActive : ''}`}
                onClick={() => onChange({ chokeGroup: value })}
              >
                {value === 0 ? 'OFF' : value}
              </button>
            ))}
          </div>
        </div>

        <div className={styles.settingRow}>
          <span className={`${shared.fieldLabel} ${styles.inlineLabel}`}>VOICES</span>
          <div className={styles.segGroup}>
            {[1, 2, 4, 8, 16, 0].map(value => (
              <button key={value}
                className={`${styles.segBtn} ${styles.segBtnNarrow} ${polyphony === value ? styles.segBtnActive : ''}`}
                onClick={() => onChange({ polyphony: value })}
              >
                {value === 0 ? '∞' : value}
              </button>
            ))}
          </div>
          <span className={styles.stealLabel}>STEAL</span>
          <div className={styles.segGroup}>
            {[
              { value: 'oldest' as const, label: 'OLD' },
              { value: 'quietest' as const, label: 'QUIET' },
              { value: 'off' as const, label: 'OFF' },
            ].map(option => (
              <button key={option.value}
                className={`${styles.segBtn} ${styles.segBtnNarrow} ${voiceSteal === option.value ? styles.segBtnActive : ''} ${polyphony === 0 ? styles.segBtnDimmed : ''}`}
                onClick={() => { if (polyphony !== 0) onChange({ voiceSteal: option.value }); }}
                disabled={polyphony === 0}
              >
                {option.label}
              </button>
            ))}
          </div>
        </div>
      </div>
    </div>
  );
}
