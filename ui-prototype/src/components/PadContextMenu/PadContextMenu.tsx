import { useLayoutEffect, useRef, useState, type CSSProperties } from 'react';
import styles from './PadContextMenu.module.css';
import type { PadParams } from '../../types';
import { padDisplayColor } from '../../data/padData';
import { measurePopup, positionPopupFromPoint, positionSubmenuFromAnchor } from '../../utils/popupPosition';

// Preset color palette for "Change Pad Color..." submenu.
// "Auto" is the special first entry — picking it clears any user override
// so the pad reverts to its category color.
const PRESET_COLORS: Array<{ label: string; value: string }> = [
  { label: 'Blue',   value: '#5b8edb' },
  { label: 'Cyan',   value: '#52c8e8' },
  { label: 'Green',  value: '#7fc46e' },
  { label: 'Yellow', value: '#e0c95a' },
  { label: 'Orange', value: '#e09a4a' },
  { label: 'Red',    value: '#d9605a' },
  { label: 'Pink',   value: '#e08aa8' },
  { label: 'Purple', value: '#9b78d4' },
  { label: 'Gray',   value: '#8c8a82' },
];

interface PadContextMenuProps {
  x: number;
  y: number;
  pad: PadParams;
  /** どの Layer を対象に Load/Replace/Clear/Reveal が動くか。
   *  選択中 Pad のメニューなら active layer、別 Pad のメニューなら 0 (MAIN) を渡す。 */
  activeLayerIndex: number;
  /** Pad が持つ Layer 数 (Add Layer ボタンの可否判定用)。 */
  layerCount: number;
  /** Layer 機能の上限 (典型: 8)。 */
  maxLayers: number;
  canPaste: boolean;
  onClose: () => void;
  onLoadSample: () => void;
  onReplaceSample: () => void;
  onClearSample: () => void;
  onCopyPad: () => void;
  onPastePad: () => void;
  onResetSettings: () => void;
  onRenamePad: () => void;
  onSetMidiNote: () => void;
  onChangePadColor: (color: string | undefined) => void;
  onOpenCustomColorPicker: () => void;
  onRevealSample: () => void;
  onAddLayer: () => void;
  onDuplicateLayer: () => void;
  // ── Layer / Pad 一括操作 (v7+) ─────────────────────────────────────
  /** 現在選択中 Layer のオーディオ系パラメータを初期値に戻す (サンプル維持) */
  onResetLayerSettings: () => void;
  /** 現在選択中 Layer のサンプル + パラメータをクリア */
  onClearLayer: () => void;
  /** Pad 全体を空に (全 Layer + Pad 音作りパラメータ, ただし MIDI Note / Output Assign / Choke は維持) */
  onClearPad: () => void;
  /** Clear Pad 用の有効性判定 (サンプルあり or 設定変更ありなら true) */
  canClearPad: boolean;
}

export function PadContextMenu({
  x,
  y,
  pad,
  activeLayerIndex,
  layerCount,
  maxLayers,
  canPaste,
  onClose,
  onLoadSample,
  onReplaceSample,
  onClearSample,
  onCopyPad,
  onPastePad,
  onResetSettings,
  onRenamePad,
  onSetMidiNote,
  onChangePadColor,
  onOpenCustomColorPicker,
  onRevealSample,
  onAddLayer,
  onDuplicateLayer,
  onResetLayerSettings,
  onClearLayer,
  onClearPad,
  canClearPad,
}: PadContextMenuProps) {
  // 対象 Layer のサンプル参照を見て、Load / Replace / Clear / Reveal の有効性を判定する。
  // activeLayerIndex==0 → flat fields、それ以外 → pad.layers[activeLayerIndex] を参照。
  const targetLayer = activeLayerIndex > 0 ? pad.layers?.[activeLayerIndex] : undefined;
  const sampleFileName = targetLayer?.sampleFileName ?? pad.sampleFileName;
  const sampleFilePath = targetLayer?.sampleFilePath ?? pad.sampleFilePath;
  const sampleMissing = targetLayer?.sampleMissing ?? pad.sampleMissing;
  const hasSample = Boolean(sampleFileName || sampleFilePath);
  const canRevealSample = Boolean(sampleFilePath && !sampleMissing);

  // 多層 Pad のときだけ「MAIN / Layer N」ラベルを足す。1Layer 状態では Pad-level
  // ラベル (Sample のみ) のままにして既存の見た目に近づける。
  const isMultiLayer = layerCount >= 2;
  const layerNoun = activeLayerIndex === 0 ? 'MAIN' : `Layer ${activeLayerIndex + 1}`;
  const layerSuffix = isMultiLayer ? ` to ${layerNoun}` : '';
  const inLayerSuffix = isMultiLayer ? ` in ${layerNoun}` : '';
  const ofLayerSuffix = isMultiLayer ? ` ${layerNoun}` : '';
  const canAddLayer = layerCount < maxLayers;
  const [colorMenuOpen, setColorMenuOpen] = useState(false);
  const [menuStyle, setMenuStyle] = useState<CSSProperties>(() =>
    positionPopupFromPoint(x, y, { width: 220, height: 330 }, { width: 220 }),
  );
  const [submenuStyle, setSubmenuStyle] = useState<CSSProperties>({});
  const menuRef = useRef<HTMLDivElement>(null);
  const submenuParentRef = useRef<HTMLDivElement>(null);
  const submenuRef = useRef<HTMLDivElement>(null);

  const updateMenuPosition = () => {
    const size = measurePopup(menuRef.current, { width: 220, height: 330 });
    setMenuStyle(positionPopupFromPoint(x, y, size, { width: 220, minHeight: 120 }));
  };

  const updateSubmenuPosition = () => {
    if (!submenuParentRef.current) return;
    const parentRect = submenuParentRef.current.getBoundingClientRect();
    const size = measurePopup(submenuRef.current, { width: 200, height: 370 });
    setSubmenuStyle(positionSubmenuFromAnchor(parentRect, size, { width: 200, minHeight: 120 }));
  };

  useLayoutEffect(() => {
    updateMenuPosition();
    const handleViewportChange = () => updateMenuPosition();
    window.addEventListener('resize', handleViewportChange);
    window.addEventListener('scroll', handleViewportChange, true);
    return () => {
      window.removeEventListener('resize', handleViewportChange);
      window.removeEventListener('scroll', handleViewportChange, true);
    };
  }, [x, y]);

  useLayoutEffect(() => {
    if (!colorMenuOpen) return;
    updateSubmenuPosition();
    const handleViewportChange = () => updateSubmenuPosition();
    window.addEventListener('resize', handleViewportChange);
    window.addEventListener('scroll', handleViewportChange, true);
    return () => {
      window.removeEventListener('resize', handleViewportChange);
      window.removeEventListener('scroll', handleViewportChange, true);
    };
  }, [colorMenuOpen, menuStyle]);

  const item = (label: string, action: () => void, disabled = false) => (
    <button
      type="button"
      className={styles.item}
      disabled={disabled}
      onClick={() => {
        if (disabled) return;
        action();
        onClose();
      }}
    >
      {label}
    </button>
  );

  const pickColor = (color: string | undefined) => {
    onChangePadColor(color);
    onClose();
  };

  const openCustomPicker = () => {
    onOpenCustomColorPicker();
    onClose();
  };

  return (
    <>
      <button className={styles.scrim} type="button" aria-label="close menu" onClick={onClose} />
      <div ref={menuRef} className={styles.menu} style={menuStyle} role="menu">
        {item(
          hasSample ? `Replace Sample${inLayerSuffix}…` : `Load Sample${layerSuffix}…`,
          hasSample ? onReplaceSample : onLoadSample,
        )}
        {hasSample && item(`Load Sample${layerSuffix}…`, onLoadSample)}
        {item(`Clear${ofLayerSuffix || ' Sample'}`, onClearSample, !hasSample)}
        {isMultiLayer && <span className={styles.divider} />}
        {isMultiLayer && item('Add Layer…', onAddLayer, !canAddLayer)}
        {isMultiLayer && item(`Duplicate ${layerNoun}`, onDuplicateLayer, !canAddLayer)}
        <span className={styles.divider} />
        {item('Copy Pad', onCopyPad)}
        {item('Paste Pad', onPastePad, !canPaste)}
        {item('Reset Pad Settings', onResetSettings)}
        {/* Layer / Pad 一括操作 — Layer 番号は 1-indexed で表示 */}
        {item(`Reset Layer ${activeLayerIndex + 1} Settings`, onResetLayerSettings)}
        {item(`Clear Layer ${activeLayerIndex + 1}`, onClearLayer, !hasSample)}
        {item('Clear Pad', onClearPad, !canClearPad)}
        {item('Rename Pad', onRenamePad)}
        {item('Set MIDI Note...', onSetMidiNote)}
        <div
          ref={submenuParentRef}
          className={styles.submenuParent}
          onMouseEnter={() => setColorMenuOpen(true)}
        >
          <button
            type="button"
            className={`${styles.item} ${styles.itemWithChevron}`}
            onClick={(e) => {
              e.stopPropagation();
              setColorMenuOpen(v => !v);
            }}
          >
            <span>Change Pad Color...</span>
            <span className={styles.chevron}>▸</span>
          </button>
          {colorMenuOpen && (
            <div ref={submenuRef} className={styles.submenu} style={submenuStyle} role="menu">
              <button
                type="button"
                className={`${styles.item} ${styles.colorItem}`}
                onClick={() => pickColor(undefined)}
              >
                <span
                  className={styles.colorSwatch}
                  style={{ background: padDisplayColor({ ...pad, padColor: undefined }) }}
                />
                <span>{pad.padColor ? 'Reset to Auto Color' : 'Auto ✓'}</span>
              </button>
              <span className={styles.divider} />
              {PRESET_COLORS.map(({ label, value }) => (
                <button
                  key={value}
                  type="button"
                  className={`${styles.item} ${styles.colorItem}`}
                  onClick={() => pickColor(value)}
                >
                  <span className={styles.colorSwatch} style={{ background: value }} />
                  <span>{label}{pad.padColor?.toLowerCase() === value.toLowerCase() ? ' ✓' : ''}</span>
                </button>
              ))}
              <span className={styles.divider} />
              <button
                type="button"
                className={`${styles.item} ${styles.colorItem}`}
                onClick={openCustomPicker}
              >
                <span
                  className={styles.colorSwatch}
                  style={{ background: pad.padColor ?? 'linear-gradient(135deg, #888, #ccc)' }}
                />
                <span>Custom Color...</span>
              </button>
            </div>
          )}
        </div>
        <span className={styles.divider} />
        {item(
          isMultiLayer
            ? `Reveal ${layerNoun} Sample in Finder/Explorer`
            : 'Reveal Sample in Finder/Explorer',
          onRevealSample,
          !canRevealSample,
        )}
      </div>
    </>
  );
}
