import { memo, useCallback, useEffect, useRef, useState, type PointerEvent } from 'react';
import { createPortal } from 'react-dom';
import styles from './VelocityRangeSlider.module.css';

// ─────────────────────────────────────────────────────────────────────────────
// VelocityRangeSlider — Pad 内全 Layer の velocity range を一覧表示する。
//
// 操作:
//  - thumb ドラッグ            → range を編集
//  - Command (Mac) / Ctrl (Win) を押しながらドラッグ → 1/10 速度の細かいドラッグ
//  - Option (Mac) / Alt  (Win) を押しながらクリック → 0–127 フルレンジにリセット
//  - 数値をダブルクリック      → インライン入力で直接入力 (Enter 確定 / Esc キャンセル)
//  - 行ラベルをクリック        → アクティブ Layer 切替
//  - [⇋ SPLIT] ボタン         → 現在の Layer 数で即時均等分割
//  - [AUTO ▾] ドロップダウン   → 任意の分割数を選択
//  - liveVelocity             → 全行に縦線 + 当該 Layer を発光
// ─────────────────────────────────────────────────────────────────────────────

interface LayerRange {
  velocityMin: number;
  velocityMax: number;
}

interface VelocityRangeSliderProps {
  layers: LayerRange[];
  activeLayerIndex: number;
  onSelectLayer: (layerIndex: number) => void;
  onChangeRange: (layerIndex: number, next: { min: number; max: number }) => void;
  onAutoSplit?: (splits: number) => void;
  liveVelocity?: number | null;
}

const RANGE = 127;
const AUTO_OPTIONS: Array<{ value: number; label: string }> = [
  { value: 1, label: 'Full Range' },
  { value: 2, label: '2 Split' },
  { value: 3, label: '3 Split' },
  { value: 4, label: '4 Split' },
  { value: 5, label: '5 Split' },
  { value: 6, label: '6 Split' },
  { value: 7, label: '7 Split' },
  { value: 8, label: '8 Split' },
];

const AUTO_MENU_HEIGHT_ESTIMATE = 8 * 24 + 8;

function clamp(v: number) {
  if (!Number.isFinite(v)) return 0;
  return Math.max(0, Math.min(RANGE, Math.round(v)));
}

function labelOf(i: number): string {
  return i === 0 ? 'MAIN' : `L${i + 1}`;
}

type MenuPos =
  | { kind: 'below'; top: number; right: number }
  | { kind: 'above'; bottom: number; right: number };

function VelocityRangeSliderComponent({
  layers,
  activeLayerIndex,
  onSelectLayer,
  onChangeRange,
  onAutoSplit,
  liveVelocity,
}: VelocityRangeSliderProps) {
  const autoBtnRef = useRef<HTMLButtonElement | null>(null);
  const [autoOpen, setAutoOpen] = useState(false);
  const [menuPos, setMenuPos] = useState<MenuPos>({ kind: 'above', bottom: 0, right: 0 });

  useEffect(() => {
    if (!autoOpen) return;
    const onDocClick = () => setAutoOpen(false);
    window.addEventListener('mousedown', onDocClick);
    return () => window.removeEventListener('mousedown', onDocClick);
  }, [autoOpen]);

  useEffect(() => {
    if (!autoOpen || !autoBtnRef.current) return;
    const rect = autoBtnRef.current.getBoundingClientRect();
    const spaceBelow = window.innerHeight - rect.bottom;
    const spaceAbove = rect.top;
    const right = window.innerWidth - rect.right;
    const fitsBelow = spaceBelow >= AUTO_MENU_HEIGHT_ESTIMATE + 4;
    if (!fitsBelow && spaceAbove > spaceBelow) {
      setMenuPos({ kind: 'above', bottom: window.innerHeight - rect.top + 4, right });
    } else {
      setMenuPos({ kind: 'below', top: rect.bottom + 4, right });
    }
  }, [autoOpen]);

  const liveVel = typeof liveVelocity === 'number' ? clamp(liveVelocity) : null;
  const liveVelPct = liveVel !== null ? (liveVel / RANGE) * 100 : null;

  return (
    <div className={styles.wrap}>
      <div className={styles.header}>
        <span className={styles.title}>VEL RANGE</span>
        {onAutoSplit && (
          <div className={styles.actions} onMouseDown={e => e.stopPropagation()}>
            <button
              type="button"
              className={styles.splitBtn}
              onClick={() => onAutoSplit(layers.length)}
              title={`Split velocity evenly across all ${layers.length} layers`}
            >
              ⇋ SPLIT
            </button>
            <button
              ref={autoBtnRef}
              type="button"
              className={styles.autoBtn}
              onClick={() => setAutoOpen(o => !o)}
              aria-haspopup="listbox"
              aria-expanded={autoOpen}
              title="Choose split count or restore full range"
            >
              AUTO <span className={styles.caret}>▾</span>
            </button>
            {autoOpen && createPortal(
              <ul
                className={styles.autoMenu}
                role="listbox"
                style={
                  menuPos.kind === 'above'
                    ? { bottom: menuPos.bottom, right: menuPos.right }
                    : { top: menuPos.top, right: menuPos.right }
                }
                onMouseDown={e => e.stopPropagation()}
              >
                {AUTO_OPTIONS.map(opt => (
                  <li key={opt.value}>
                    <button
                      type="button"
                      className={styles.autoOpt}
                      onClick={() => { onAutoSplit(opt.value); setAutoOpen(false); }}
                    >
                      {opt.label}
                    </button>
                  </li>
                ))}
              </ul>,
              document.body
            )}
          </div>
        )}
      </div>

      <div className={styles.rows}>
        {layers.map((layer, li) => (
          <LayerRangeRow
            key={li}
            label={labelOf(li)}
            min={layer.velocityMin}
            max={layer.velocityMax}
            active={li === activeLayerIndex}
            hit={liveVel !== null && liveVel >= layer.velocityMin && liveVel <= layer.velocityMax}
            onSelect={() => onSelectLayer(li)}
            onChange={(next) => onChangeRange(li, next)}
            liveVelPct={liveVelPct}
          />
        ))}
      </div>
    </div>
  );
}

export const VelocityRangeSlider = memo(VelocityRangeSliderComponent);

// ─────────────────────────────────────────────────────────────────────────────
// LayerRangeRow
// ─────────────────────────────────────────────────────────────────────────────
interface LayerRangeRowProps {
  label: string;
  min: number;
  max: number;
  active: boolean;
  hit: boolean;
  onSelect: () => void;
  onChange: (next: { min: number; max: number }) => void;
  liveVelPct: number | null;
}

type Editing = { which: 'min' | 'max'; draft: string } | null;

function LayerRangeRow({
  label,
  min,
  max,
  active,
  hit,
  onSelect,
  onChange,
  liveVelPct,
}: LayerRangeRowProps) {
  const trackRef = useRef<HTMLDivElement | null>(null);
  const inputRef = useRef<HTMLInputElement | null>(null);
  const [editing, setEditing] = useState<Editing>(null);

  // input が開いたら即 select-all
  useEffect(() => {
    if (editing) {
      requestAnimationFrame(() => {
        inputRef.current?.select();
      });
    }
  }, [editing]);

  // ── インライン編集 ─────────────────────────────────────────────
  const beginEdit = useCallback((which: 'min' | 'max') => {
    onSelect();
    setEditing({ which, draft: String(which === 'min' ? min : max) });
  }, [min, max, onSelect]);

  const commitEdit = useCallback(() => {
    setEditing(prev => {
      if (!prev) return null;
      const v = parseInt(prev.draft, 10);
      if (!isNaN(v)) {
        const cv = clamp(v);
        if (prev.which === 'min') onChange({ min: Math.min(cv, max), max });
        else onChange({ min, max: Math.max(cv, min) });
      }
      return null;
    });
  }, [min, max, onChange]);

  // ── thumb ドラッグ ─────────────────────────────────────────────
  // デルタベース: thumb は現在位置から相対移動。
  //   - 通常ドラッグ  → 1:1 移動
  //   - Command (Mac) / Ctrl (Win) 押しながら → 0.1× 細かさ
  //   - Option (Mac) / Alt (Win) → ドラッグせずにフルレンジリセット
  const startDrag = useCallback(
    (kind: 'min' | 'max') => (e: PointerEvent<HTMLDivElement>) => {
      e.preventDefault();
      e.stopPropagation();

      // Option / Alt → リセット
      if (e.altKey) {
        onSelect();
        onChange({ min: 0, max: RANGE });
        return;
      }

      onSelect();
      (e.target as Element).setPointerCapture?.(e.pointerId);

      const trackEl = trackRef.current;
      const trackWidth = trackEl ? trackEl.getBoundingClientRect().width : 200;
      // 浮動小数でアキュムレート (clamp は送信時のみ)
      let current = kind === 'min' ? min : max;
      let lastX = e.clientX;

      const onMove = (ev: globalThis.PointerEvent) => {
        const isFine = ev.metaKey || ev.ctrlKey; // Command(Mac) / Ctrl(Win)
        const dx = ev.clientX - lastX;
        lastX = ev.clientX;
        const delta = (dx / trackWidth) * RANGE * (isFine ? 0.1 : 1.0);
        current = Math.max(0, Math.min(RANGE, current + delta));
        const v = Math.round(current);
        if (kind === 'min') onChange({ min: Math.min(v, max), max });
        else onChange({ min, max: Math.max(v, min) });
      };

      const onUp = () => {
        window.removeEventListener('pointermove', onMove);
        window.removeEventListener('pointerup', onUp);
      };
      window.addEventListener('pointermove', onMove);
      window.addEventListener('pointerup', onUp);
    },
    [min, max, onChange, onSelect],
  );

  const minPct = (min / RANGE) * 100;
  const maxPct = (max / RANGE) * 100;

  const rowCls = [
    styles.row,
    active ? styles.rowActive : '',
    hit ? styles.rowHit : '',
  ].filter(Boolean).join(' ');

  return (
    <div className={rowCls}>
      {/* Layer ラベル — クリックで選択 */}
      <button
        type="button"
        className={styles.rowLabel}
        onClick={onSelect}
        title={`Edit ${label}`}
      >
        {label}
      </button>

      {/* レンジトラック */}
      <div className={styles.track} ref={trackRef} onMouseDown={onSelect}>
        <div
          className={styles.fill}
          style={{ left: `${minPct}%`, width: `${Math.max(0, maxPct - minPct)}%` }}
        />
        <div
          className={styles.thumb}
          style={{ left: `${minPct}%` }}
          onPointerDown={startDrag('min')}
          role="slider"
          aria-valuemin={0}
          aria-valuemax={RANGE}
          aria-valuenow={min}
          aria-label={`${label} velocity min`}
        />
        <div
          className={styles.thumb}
          style={{ left: `${maxPct}%` }}
          onPointerDown={startDrag('max')}
          role="slider"
          aria-valuemin={0}
          aria-valuemax={RANGE}
          aria-valuenow={max}
          aria-label={`${label} velocity max`}
        />
        {liveVelPct !== null && (
          <div className={styles.liveMarker} style={{ left: `${liveVelPct}%` }} aria-hidden />
        )}
      </div>

      {/* 数値表示 — ダブルクリックでインライン編集 */}
      <span className={styles.rowValues}>
        {editing?.which === 'min' ? (
          <input
            ref={inputRef}
            className={styles.valueInput}
            value={editing.draft}
            onChange={e => setEditing({ which: 'min', draft: e.currentTarget.value })}
            onKeyDown={e => {
              if (e.key === 'Enter') { e.preventDefault(); commitEdit(); }
              if (e.key === 'Escape') { e.preventDefault(); setEditing(null); }
            }}
            onBlur={commitEdit}
            type="text"
            inputMode="numeric"
          />
        ) : (
          <b
            onDoubleClick={e => { e.stopPropagation(); beginEdit('min'); }}
            title="Double-click to edit"
            style={{ cursor: 'text' }}
          >
            {min}
          </b>
        )}
        <span>–</span>
        {editing?.which === 'max' ? (
          <input
            ref={inputRef}
            className={styles.valueInput}
            value={editing.draft}
            onChange={e => setEditing({ which: 'max', draft: e.currentTarget.value })}
            onKeyDown={e => {
              if (e.key === 'Enter') { e.preventDefault(); commitEdit(); }
              if (e.key === 'Escape') { e.preventDefault(); setEditing(null); }
            }}
            onBlur={commitEdit}
            type="text"
            inputMode="numeric"
          />
        ) : (
          <b
            onDoubleClick={e => { e.stopPropagation(); beginEdit('max'); }}
            title="Double-click to edit"
            style={{ cursor: 'text' }}
          >
            {max}
          </b>
        )}
      </span>
    </div>
  );
}
