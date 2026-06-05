import { useEffect, useState } from 'react';
import styles from './PadSettingsPanel.module.css';
import { Dropdown } from '../Dropdown/Dropdown';
import { OutputAssignDropdown } from '../OutputAssignDropdown/OutputAssignDropdown';
import { ToggleSwitch } from '../ToggleSwitch/ToggleSwitch';
import { midiNoteName } from '../../data/padData';
import type { PadParams, PlayMode } from '../../types';

interface PadSettingsPanelProps {
  pad: PadParams;
  padIndex: number;
  onChange: (patch: Partial<PadParams>) => void;
}

export function PadSettingsPanel({ pad, padIndex, onChange }: PadSettingsPanelProps) {
  const [collapsed, setCollapsed] = useState(false);
  const [nameDraft, setNameDraft] = useState(pad.padName);

  useEffect(() => {
    setNameDraft(pad.padName);
  }, [pad.padName]);

  const commitNameDraft = () => {
    const nextName = nameDraft.trim();
    if (nextName && nextName !== pad.padName) onChange({ padName: nextName });
    else setNameDraft(pad.padName);
  };

  return (
    <div className={styles.panel}>
      {/* ── 見出し ───────────────────────────────────────────────────── */}
      <div className={styles.header}>
        <div className={styles.heading}>
          <span className={styles.padNum}>P{String(padIndex + 1).padStart(2, '0')}</span>
          <span className={styles.title}>PAD SETTINGS</span>
        </div>
        <button
          className={styles.collapseBtn}
          onClick={() => setCollapsed(!collapsed)}
          aria-label="collapse"
        >
          <svg width="10" height="10" viewBox="0 0 10 10"
            style={{ transform: collapsed ? 'rotate(180deg)' : 'none', transition: 'transform 200ms' }}
          >
            <path d="M2 6 L5 3 L8 6" stroke="currentColor" strokeWidth="1.4" fill="none" />
          </svg>
        </button>
      </div>

      {!collapsed && (
        <div className={styles.body}>
          {/* MIDI Note */}
          <Field label="MIDI NOTE">
            <input
              className={styles.input}
              value={midiNoteName(pad.midiNote)}
              readOnly
            />
          </Field>

          {/* Pad Name */}
          <Field label="PAD NAME">
            <input
              className={styles.input}
              value={nameDraft}
              onChange={e => setNameDraft(e.target.value)}
              onBlur={commitNameDraft}
              onKeyDown={e => {
                if (e.key === 'Enter') commitNameDraft();
                if (e.key === 'Escape') setNameDraft(pad.padName);
              }}
            />
          </Field>

          {/* Sample File */}
          <Field label="SAMPLE FILE">
            <div className={styles.fileRow}>
              <input
                className={`${styles.input} ${styles.fileInput}`}
                value={pad.sampleFileName}
                readOnly
                placeholder="—"
              />
              <button className={styles.folderBtn} aria-label="browse">
                <svg width="13" height="13" viewBox="0 0 14 14">
                  <path
                    d="M1.5 3.5 L1.5 11 A0.5 0.5 0 0 0 2 11.5 L12 11.5 A0.5 0.5 0 0 0 12.5 11 L12.5 5 A0.5 0.5 0 0 0 12 4.5 L6.5 4.5 L5 3 L2 3 A0.5 0.5 0 0 0 1.5 3.5 Z"
                    stroke="currentColor" strokeWidth="1" fill="none" strokeLinejoin="round"
                  />
                </svg>
              </button>
            </div>
          </Field>

          {/* Output */}
          <Field label="OUTPUT">
            <OutputAssignDropdown
              value={pad.outputAssign}
              onChange={v => onChange({ outputAssign: v })}
            />
          </Field>

          {/* Play Mode */}
          <Field label="PLAY MODE">
            <Dropdown<PlayMode>
              value={pad.playMode}
              onChange={v => onChange({ playMode: v })}
              options={[
                { value: 'OneShot' as PlayMode, label: 'One Shot' },
                { value: 'Gate'    as PlayMode, label: 'Gate' },
              ]}
            />
          </Field>

          {/* Choke Group */}
          <Field label="CHOKE GROUP">
            <Dropdown
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
          </Field>

          <div className={styles.sep} />

          {/* Reverse */}
          <div className={styles.toggleRow}>
            <span className={styles.toggleLabel}>REVERSE</span>
            <ToggleSwitch
              on={pad.reverse}
              onChange={v => onChange({ reverse: v })}
              size="sm"
            />
          </div>

          {/* Smart Trim */}
          <div className={styles.toggleRow}>
            <span className={styles.toggleLabel}>SMART TRIM</span>
            <ToggleSwitch
              on={pad.smartTrim}
              onChange={v => onChange({ smartTrim: v })}
              size="sm"
            />
          </div>
        </div>
      )}
    </div>
  );
}

/* ── 1 行のラベル + 入力欄 ─────────────────────────────────────────── */
function Field({ label, children }: { label: string; children: React.ReactNode }) {
  return (
    <div className={styles.field}>
      <div className={styles.fieldLabel}>{label}</div>
      {children}
    </div>
  );
}
