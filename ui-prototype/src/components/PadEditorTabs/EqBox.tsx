import { useState } from 'react';
import styles from './EqBox.module.css';
import { Knob } from '../Knob/Knob';
import { EqCurve } from './EqCurve';
import type { PointId } from './EqCurve';
import type { EqParams } from '../../types';
import { parseNumericText } from '../../utils/numericInput';
import { layerAutomationTarget, type AutomationTarget } from '../../utils/automationTarget';

interface Props {
  eq: EqParams;
  patchEq: (updater: (eq: EqParams) => EqParams) => void;
  onRemove?: () => void;
  padIndex?: number;
  layerIndex?: number;
}

const BANDS: Array<{ id: PointId; label: string; className: string }> = [
  { id: 'low', label: 'LOW', className: 'bandLow' },
  { id: 'lowMid', label: 'LOW MID', className: 'bandLowMid' },
  { id: 'highMid', label: 'HIGH MID', className: 'bandHighMid' },
  { id: 'high', label: 'HIGH', className: 'bandHigh' },
];

function fmtFreq(hz: number): string {
  if (hz >= 1000) return `${(hz / 1000).toFixed(hz >= 10000 ? 0 : 2)}k`;
  return `${Math.round(hz)}`;
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

function qToNorm(q: number): number {
  return (Math.max(0.2, Math.min(8, q)) - 0.2) / (8 - 0.2);
}

function normToQ(n: number): number {
  return 0.2 + Math.max(0, Math.min(1, n)) * (8 - 0.2);
}

export function EqBox({ eq, patchEq, onRemove, padIndex, layerIndex }: Props) {
  const [activeBand, setActiveBand] = useState<PointId>('lowMid');
  const band = eq[activeBand];
  const edgeModeKey = activeBand === 'low' ? 'lowMode' : activeBand === 'high' ? 'highMode' : null;
  const isCutMode = edgeModeKey ? eq[edgeModeKey] === 'cut' : false;
  const target = (suffix: string, name: string): AutomationTarget | undefined =>
    padIndex !== undefined && layerIndex !== undefined
      ? layerAutomationTarget(padIndex, layerIndex, suffix, name)
      : undefined;
  const bandPrefix = activeBand === 'low' ? 'eqLow'
    : activeBand === 'lowMid' ? 'eqLowMid'
    : activeBand === 'highMid' ? 'eqHighMid'
    : 'eqHigh';

  const onCurveChange = (patch: Partial<EqParams>) => {
    patchEq(current => ({
      ...current,
      ...patch,
    }));
  };

  return (
    <div className={`${styles.box} ${eq.bypassed ? styles.bypassed : ''}`}>
      <div className={styles.header}>
        <span className={styles.title}>4 BAND EQ</span>
        <div className={styles.headerActions}>
          <button
            type="button"
            className={`${styles.iconBtn} ${eq.bypassed ? styles.powerOff : styles.powerOn}`}
            onClick={() => patchEq(cur => ({ ...cur, bypassed: !cur.bypassed }))}
            aria-pressed={!eq.bypassed}
            title={eq.bypassed ? 'Bypassed - click to activate' : 'Active - click to bypass'}
            data-automation-target-id={target('eqBypass', 'EQ Bypass')?.id}
            data-automation-target-name={target('eqBypass', 'EQ Bypass')?.name}
          >
            <PowerIcon />
          </button>
          {onRemove && (
            <button
              type="button"
              className={`${styles.iconBtn} ${styles.removeBtn}`}
              onClick={onRemove}
              title="Remove EQ"
              aria-label="Remove EQ"
            >
              X
            </button>
          )}
        </div>
      </div>

      <EqCurve
        eq={eq}
        bypassed={eq.bypassed}
        activePoint={activeBand}
        onChange={onCurveChange}
        onActivate={(p) => p && setActiveBand(p)}
      />

      <div className={styles.bandTabs} role="tablist">
        {BANDS.map(b => (
          <button
            key={b.id}
            role="tab"
            aria-selected={activeBand === b.id}
            className={`${styles.bandTab} ${styles[b.className]} ${activeBand === b.id ? styles.bandTabActive : ''}`}
            onClick={() => setActiveBand(b.id)}
          >
            {b.label}
          </button>
        ))}
      </div>

      <div className={`${styles.knobsRow} ${edgeModeKey ? styles.knobsRowWithMode : ''}`}>
        <Knob
          size={32}
          label="FREQ"
          value={freqToNorm(band.freq)}
          defaultValue={freqToNorm(activeBand === 'low' ? 120 : activeBand === 'lowMid' ? 450 : activeBand === 'highMid' ? 2400 : 10000)}
          valueText={fmtFreq(band.freq)}
          parseInput={text => {
            const v = parseNumericText(text);
            return v == null ? null : freqToNorm(v);
          }}
          onChange={v => patchEq(cur => ({ ...cur, [activeBand]: { ...cur[activeBand], freq: normToFreq(v) } }))}
          automationTarget={target(`${bandPrefix}Freq`, `${BANDS.find(item => item.id === activeBand)?.label} Frequency`)}
        />
        <Knob
          size={32}
          label="GAIN"
          bipolar
          value={band.gain}
          min={-18}
          max={18}
          defaultValue={0}
          valueText={`${band.gain >= 0 ? '+' : ''}${band.gain.toFixed(1)}`}
          parseInput={parseNumericText}
          onChange={v => patchEq(cur => ({ ...cur, [activeBand]: { ...cur[activeBand], gain: v } }))}
          automationTarget={target(`${bandPrefix}Gain`, `${BANDS.find(item => item.id === activeBand)?.label} Gain`)}
        />
        <Knob
          size={32}
          label="Q"
          value={qToNorm(band.q)}
          defaultValue={qToNorm(activeBand === 'low' || activeBand === 'high' ? 0.7 : 1.0)}
          valueText={band.q.toFixed(2)}
          parseInput={text => {
            const v = parseNumericText(text);
            return v == null ? null : qToNorm(v);
          }}
          onChange={v => patchEq(cur => ({ ...cur, [activeBand]: { ...cur[activeBand], q: normToQ(v) } }))}
          automationTarget={target(`${bandPrefix}Q`, `${BANDS.find(item => item.id === activeBand)?.label} Q`)}
        />
        {edgeModeKey && (
          <ModeSwitch
            value={eq[edgeModeKey]}
            onChange={mode => patchEq(cur => ({ ...cur, [edgeModeKey]: mode }))}
            automationTarget={target(activeBand === 'low' ? 'eqLowMode' : 'eqHighMode', activeBand === 'low' ? 'EQ Low Cut' : 'EQ High Cut')}
          />
        )}
      </div>
      {isCutMode && (
        <div className={styles.modeHint}>
          {activeBand === 'low' ? 'LOW CUT' : 'HIGH CUT'}
        </div>
      )}
    </div>
  );
}

function ModeSwitch({ value, onChange, automationTarget }: {
  value: 'shelf' | 'cut';
  onChange: (value: 'shelf' | 'cut') => void;
  automationTarget?: AutomationTarget;
}) {
  return (
    <div className={styles.modeWrap}>
      <span className={styles.miniLabel}>MODE</span>
      <div className={styles.modeGrid}>
        <button
          type="button"
          className={`${styles.modeBtn} ${value === 'shelf' ? styles.modeBtnActive : ''}`}
          onClick={() => onChange('shelf')}
          data-automation-target-id={automationTarget?.id}
          data-automation-target-name={automationTarget?.name}
        >
          SHELF
        </button>
        <button
          type="button"
          className={`${styles.modeBtn} ${value === 'cut' ? styles.modeBtnActive : ''}`}
          onClick={() => onChange('cut')}
          data-automation-target-id={automationTarget?.id}
          data-automation-target-name={automationTarget?.name}
        >
          CUT
        </button>
      </div>
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
