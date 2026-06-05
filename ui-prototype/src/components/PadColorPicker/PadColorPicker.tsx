import { useEffect, useMemo, useState } from 'react';
import styles from './PadColorPicker.module.css';

// In-app pad color picker.
//
// We deliberately avoid the native <input type="color"> control here: on macOS
// it opens the system Color Panel, which has no Apply/Cancel buttons and
// commits changes live without a clear confirmation step. Building a small
// in-plugin dialog gives us consistent OK/Cancel semantics across macOS,
// Windows, AU, VST3, and Standalone.

interface Props {
  /** Pad color when the dialog opened. Used as the Cancel target.
   *  Always a `#rrggbb` string — when the pad was in Auto mode the caller
   *  passes the derived category color so the sliders have a starting point. */
  initialColor: string;
  /** True if the pad was on Auto when the dialog opened. */
  startedFromAuto: boolean;
  onApply: (color: string) => void;
  onResetToAuto: () => void;
  onCancel: () => void;
}

function clamp(v: number, lo: number, hi: number) {
  return Math.max(lo, Math.min(hi, v));
}

function hexToRgb(hex: string): [number, number, number] {
  const h = hex.replace('#', '');
  const full = h.length === 3 ? h.split('').map(c => c + c).join('') : h;
  const n = parseInt(full, 16);
  return [(n >> 16) & 0xff, (n >> 8) & 0xff, n & 0xff];
}

function rgbToHex(r: number, g: number, b: number): string {
  const to2 = (n: number) => clamp(Math.round(n), 0, 255).toString(16).padStart(2, '0');
  return `#${to2(r)}${to2(g)}${to2(b)}`;
}

function rgbToHsl(r: number, g: number, b: number): [number, number, number] {
  const rn = r / 255, gn = g / 255, bn = b / 255;
  const max = Math.max(rn, gn, bn);
  const min = Math.min(rn, gn, bn);
  const l = (max + min) / 2;
  let h = 0, s = 0;
  if (max !== min) {
    const d = max - min;
    s = l > 0.5 ? d / (2 - max - min) : d / (max + min);
    switch (max) {
      case rn: h = (gn - bn) / d + (gn < bn ? 6 : 0); break;
      case gn: h = (bn - rn) / d + 2; break;
      case bn: h = (rn - gn) / d + 4; break;
    }
    h *= 60;
  }
  return [h, s * 100, l * 100];
}

function hslToRgb(h: number, s: number, l: number): [number, number, number] {
  const sn = s / 100, ln = l / 100;
  if (sn === 0) {
    const v = Math.round(ln * 255);
    return [v, v, v];
  }
  const hh = ((h % 360) + 360) % 360 / 360;
  const q = ln < 0.5 ? ln * (1 + sn) : ln + sn - ln * sn;
  const p = 2 * ln - q;
  const conv = (t: number) => {
    if (t < 0) t += 1;
    if (t > 1) t -= 1;
    if (t < 1 / 6) return p + (q - p) * 6 * t;
    if (t < 1 / 2) return q;
    if (t < 2 / 3) return p + (q - p) * (2 / 3 - t) * 6;
    return p;
  };
  return [
    Math.round(conv(hh + 1 / 3) * 255),
    Math.round(conv(hh) * 255),
    Math.round(conv(hh - 1 / 3) * 255),
  ];
}

function normalizeHex(input: string): string | null {
  const m = input.trim().toLowerCase().replace(/^#/, '');
  if (/^[0-9a-f]{6}$/.test(m)) return '#' + m;
  if (/^[0-9a-f]{3}$/.test(m)) return '#' + m.split('').map(c => c + c).join('');
  return null;
}

const PRESETS = [
  '#5b8edb', '#52c8e8', '#7fc46e', '#e0c95a',
  '#e09a4a', '#d9605a', '#e08aa8', '#9b78d4',
  '#8c8a82', '#ffffff', '#000000',
];

export function PadColorPicker({ initialColor, startedFromAuto, onApply, onResetToAuto, onCancel }: Props) {
  const [hex, setHex] = useState(initialColor);
  const [hexDraft, setHexDraft] = useState(initialColor);

  const [r, g, b] = useMemo(() => hexToRgb(hex), [hex]);
  const [h, s, l] = useMemo(() => rgbToHsl(r, g, b), [r, g, b]);

  // Keep the hex text field in sync when sliders move (but allow free typing).
  useEffect(() => { setHexDraft(hex); }, [hex]);

  const setFromHsl = (nh: number, ns: number, nl: number) => {
    const [nr, ng, nb] = hslToRgb(nh, ns, nl);
    setHex(rgbToHex(nr, ng, nb));
  };

  const commitHexDraft = () => {
    const n = normalizeHex(hexDraft);
    if (n) setHex(n);
    else setHexDraft(hex);
  };

  // Close on Escape, confirm on Enter.
  useEffect(() => {
    const handler = (e: KeyboardEvent) => {
      if (e.key === 'Escape') onCancel();
      if (e.key === 'Enter') onApply(hex);
    };
    window.addEventListener('keydown', handler);
    return () => window.removeEventListener('keydown', handler);
  }, [hex, onApply, onCancel]);

  return (
    <div className={styles.backdrop} role="presentation" onMouseDown={onCancel}>
      <div
        className={styles.dialog}
        role="dialog"
        aria-modal="true"
        aria-label="Pick Pad Color"
        onMouseDown={(e) => e.stopPropagation()}
      >
        <div className={styles.title}>Pick Pad Color</div>

        <div className={styles.previewRow}>
          <div className={styles.previewSwatch} style={{ background: hex }} aria-label="preview" />
          <input
            className={styles.hexInput}
            type="text"
            value={hexDraft}
            spellCheck={false}
            onChange={(e) => setHexDraft(e.currentTarget.value)}
            onBlur={commitHexDraft}
            onKeyDown={(e) => {
              if (e.key === 'Enter') { commitHexDraft(); e.stopPropagation(); }
            }}
          />
        </div>

        <label className={styles.sliderRow}>
          <span className={styles.sliderLabel}>HUE</span>
          <input
            type="range"
            min={0}
            max={360}
            step={1}
            value={Math.round(h)}
            onChange={(e) => setFromHsl(+e.currentTarget.value, s, l)}
            className={styles.hueSlider}
          />
          <span className={styles.sliderValue}>{Math.round(h)}</span>
        </label>

        <label className={styles.sliderRow}>
          <span className={styles.sliderLabel}>SAT</span>
          <input
            type="range"
            min={0}
            max={100}
            step={1}
            value={Math.round(s)}
            onChange={(e) => setFromHsl(h, +e.currentTarget.value, l)}
            style={{
              background: `linear-gradient(to right, hsl(${h}, 0%, ${l}%), hsl(${h}, 100%, ${l}%))`,
            }}
            className={styles.satSlider}
          />
          <span className={styles.sliderValue}>{Math.round(s)}</span>
        </label>

        <label className={styles.sliderRow}>
          <span className={styles.sliderLabel}>LIGHT</span>
          <input
            type="range"
            min={0}
            max={100}
            step={1}
            value={Math.round(l)}
            onChange={(e) => setFromHsl(h, s, +e.currentTarget.value)}
            style={{
              background: `linear-gradient(to right, #000, hsl(${h}, ${s}%, 50%), #fff)`,
            }}
            className={styles.lightSlider}
          />
          <span className={styles.sliderValue}>{Math.round(l)}</span>
        </label>

        <div className={styles.presetGrid}>
          {PRESETS.map((p) => (
            <button
              key={p}
              type="button"
              className={`${styles.presetSwatch} ${p.toLowerCase() === hex.toLowerCase() ? styles.presetSelected : ''}`}
              style={{ background: p }}
              aria-label={`preset ${p}`}
              onClick={() => setHex(p)}
            />
          ))}
        </div>

        <div className={styles.actions}>
          <button type="button" className={styles.linkBtn} onClick={onResetToAuto}>
            {startedFromAuto ? 'Currently Auto' : 'Reset to Auto'}
          </button>
          <div className={styles.actionsRight}>
            <button type="button" onClick={onCancel}>Cancel</button>
            <button type="button" className={styles.primary} onClick={() => onApply(hex)}>Apply</button>
          </div>
        </div>
      </div>
    </div>
  );
}
