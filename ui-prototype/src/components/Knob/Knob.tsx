import { memo, useCallback, useId, useRef, useState } from 'react';
import styles from './Knob.module.css';
import { parseNumericText } from '../../utils/numericInput';
import type { AutomationTarget } from '../../utils/automationTarget';

export type KnobProps = {
  /** 現在値 (min..max の範囲) */
  value: number;
  /** ラベル (ゴールド色, 上に表示) */
  label?: string;
  /** 最小値 (default 0) */
  min?: number;
  /** 最大値 (default 1) */
  max?: number;
  /** ノブサイズ px (default 48) */
  size?: number;
  /** 値表示の単位 ('Hz', 'ms', '%' など) */
  unit?: string;
  /** 値表示文字列を完全カスタム (unit より優先)。null = 非表示 */
  valueText?: string | null;
  /** 数値入力開始時の初期文字列。valueText が null のノブでも使う。 */
  inputText?: string;
  /** true なら中央 ((min+max)/2) が起点。Pan/Pitch などの bipolar 用 */
  bipolar?: boolean;
  /** 値変更コールバック (省略時はドラッグ操作無効) */
  onChange?: (value: number) => void;
  /** ダブルクリックで戻す値 */
  defaultValue?: number;
  /** リセット時のコールバック。未指定なら defaultValue を onChange へ渡す */
  onReset?: () => void;
  /** ダブルクリック数値入力。戻り値は `min..max` と同じ単位。null = キャンセル */
  parseInput?: (text: string) => number | null;
  /**
   * 識別キー。"同じノブが別オブジェクトの編集対象へ切り替わる" 状況
   * (例: PadControlSections で選択 Pad が切り替わる) で、
   * 値表示が偶然一致していても確実に再レンダーさせるために使う。
   * 親が padIndex 等を渡せば、memo 比較に含まれて
   * onChange / onReset の closure が最新版へ差し替わる。
   * 値変動だけが起きる単一オブジェクト編集では渡さなくてよい。
   */
  ownerKey?: string | number | null;
  /** AUTO mode/right-click automation assignment target. */
  automationTarget?: AutomationTarget;
};

const MIN_ANGLE = -135;
const MAX_ANGLE =  135;
const SPAN      = MAX_ANGLE - MIN_ANGLE;          // 270deg
// 60 Hz throttle: 60Hz ディスプレイで毎フレーム値が更新される。
// (旧 30 Hz だと 2 フレームに 1 回しか動かず "カクつき" として知覚される)
const CONTINUOUS_UPDATE_MS = 1000 / 60;

function knobDebugEnabled() {
  try {
    if (new URLSearchParams(window.location.search).has('debugKnobs')) return true;
    return localStorage.getItem('ASTER_DEBUG_KNOBS') === '1'
      || Boolean((window as Window & { __ASTER_DEBUG_KNOBS?: boolean }).__ASTER_DEBUG_KNOBS);
  } catch {
    return false;
  }
}

function knobDebugLog(message: string, payload: unknown) {
  if (!knobDebugEnabled()) return;
  try {
    console.info(`[ASTER KNOB TRACE] ${message} ${JSON.stringify(payload)}`);
  } catch {
    console.info(`[ASTER KNOB TRACE] ${message}`);
  }
}

function KnobImpl({
  value,
  label,
  min   = 0,
  max   = 1,
  size  = 48,
  unit,
  valueText,
  inputText,
  bipolar = false,
  onChange,
  defaultValue,
  onReset,
  parseInput,
  automationTarget,
}: KnobProps) {
  const uid = useId().replace(/:/g, '');
  const faceGradientId = `knobFace-${uid}`;
  const [editing, setEditing] = useState(false);
  const [draft, setDraft] = useState('');

  // ── 値の正規化 (0..1) ─────────────────────────────────────────────
  const range = max - min;
  const v     = range > 0 ? Math.max(0, Math.min(1, (value - min) / range)) : 0;

  // ── 現在角度 ──────────────────────────────────────────────────────
  const currentAngle = MIN_ANGLE + SPAN * v;

  // ── ドラッグハンドラ (onChange 提供時のみ有効) ───────────────────
  const draggingRef = useRef(false);
  const handlePointerDown = useCallback(
    (e: React.PointerEvent) => {
      const debug = knobDebugEnabled();
      if (e.detail >= 2) return;
      if (!onChange) {
        if (debug) {
          knobDebugLog('pointerDown ignored: no onChange', { label, value, min, max });
        }
        return;
      }
      e.preventDefault();
      if (e.altKey) {
        if (onReset) onReset();
        else if (defaultValue !== undefined) onChange(defaultValue);
        return;
      }
      (e.currentTarget as Element).setPointerCapture(e.pointerId);
      draggingRef.current = true;
      const startY = e.clientY;
      const startV = v;
      let lastSentAt = 0;
      let lastSentValue = value;
      let latestValue = value;

      if (debug) {
        const rect = (e.currentTarget as HTMLElement).getBoundingClientRect();
        const front = document.elementFromPoint(e.clientX, e.clientY) as HTMLElement | null;
        knobDebugLog('pointerDown', {
          label,
          value,
          min,
          max,
          rect: {
            x: Math.round(rect.x),
            y: Math.round(rect.y),
            width: Math.round(rect.width),
            height: Math.round(rect.height),
          },
          targetClass: (e.target as HTMLElement | null)?.className ?? '',
          currentTargetClass: (e.currentTarget as HTMLElement | null)?.className ?? '',
          frontElement: front
            ? {
                tag: front.tagName,
                className: front.className,
                ariaLabel: front.getAttribute('aria-label'),
                role: front.getAttribute('role'),
              }
            : null,
        });
      }

      const emitChange = (nextValue: number, force = false) => {
        latestValue = nextValue;
        const now = performance.now();
        if (!force && now - lastSentAt < CONTINUOUS_UPDATE_MS) return;
        if (lastSentValue === nextValue) return;

        lastSentAt = now;
        lastSentValue = nextValue;
        onChange(nextValue);
      };

      const onMove = (ev: PointerEvent) => {
        if (!draggingRef.current) return;
        // 上方向ドラッグで値が増える / Command(Ctrl) か Shift で微調整
        const dy   = startY - ev.clientY;
        const sens = (ev.metaKey || ev.ctrlKey || ev.shiftKey) ? 900 : 180;
        const nv   = Math.max(0, Math.min(1, startV + dy / sens));
        emitChange(min + nv * range);
      };
      const onUp = () => {
        emitChange(latestValue, true);
        if (debug) {
          knobDebugLog('pointerUp', { label, latestValue });
        }
        draggingRef.current = false;
        window.removeEventListener('pointermove', onMove);
        window.removeEventListener('pointerup',   onUp);
      };
      window.addEventListener('pointermove', onMove);
      window.addEventListener('pointerup',   onUp);
    },
    [defaultValue, onChange, onReset, v, value, min, max, range, label],
  );

  const handleDoubleClick = useCallback(
    (e: React.MouseEvent) => {
      e.preventDefault();
      e.stopPropagation();
      if (!onChange) return;
      setDraft(inputText ?? (valueText && valueText !== '–' ? valueText : String(value)));
      setEditing(true);
    },
    [inputText, onChange, value, valueText],
  );

  const commitDraft = useCallback(() => {
    if (!onChange) {
      setEditing(false);
      return;
    }

    const parsed = (parseInput ?? parseNumericText)(draft);
    if (parsed !== null) {
      const next = Math.max(min, Math.min(max, parsed));
      onChange(next);
    }
    setEditing(false);
  }, [draft, max, min, onChange, parseInput]);

  const cancelDraft = useCallback(() => {
    setEditing(false);
  }, []);

  // ── SVG ジオメトリ ────────────────────────────────────────────────
  // メーターはノブの「内側の溝」に沿わせる。画像のブラシ仕上げトップと
  // 外周ベゼルの境目あたりが視覚的に最適 → size * 0.41 がちょうど良い。
  const cx     = size / 2;
  const cy     = size / 2;
  const meterR = size * 0.42;
  const faceR  = size * 0.305;
  const indicatorInnerR = size * 0.215;
  const indicatorOuterR = size * 0.335;

  // メーターアーク (bipolar なら中央起点)
  const meterFromAngle = bipolar ? 0 : MIN_ANGLE;
  const showMeter      = Math.abs(currentAngle - meterFromAngle) > 0.5;
  const meterPath      = showMeter
    ? describeArc(
        cx, cy, meterR,
        Math.min(meterFromAngle, currentAngle),
        Math.max(meterFromAngle, currentAngle),
      )
    : '';

  const indicatorInner = polarToCartesian(cx, cy, indicatorInnerR, currentAngle);
  const indicatorOuter = polarToCartesian(cx, cy, indicatorOuterR, currentAngle);

  // ── 値表示テキストの決定 ─────────────────────────────────────────
  let displayValue: string = '–';
  if (valueText !== undefined && valueText !== null) {
    displayValue = valueText;
  } else if (valueText === undefined) {
    displayValue = unit ? `${value.toFixed(2)} ${unit}` : value.toFixed(2);
  }
  const showValue = valueText !== null;

  return (
    <div className={styles.wrap}>
      {label && <div className={styles.label}>{label}</div>}

      <div
        className={styles.knobOuter}
        style={{ width: size, height: size }}
        onPointerDown={handlePointerDown}
        onDoubleClick={handleDoubleClick}
        role={onChange ? 'slider' : undefined}
        aria-valuemin={min}
        aria-valuemax={max}
        aria-valuenow={value}
        aria-label={label}
        tabIndex={onChange ? 0 : -1}
        data-automation-target-id={automationTarget?.id}
        data-automation-target-name={automationTarget?.name}
      >
        <span className={styles.body} aria-hidden="true">
          <span className={styles.face} />
        </span>

        <svg
          className={styles.meterSvg}
          width={size}
          height={size}
          viewBox={`0 0 ${size} ${size}`}
          aria-hidden="true"
        >
          <defs>
            <radialGradient id={faceGradientId} cx="42%" cy="28%" r="70%">
              <stop offset="0%" stopColor="rgba(52, 52, 52, 0.76)" />
              <stop offset="48%" stopColor="rgba(32, 32, 32, 0.94)" />
              <stop offset="100%" stopColor="rgba(18, 18, 18, 1)" />
            </radialGradient>
          </defs>

          <circle
            cx={cx}
            cy={cy}
            r={faceR}
            fill={`url(#${faceGradientId})`}
            stroke="rgba(80, 76, 68, 0.38)"
            strokeWidth="0.75"
          />

          {/* 全レンジの非常に薄いガイドリング */}
          <path
            d={describeArc(cx, cy, meterR, MIN_ANGLE, MAX_ANGLE)}
            stroke="rgba(100, 96, 88, 0.20)"
            strokeWidth="1.15"
            fill="none"
            strokeLinecap="round"
          />

          {/* メーターアーク (値に追従) — グローは feGaussianBlur ではなく
              薄い太線を下に敷くことで疑似。フィルタを使わない分、
              ノブが多数並ぶ画面でも合計レンダリングコストが大幅に下がる。
              stroke 色は className 経由で CSS から差し替え可能 (Layer 編集中の
              ゴールドテーマ等)。 */}
          {showMeter && (
            <>
              <path
                d={meterPath}
                className={styles.meterArcUnder}
                strokeWidth="3.2"
                fill="none"
                strokeLinecap="round"
                aria-hidden
              />
              <path
                d={meterPath}
                className={styles.meterArc}
                strokeWidth="1.65"
                fill="none"
                strokeLinecap="round"
              />
            </>
          )}

          {/* インジケータ針も同様に薄太線を下敷きにして疑似グロー */}
          <line
            x1={indicatorInner.x}
            y1={indicatorInner.y}
            x2={indicatorOuter.x}
            y2={indicatorOuter.y}
            className={styles.indicatorUnder}
            strokeWidth="3.0"
            strokeLinecap="round"
            aria-hidden
          />
          <line
            x1={indicatorInner.x}
            y1={indicatorInner.y}
            x2={indicatorOuter.x}
            y2={indicatorOuter.y}
            className={styles.indicator}
            strokeWidth="1.45"
            strokeLinecap="round"
          />
        </svg>
      </div>

      {editing ? (
        <input
          className={styles.valueInput}
          value={draft}
          autoFocus
          onMouseDown={(e) => e.stopPropagation()}
          onDoubleClick={(e) => e.stopPropagation()}
          onChange={(e) => setDraft(e.currentTarget.value)}
          onBlur={cancelDraft}
          onKeyDown={(e) => {
            if (e.key === 'Enter') commitDraft();
            if (e.key === 'Escape') cancelDraft();
          }}
        />
      ) : (
        showValue && <div className={styles.value} onDoubleClick={handleDoubleClick}>{displayValue}</div>
      )}
    </div>
  );
}

// memo: Knob renders pure SVG from its props. Parents that re-render for
// reasons unrelated to this knob's value (e.g. level meter ticks, or a
// sibling parameter update) cause `describeArc()` and the full SVG tree
// to be rebuilt unless we skip via memo. With dozens of Knobs visible
// (PadControlSections ~13, Mixer Pan ~16, two master OUTPUT knobs), the
// total cost adds up.
//
// Custom compare ignores onChange / onReset identity on purpose — call sites
// use inline arrows like `v => onChange({ ... })` which would otherwise
// defeat memo on every parent render.
//
// Caveat: if the SAME knob instance is reused for different edit targets
// (e.g. PadControlSections re-points its 11 knobs to a different pad when
// the user selects another pad), memo will skip the re-render whenever the
// visible props happen to match — and the stale onChange closure keeps
// writing to the previous pad. Parents in that situation must pass an
// `ownerKey` (typically the pad index) so memo can detect the switch.
export const Knob = memo(KnobImpl, (prev, next) =>
  prev.value === next.value &&
  prev.label === next.label &&
  prev.min   === next.min   &&
  prev.max   === next.max   &&
  prev.size  === next.size  &&
  prev.unit  === next.unit  &&
  prev.valueText    === next.valueText    &&
  prev.inputText    === next.inputText    &&
  prev.bipolar      === next.bipolar      &&
  prev.defaultValue === next.defaultValue &&
  prev.ownerKey     === next.ownerKey     &&
  prev.parseInput   === next.parseInput,
);

/* ─── SVG arc 補助関数 ─────────────────────────────────────────────── */
function polarToCartesian(cx: number, cy: number, r: number, deg: number) {
  const a = (deg - 90) * (Math.PI / 180);
  return { x: cx + r * Math.cos(a), y: cy + r * Math.sin(a) };
}

function describeArc(
  cx: number, cy: number, r: number,
  startDeg: number, endDeg: number,
): string {
  const s = polarToCartesian(cx, cy, r, endDeg);
  const e = polarToCartesian(cx, cy, r, startDeg);
  const large = endDeg - startDeg <= 180 ? '0' : '1';
  return [
    'M', s.x.toFixed(2), s.y.toFixed(2),
    'A', r.toFixed(2), r.toFixed(2), 0, large, 0,
    e.x.toFixed(2), e.y.toFixed(2),
  ].join(' ');
}
