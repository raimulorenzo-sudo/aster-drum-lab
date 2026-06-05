import { memo, useCallback, useMemo, useRef, useState } from 'react';
import styles from './FilterCurve.module.css';
import type { FilterParams } from '../../types';
import { dbToY, freqToX, xToFreq } from '../../utils/eqMath';

interface Props {
  params: FilterParams;
  bypassed: boolean;
  onChange: (patch: Partial<FilterParams>) => void;
}

const WIDTH = 184;
const HEIGHT = 56;
const VISUAL_RANGE_DB = 18;
const LINE_FLOOR_DB = -17.2;

// 欠損/NaN/旧フォーマットを安全な既定値に丸める。
function sanitize(p: FilterParams): FilterParams {
  const slope = (v: unknown, d: 12 | 24 | 48): 12 | 24 | 48 => v === 12 ? 12 : v === 24 ? 24 : v === 48 ? 48 : d;
  const num = (v: unknown, d: number) => Number.isFinite(v as number) ? (v as number) : d;
  return {
    hpEnabled: Boolean(p.hpEnabled),
    hpCutoff: num(p.hpCutoff, 80),
    hpSlope: slope(p.hpSlope, 12),
    hpResonance: num(p.hpResonance, 0.7),
    lpEnabled: Boolean(p.lpEnabled),
    lpCutoff: num(p.lpCutoff, 18000),
    lpSlope: slope(p.lpSlope, 12),
    lpResonance: num(p.lpResonance, 0.7),
  };
}

// HP / LP を合成したフィルター応答 (dB)。slope は dB/oct、resonance はコーナー強調。
function responseDb(p: FilterParams, freq: number): number {
  let db = 0;
  if (p.hpEnabled) {
    const cutoff = Math.max(20, Math.min(20000, p.hpCutoff));
    const ratio = Math.max(1e-6, freq / cutoff);
    db += Math.min(0, Math.log2(ratio) * p.hpSlope);
    const d = Math.abs(Math.log2(ratio));
    db += Math.max(0, Math.min(8, p.hpResonance) - 0.7) * 2.0 * Math.exp(-d * d * 10);
  }
  if (p.lpEnabled) {
    const cutoff = Math.max(20, Math.min(20000, p.lpCutoff));
    const ratio = Math.max(1e-6, freq / cutoff);
    db += Math.min(0, -Math.log2(ratio) * p.lpSlope);
    const d = Math.abs(Math.log2(ratio));
    db += Math.max(0, Math.min(8, p.lpResonance) - 0.7) * 2.0 * Math.exp(-d * d * 10);
  }
  return Math.max(-96, Math.min(18, db));
}

function FilterCurveComponent({ params: rawParams, bypassed, onChange }: Props) {
  const params = sanitize(rawParams);
  const svgRef = useRef<SVGSVGElement | null>(null);
  const [dragging, setDragging] = useState(false);

  const { linePath, fillPath } = useMemo(() => {
    const samples = Array.from({ length: 128 }, (_, i) => {
      const x = i / 127;
      return { x, db: responseDb(params, xToFreq(x)) };
    });

    const fullLine = samples
      .map((p, i) =>
        `${i === 0 ? 'M' : 'L'} ${(p.x * WIDTH).toFixed(1)} ${(dbToY(p.db, VISUAL_RANGE_DB) * HEIGHT).toFixed(1)}`,
      )
      .join(' ');

    let previousWasVisible = false;
    const visibleLine = samples
      .map(p => {
        if (p.db <= LINE_FLOOR_DB) {
          previousWasVisible = false;
          return '';
        }
        const command = previousWasVisible ? 'L' : 'M';
        previousWasVisible = true;
        return `${command} ${(p.x * WIDTH).toFixed(1)} ${(dbToY(p.db, VISUAL_RANGE_DB) * HEIGHT).toFixed(1)}`;
      })
      .filter(Boolean)
      .join(' ');

    const fill = `${fullLine} L ${WIDTH} ${HEIGHT} L 0 ${HEIGHT} Z`;
    return { linePath: visibleLine, fillPath: fill };
  }, [params]);

  const handles = useMemo(() => {
    const list: { key: 'hp' | 'lp'; x: number; y: number }[] = [];
    if (params.hpEnabled) list.push({ key: 'hp', x: freqToX(params.hpCutoff) * WIDTH, y: dbToY(responseDb(params, params.hpCutoff), VISUAL_RANGE_DB) * HEIGHT });
    if (params.lpEnabled) list.push({ key: 'lp', x: freqToX(params.lpCutoff) * WIDTH, y: dbToY(responseDb(params, params.lpCutoff), VISUAL_RANGE_DB) * HEIGHT });
    return list;
  }, [params]);

  const localCoord = useCallback((clientX: number, clientY: number) => {
    const el = svgRef.current;
    if (!el) return { x: 0, y: 0 };
    const rect = el.getBoundingClientRect();
    return {
      x: Math.max(0, Math.min(1, (clientX - rect.left) / rect.width)),
      y: Math.max(0, Math.min(1, (clientY - rect.top) / rect.height)),
    };
  }, []);

  const applyPointer = useCallback((event: React.PointerEvent<SVGSVGElement>) => {
    const { x, y } = localCoord(event.clientX, event.clientY);
    const freq = Math.max(20, Math.min(20000, xToFreq(x)));
    const resonance = Math.max(0.2, Math.min(8, 0.2 + (1 - y) * 7.8));
    // ドラッグ位置に近い有効コーナーを動かす。Q もそのセクションのみ更新。
    if (params.hpEnabled && params.lpEnabled) {
      const dHp = Math.abs(freqToX(params.hpCutoff) - x);
      const dLp = Math.abs(freqToX(params.lpCutoff) - x);
      if (dHp <= dLp) onChange({ hpCutoff: freq, hpResonance: resonance });
      else onChange({ lpCutoff: freq, lpResonance: resonance });
    } else if (params.hpEnabled) {
      onChange({ hpCutoff: freq, hpResonance: resonance });
    } else if (params.lpEnabled) {
      onChange({ lpCutoff: freq, lpResonance: resonance });
    }
  }, [localCoord, onChange, params]);

  return (
    <div className={`${styles.wrap} ${bypassed ? styles.bypassed : ''}`}>
      <svg
        ref={svgRef}
        className={styles.svg}
        viewBox={`0 0 ${WIDTH} ${HEIGHT}`}
        preserveAspectRatio="none"
        onPointerDown={event => {
          event.preventDefault();
          event.currentTarget.setPointerCapture(event.pointerId);
          setDragging(true);
          applyPointer(event);
        }}
        onPointerMove={event => { if (dragging) applyPointer(event); }}
        onPointerUp={() => setDragging(false)}
      >
        <defs>
          <linearGradient id="filterFill" x1="0" y1="0" x2="0" y2="1">
            <stop offset="0%" stopColor="rgba(82,200,232,0.22)" />
            <stop offset="100%" stopColor="rgba(82,200,232,0.01)" />
          </linearGradient>
        </defs>
        {[20, 100, 1000, 10000, 20000].map(freq => (
          <line key={freq} x1={freqToX(freq) * WIDTH} x2={freqToX(freq) * WIDTH} y1={0} y2={HEIGHT} className={styles.gridV} />
        ))}
        {[-12, 0, 12].map(db => (
          <line key={db} x1={0} x2={WIDTH} y1={dbToY(db, VISUAL_RANGE_DB) * HEIGHT} y2={dbToY(db, VISUAL_RANGE_DB) * HEIGHT} className={db === 0 ? styles.gridHZero : styles.gridH} />
        ))}
        <path d={fillPath} className={styles.fill} />
        <path d={linePath} className={styles.curve} />
        {handles.map(h => (
          <g key={h.key}>
            <line x1={h.x} x2={h.x} y1={0} y2={HEIGHT} className={h.key === 'hp' ? styles.cutoffLineHp : styles.cutoffLineLp} />
            <circle cx={h.x} cy={h.y} r={4} className={h.key === 'hp' ? styles.pointHp : styles.pointLp} />
          </g>
        ))}
      </svg>
    </div>
  );
}

export const FilterCurve = memo(FilterCurveComponent);
