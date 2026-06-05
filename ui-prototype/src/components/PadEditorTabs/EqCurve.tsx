import { memo, useCallback, useEffect, useMemo, useRef, useState } from 'react';
import styles from './EqCurve.module.css';
import type { EqParams } from '../../types';
import { dbToY, freqToX, sampleEqCurve, xToFreq, yToDb } from '../../utils/eqMath';

interface Props {
  eq: EqParams;
  bypassed: boolean;
  activePoint: PointId | null;
  onChange: (patch: Partial<EqParams>) => void;
  onActivate: (p: PointId | null) => void;
}

export type PointId = 'low' | 'lowMid' | 'highMid' | 'high';

const WIDTH = 184;
const HEIGHT = 56;
const LINE_FLOOR_OFFSET_DB = 1.5;

function EqCurveComponent({ eq, bypassed, activePoint, onChange, onActivate }: Props) {
  const svgRef = useRef<SVGSVGElement | null>(null);
  const [drag, setDrag] = useState<PointId | null>(null);
  const hasCutMode = eq.lowMode === 'cut' || eq.highMode === 'cut';
  const visualRange = hasCutMode ? 36 : 18;

  /** Sample the curve only when eq actually changes (avoid recompute on every parent render). */
  const samples = useMemo(() => sampleEqCurve(eq, 128), [eq]);

  const curvePath = useMemo(() => {
    const lineFloorDb = -visualRange + LINE_FLOOR_OFFSET_DB;
    let previousWasVisible = false;
    return samples
      .map(p => {
        if (hasCutMode && p.db <= lineFloorDb) {
          previousWasVisible = false;
          return '';
        }
        const command = previousWasVisible ? 'L' : 'M';
        previousWasVisible = true;
        return `${command} ${(p.x * WIDTH).toFixed(1)} ${(dbToY(p.db, visualRange) * HEIGHT).toFixed(1)}`;
      })
      .filter(Boolean)
      .join(' ');
  }, [hasCutMode, samples, visualRange]);

  const fillCurvePath = useMemo(() => {
    return samples
      .map((p, i) =>
        `${i === 0 ? 'M' : 'L'} ${(p.x * WIDTH).toFixed(1)} ${(dbToY(p.db, visualRange) * HEIGHT).toFixed(1)}`,
      )
      .join(' ');
  }, [samples, visualRange]);

  const fillPath = useMemo(() => {
    if (!fillCurvePath) return '';
    const midY = (0.5 * HEIGHT).toFixed(1);
    return `${fillCurvePath} L ${WIDTH} ${midY} L 0 ${midY} Z`;
  }, [fillCurvePath]);

  const lowCutFillPath = useMemo(() => {
    if (eq.lowMode !== 'cut') return '';
    const cutoffX = freqToX(eq.low.freq);
    const section = samples.filter(p => p.x <= cutoffX + 0.015);
    if (section.length < 2) return '';
    const body = section
      .map((p, i) =>
        `${i === 0 ? 'M' : 'L'} ${(p.x * WIDTH).toFixed(1)} ${(dbToY(p.db, visualRange) * HEIGHT).toFixed(1)}`,
      )
      .join(' ');
    const lastX = (section[section.length - 1].x * WIDTH).toFixed(1);
    return `${body} L ${lastX} ${HEIGHT} L 0 ${HEIGHT} Z`;
  }, [eq.low.freq, eq.lowMode, samples, visualRange]);

  const highCutFillPath = useMemo(() => {
    if (eq.highMode !== 'cut') return '';
    const cutoffX = freqToX(eq.high.freq);
    const section = samples.filter(p => p.x >= cutoffX - 0.015);
    if (section.length < 2) return '';
    const body = section
      .map((p, i) =>
        `${i === 0 ? 'M' : 'L'} ${(p.x * WIDTH).toFixed(1)} ${(dbToY(p.db, visualRange) * HEIGHT).toFixed(1)}`,
      )
      .join(' ');
    const firstX = (section[0].x * WIDTH).toFixed(1);
    return `${body} L ${WIDTH} ${HEIGHT} L ${firstX} ${HEIGHT} Z`;
  }, [eq.high.freq, eq.highMode, samples, visualRange]);

  const points = useMemo(() => [
    { id: 'low' as const,     x: freqToX(eq.low.freq),     y: eq.lowMode === 'cut' ? 0.5 : dbToY(eq.low.gain, visualRange), label: 'LOW' },
    { id: 'lowMid' as const,  x: freqToX(eq.lowMid.freq),  y: dbToY(eq.lowMid.gain, visualRange),  label: 'L MID' },
    { id: 'highMid' as const, x: freqToX(eq.highMid.freq), y: dbToY(eq.highMid.gain, visualRange), label: 'H MID' },
    { id: 'high' as const,    x: freqToX(eq.high.freq),    y: eq.highMode === 'cut' ? 0.5 : dbToY(eq.high.gain, visualRange), label: 'HIGH' },
  ], [eq, visualRange]);

  const localCoord = useCallback((clientX: number, clientY: number) => {
    const el = svgRef.current;
    if (!el) return { x: 0, y: 0 };
    const rect = el.getBoundingClientRect();
    return {
      x: Math.max(0, Math.min(1, (clientX - rect.left) / rect.width)),
      y: Math.max(0, Math.min(1, (clientY - rect.top) / rect.height)),
    };
  }, []);

  const onPointerDown = useCallback((id: PointId) => (e: React.PointerEvent) => {
    e.preventDefault();
    (e.currentTarget as Element).setPointerCapture?.(e.pointerId);
    setDrag(id);
    onActivate(id);
  }, [onActivate]);

  const onPointerMove = useCallback((e: React.PointerEvent) => {
    if (!drag) return;
    const { x, y } = localCoord(e.clientX, e.clientY);
    const freq = xToFreq(x);
    const gain = Math.max(-18, Math.min(18, yToDb(y, 18)));
    const isEdgeCut = (drag === 'low' && eq.lowMode === 'cut') || (drag === 'high' && eq.highMode === 'cut');
    onChange({ [drag]: { ...eq[drag], freq: Math.max(20, Math.min(20000, freq)), gain: isEdgeCut ? eq[drag].gain : gain } });
  }, [drag, eq, localCoord, onChange]);

  const onPointerUp = useCallback(() => setDrag(null), []);

  const onPointDouble = useCallback((id: PointId) => {
    onChange({ [id]: { ...eq[id], gain: 0 } });
  }, [eq, onChange]);

  // global pointer-up safety
  useEffect(() => {
    if (!drag) return;
    const up = () => setDrag(null);
    window.addEventListener('pointerup', up);
    return () => window.removeEventListener('pointerup', up);
  }, [drag]);

  return (
    <div className={`${styles.wrap} ${bypassed ? styles.bypassed : ''}`}>
      <svg
        ref={svgRef}
        className={styles.svg}
        viewBox={`0 0 ${WIDTH} ${HEIGHT}`}
        preserveAspectRatio="none"
        onPointerMove={onPointerMove}
        onPointerUp={onPointerUp}
      >
        {/* grid: freq verticals */}
        {[20, 100, 1000, 10000, 20000].map(f => {
          const x = freqToX(f) * WIDTH;
          return (
            <g key={f}>
              <line x1={x} x2={x} y1={0} y2={HEIGHT} className={styles.gridV} />
              <text x={x + 2} y={HEIGHT - 3} className={styles.gridLabel}>
                {f >= 1000 ? `${f / 1000}k` : f}
              </text>
            </g>
          );
        })}
        {/* grid: dB horizontals */}
        {[-12, 0, 12].map(db => {
          const y = dbToY(db, visualRange) * HEIGHT;
          return (
            <g key={db}>
              <line x1={0} x2={WIDTH} y1={y} y2={y} className={db === 0 ? styles.gridHZero : styles.gridH} />
              <text x={2} y={y - 2} className={styles.gridLabel}>
                {db > 0 ? `+${db}` : db} dB
              </text>
            </g>
          );
        })}

        {/* curve fill + line */}
        {lowCutFillPath && <path d={lowCutFillPath} className={styles.cutFillLow} />}
        {highCutFillPath && <path d={highCutFillPath} className={styles.cutFillHigh} />}
        <path d={fillPath} className={styles.curveFill} />
        {eq.lowMode === 'cut' && (
          <line
            x1={freqToX(eq.low.freq) * WIDTH}
            x2={freqToX(eq.low.freq) * WIDTH}
            y1={0}
            y2={HEIGHT}
            className={styles.cutLineLow}
          />
        )}
        {eq.highMode === 'cut' && (
          <line
            x1={freqToX(eq.high.freq) * WIDTH}
            x2={freqToX(eq.high.freq) * WIDTH}
            y1={0}
            y2={HEIGHT}
            className={styles.cutLineHigh}
          />
        )}
        <path d={curvePath} className={styles.curve} />

        {/* points */}
        {points.map(p => (
          <g key={p.id}>
            <circle
              cx={p.x * WIDTH}
              cy={p.y * HEIGHT}
              r={activePoint === p.id ? 4.5 : 3.5}
              className={`${styles.point} ${styles[`point${p.id[0].toUpperCase()}${p.id.slice(1)}`]} ${activePoint === p.id ? styles.pointActive : ''}`}
              onPointerDown={onPointerDown(p.id)}
              onDoubleClick={() => onPointDouble(p.id)}
            />
            <text
              x={p.x * WIDTH}
              y={p.y * HEIGHT - 10}
              textAnchor="middle"
              className={`${styles.pointLabel} ${activePoint === p.id ? styles.pointLabelActive : ''}`}
            >
              {p.label}
            </text>
          </g>
        ))}
      </svg>
    </div>
  );
}

export const EqCurve = memo(EqCurveComponent);
