import { memo, useCallback, useMemo, useRef, useState } from 'react';
import styles from './VelCurveEditor.module.css';
import { Dropdown } from '../Dropdown/Dropdown';
import {
  DEFAULT_VEL_CURVE,
  presetVelCurve,
  sampleVelCurve,
  withUpdatedPoint,
} from '../../utils/velCurve';
import type { VelCurvePreset, VelCurveState } from '../../types';

interface Props {
  curve: VelCurveState | undefined;
  onChange: (next: VelCurveState) => void;
}

const PRESET_OPTIONS: { value: VelCurvePreset; label: string }[] = [
  { value: 'Linear',        label: 'Linear' },
  { value: 'Soft',          label: 'Soft' },
  { value: 'Hard',          label: 'Hard' },
  { value: 'Less Dynamics', label: 'Less Dynamics' },
  { value: 'More Dynamics', label: 'More Dynamics' },
  { value: 'Custom',        label: 'Custom' },
];

// SVG viewBox 用の論理サイズ
const VB_W = 200;
const VB_H = 110;
const PAD_L = 4;
const PAD_R = 4;
const PAD_T = 4;
const PAD_B = 4;
const INNER_W = VB_W - PAD_L - PAD_R;
const INNER_H = VB_H - PAD_T - PAD_B;

function VelCurveEditorComponent({ curve, onChange }: Props) {
  const cur = curve ?? DEFAULT_VEL_CURVE;
  const [dragging, setDragging] = useState<'p1' | 'p2' | null>(null);
  const [hover,    setHover]    = useState<'p1' | 'p2' | null>(null);
  const svgRef = useRef<SVGSVGElement | null>(null);

  // ── サンプル → SVG path ────────────────────────────────────────────
  const pathD = useMemo(() => {
    const pts = sampleVelCurve(cur, 64);
    return pts
      .map((p, i) =>
        `${i === 0 ? 'M' : 'L'} ${(PAD_L + p.x * INNER_W).toFixed(1)} ${(PAD_T + (1 - p.y) * INNER_H).toFixed(1)}`
      )
      .join(' ');
  }, [cur]);

  // ── ポイント座標 (SVG viewBox 単位) ─────────────────────────────────
  const toSvg = (x: number, y: number) => ({
    cx: PAD_L + x * INNER_W,
    cy: PAD_T + (1 - y) * INNER_H,
  });

  // ── マウス座標 → 正規化 (0..1, y は上が 1) ─────────────────────────
  const localCoord = useCallback((clientX: number, clientY: number) => {
    const el = svgRef.current;
    if (!el) return { x: 0, y: 0 };
    const r = el.getBoundingClientRect();
    const lx = (clientX - r.left) / r.width  * VB_W;
    const ly = (clientY - r.top)  / r.height * VB_H;
    const x = (lx - PAD_L) / INNER_W;
    const y = 1 - (ly - PAD_T) / INNER_H;
    return { x: Math.max(0, Math.min(1, x)), y: Math.max(0, Math.min(1, y)) };
  }, []);

  // ── ドラッグ ────────────────────────────────────────────────────────
  const onPointDown = useCallback((id: 'p1' | 'p2') => (e: React.PointerEvent) => {
    e.preventDefault();
    e.stopPropagation();
    (e.currentTarget as Element).setPointerCapture?.(e.pointerId);
    setDragging(id);
  }, []);

  const onMove = useCallback((e: React.PointerEvent) => {
    if (!dragging) return;
    const { x, y } = localCoord(e.clientX, e.clientY);
    const next = withUpdatedPoint(cur, dragging, { x, y });
    onChange(next);
  }, [dragging, cur, localCoord, onChange]);

  const onUp = useCallback(() => setDragging(null), []);

  // ── ダブルクリック (背景): Linear リセット ──────────────────────────
  const onDoubleClickBg = useCallback((e: React.MouseEvent) => {
    e.preventDefault();
    onChange(presetVelCurve('Linear'));
  }, [onChange]);

  const onChangePreset = useCallback((preset: VelCurvePreset) => {
    if (preset === 'Custom') return; // Custom は表示用; 選択しても何もしない
    onChange(presetVelCurve(preset));
  }, [onChange]);

  const onClickReset = useCallback(() => onChange(presetVelCurve('Linear')), [onChange]);

  const p1 = toSvg(cur.p1.x, cur.p1.y);
  const p2 = toSvg(cur.p2.x, cur.p2.y);

  return (
    <div className={styles.wrap}>
      {/* ── ヘッダー: ラベル ────────────────────── */}
      <div className={styles.title}>VEL CURVE</div>

      <div className={styles.row}>
        {/* ── グラフ ───────────────────────────── */}
        <div className={styles.graphWrap}>
          <svg
            ref={svgRef}
            className={styles.svg}
            viewBox={`0 0 ${VB_W} ${VB_H}`}
            preserveAspectRatio="none"
            onPointerMove={onMove}
            onPointerUp={onUp}
            onDoubleClick={onDoubleClickBg}
          >
            {/* グリッド: 横軸 64 / 縦軸 64 (==0.5) と端 */}
            {[0, 0.25, 0.5, 0.75, 1].map(t => (
              <line
                key={`vx-${t}`}
                x1={PAD_L + t * INNER_W} y1={PAD_T}
                x2={PAD_L + t * INNER_W} y2={PAD_T + INNER_H}
                className={t === 0.5 ? styles.gridMid : styles.grid}
              />
            ))}
            {[0, 0.25, 0.5, 0.75, 1].map(t => (
              <line
                key={`hy-${t}`}
                x1={PAD_L}            y1={PAD_T + (1 - t) * INNER_H}
                x2={PAD_L + INNER_W}  y2={PAD_T + (1 - t) * INNER_H}
                className={t === 0.5 ? styles.gridMid : styles.grid}
              />
            ))}

            {/* カーブ */}
            <path d={pathD} className={styles.curve} />

            {/* 端点 (固定, 視覚マーカーのみ) */}
            <circle cx={PAD_L}             cy={PAD_T + INNER_H} r={2.4} className={styles.endPoint} />
            <circle cx={PAD_L + INNER_W}   cy={PAD_T}           r={2.4} className={styles.endPoint} />

            {/* 可動ポイント */}
            {(['p1','p2'] as const).map(id => {
              const pos = id === 'p1' ? p1 : p2;
              const active = dragging === id || hover === id;
              return (
                <g key={id}>
                  {/* 当たり判定: 透明な大きめの円 */}
                  <circle
                    cx={pos.cx} cy={pos.cy} r={9}
                    className={styles.pointHit}
                    onPointerDown={onPointDown(id)}
                    onPointerEnter={() => setHover(id)}
                    onPointerLeave={() => setHover(prev => prev === id ? null : prev)}
                  />
                  {/* 見た目 */}
                  <circle
                    cx={pos.cx} cy={pos.cy}
                    r={active ? 4.4 : 3.6}
                    className={`${styles.point} ${active ? styles.pointActive : ''}`}
                  />
                </g>
              );
            })}
          </svg>
          {/* 軸ラベル — 控えめ */}
          <div className={styles.axisX}>
            <span>0</span><span>64</span><span>127</span>
          </div>
          <div className={styles.axisY}>
            <span>127</span><span>64</span><span>0</span>
          </div>
          <div className={styles.axisCaption}>INPUT VELOCITY</div>
        </div>

        {/* ── 右側: PRESET + Reset ─────────────── */}
        <div className={styles.side}>
          <span className={styles.sideLabel}>PRESET</span>
          <Dropdown<VelCurvePreset>
            value={cur.preset}
            onChange={onChangePreset}
            options={PRESET_OPTIONS}
          />
          <button
            type="button"
            className={styles.resetBtn}
            onClick={onClickReset}
            title="Reset to Linear"
          >
            <svg width="11" height="11" viewBox="0 0 14 14" aria-hidden>
              <path
                d="M11.5 3.5 A5 5 0 1 0 12.5 8"
                stroke="currentColor" strokeWidth="1.5" fill="none" strokeLinecap="round"
              />
              <path d="M12.5 1.5 V 4.5 H 9.5" stroke="currentColor" strokeWidth="1.5" fill="none" strokeLinecap="round" strokeLinejoin="round" />
            </svg>
            <span>Reset</span>
          </button>
        </div>
      </div>
    </div>
  );
}

export const VelCurveEditor = memo(VelCurveEditorComponent);
