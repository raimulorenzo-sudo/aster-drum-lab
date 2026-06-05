import styles from './BottomControlPanel.module.css';
import { Knob } from '../Knob/Knob';
import { ToggleSwitch } from '../ToggleSwitch/ToggleSwitch';
import { Dropdown } from '../Dropdown/Dropdown';
import { OutputAssignDropdown } from '../OutputAssignDropdown/OutputAssignDropdown';
import { IconButton } from '../IconButton/IconButton';
import type { PadParams, PlayMode } from '../../types';

interface BottomControlPanelProps {
  pad: PadParams;
  onChange: (patch: Partial<PadParams>) => void;
}

export function BottomControlPanel({ pad, onChange }: BottomControlPanelProps) {
  return (
    <div className={styles.panel}>
      {/* ── PLAY MODE ──────────────────────────────────────────────── */}
      <div className={styles.cell}>
        <div className={styles.label}>PLAY MODE</div>
        <div className={styles.modeButtons}>
          <IconButton
            active={pad.playMode === 'OneShot'}
            onClick={() => onChange({ playMode: 'OneShot' as PlayMode })}
          >
            ONE SHOT
          </IconButton>
          <IconButton
            active={pad.playMode === 'Gate'}
            onClick={() => onChange({ playMode: 'Gate' as PlayMode })}
          >
            GATE
          </IconButton>
        </div>
      </div>

      <div className={styles.divider} />

      {/* ── CHOKE GROUP ────────────────────────────────────────────── */}
      <div className={styles.cell}>
        <div className={styles.label}>CHOKE GROUP</div>
        <Dropdown
          width={80}
          value={pad.chokeGroup}
          onChange={v => onChange({ chokeGroup: v })}
          options={[
            { value: 0, label: 'Off' },
            { value: 1, label: '1' },
            { value: 2, label: '2' },
            { value: 3, label: '3' },
            { value: 4, label: '4' },
          ]}
        />
      </div>

      <div className={styles.divider} />

      {/* ── VELOCITY SENS / HUMANIZE ───────────────────────────────── */}
      <div className={styles.cellKnobs}>
        <Knob
          size={48}
          label="VELOCITY"
          value={pad.velocitySens}
          valueText={`${Math.round(pad.velocitySens * 100)} %`}
          onChange={v => onChange({ velocitySens: v })}
        />
        <Knob
          size={48}
          label="HUMANIZE"
          value={pad.humanize}
          valueText={`${Math.round(pad.humanize * 100)} %`}
          onChange={v => onChange({ humanize: v })}
        />
      </div>

      <div className={styles.divider} />

      {/* ── OUTPUT ASSIGN ──────────────────────────────────────────── */}
      <div className={styles.cell}>
        <div className={styles.label}>OUTPUT</div>
        <OutputAssignDropdown
          width={110}
          value={pad.outputAssign}
          onChange={v => onChange({ outputAssign: v })}
        />
      </div>

      <div className={styles.divider} />

      {/* ── REVERSE ────────────────────────────────────────────────── */}
      <div className={styles.cell}>
        <div className={styles.label}>REVERSE</div>
        <ToggleSwitch
          on={pad.reverse}
          onChange={v => onChange({ reverse: v })}
          size="sm"
        />
      </div>

      <div className={styles.divider} />

      {/* ── MUTE / SOLO ───────────────────────────────────────────── */}
      <div className={styles.cell}>
        <div className={styles.label}>&nbsp;</div>
        <div className={styles.muteSoloRow}>
          <button
            className={`${styles.ms} ${pad.mute ? styles.muteOn : ''}`}
            onClick={() => onChange({ mute: !pad.mute })}
            title="Mute"
          >M</button>
          <button
            className={`${styles.ms} ${pad.solo ? styles.soloOn : ''}`}
            onClick={() => onChange({ solo: !pad.solo })}
            title="Solo"
          >S</button>
        </div>
      </div>
    </div>
  );
}
