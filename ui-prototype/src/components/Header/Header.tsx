import { useState, useRef, useEffect, useLayoutEffect, type CSSProperties } from 'react';
import { createPortal } from 'react-dom';
import styles from './Header.module.css';
import { measurePopup, positionPopupFromAnchor } from '../../utils/popupPosition';

export type UiScale = 0.5 | 0.75 | 1 | 1.2 | 1.5 | 1.75 | 2;
export type HeaderKitItem = {
  name: string;
  path: string;
  isDefault?: boolean;
};

const UI_SCALES: UiScale[] = [2, 1.75, 1.5, 1.2, 1, 0.75, 0.5];

interface HeaderProps {
  kitName: string;
  kitItems: HeaderKitItem[];
  currentKitPath: string;
  uiScale: UiScale;
  onSelectKit: (path: string) => void;
  onRescanKitFolder: () => void;
  onRevealKitFolder: () => void;
  onSaveKit: () => void;
  onSaveKitAs: () => void;
  onUiScaleChange: (scale: UiScale) => void;
  // Undo / Redo (Cmd+Z / Cmd+Shift+Z は App 側で keyboard listener)
  onUndo: () => void;
  onRedo: () => void;
  canUndo: boolean;
  canRedo: boolean;
}

export function Header(props: HeaderProps) {
  const [menuOpen, setMenuOpen] = useState(false);
  const [kitMenuOpen, setKitMenuOpen] = useState(false);
  const [menuStyle, setMenuStyle] = useState<CSSProperties>({});
  const [kitMenuStyle, setKitMenuStyle] = useState<CSSProperties>({});
  const kitAreaRef = useRef<HTMLDivElement>(null);
  const kitButtonRef = useRef<HTMLButtonElement>(null);
  const kitMenuPanelRef = useRef<HTMLDivElement>(null);
  const actionsRef = useRef<HTMLDivElement>(null);
  const menuButtonRef = useRef<HTMLButtonElement>(null);
  const menuPanelRef = useRef<HTMLDivElement>(null);

  const updateMenuPosition = () => {
    if (!menuButtonRef.current) return;

    const rect = menuButtonRef.current.getBoundingClientRect();
    const width = 214;
    const size = measurePopup(menuPanelRef.current, { width, height: 172 });
    setMenuStyle(positionPopupFromAnchor(rect, size, {
      align: 'end',
      width,
      minHeight: 96,
      gap: 10,
    }));
  };

  const updateKitMenuPosition = () => {
    if (!kitButtonRef.current) return;

    const rect = kitButtonRef.current.getBoundingClientRect();
    const width = Math.max(230, rect.width);
    const estimatedHeight = Math.min(360, Math.max(96, props.kitItems.length * 30 + 72));
    const size = measurePopup(kitMenuPanelRef.current, { width, height: estimatedHeight });
    setKitMenuStyle(positionPopupFromAnchor(rect, size, {
      width,
      minHeight: 120,
      gap: 6,
    }));
  };

  const chooseScale = (scale: UiScale) => {
    props.onUiScaleChange(scale);
    setMenuOpen(false);
  };

  useEffect(() => {
    if (!menuOpen && !kitMenuOpen) return;

    const handlePointerDown = (e: MouseEvent) => {
      const target = e.target as Node;
      if (actionsRef.current?.contains(target)) return;
      if (menuPanelRef.current?.contains(target)) return;
      if (kitAreaRef.current?.contains(target)) return;
      if (kitMenuPanelRef.current?.contains(target)) return;
      setMenuOpen(false);
      setKitMenuOpen(false);
    };

    document.addEventListener('mousedown', handlePointerDown);
    return () => document.removeEventListener('mousedown', handlePointerDown);
  }, [menuOpen, kitMenuOpen]);

  useLayoutEffect(() => {
    if (!menuOpen) return;

    updateMenuPosition();
    const handleViewportChange = () => updateMenuPosition();
    window.addEventListener('resize', handleViewportChange);
    window.addEventListener('scroll', handleViewportChange, true);
    return () => {
      window.removeEventListener('resize', handleViewportChange);
      window.removeEventListener('scroll', handleViewportChange, true);
    };
  }, [menuOpen]);

  useLayoutEffect(() => {
    if (!kitMenuOpen) return;

    updateKitMenuPosition();
    const handleViewportChange = () => updateKitMenuPosition();
    window.addEventListener('resize', handleViewportChange);
    window.addEventListener('scroll', handleViewportChange, true);
    return () => {
      window.removeEventListener('resize', handleViewportChange);
      window.removeEventListener('scroll', handleViewportChange, true);
    };
  }, [kitMenuOpen, props.kitItems.length]);

  const isCurrentKit = (item: HeaderKitItem) => {
    if (item.isDefault) return props.currentKitPath.length === 0;
    return item.path === props.currentKitPath;
  };

  const chooseKit = (path: string) => {
    props.onSelectKit(path);
    setKitMenuOpen(false);
  };

  return (
    <header className={styles.header}>
      {/* ── ロゴ（サンバースト + ブランド名） ─────────────────────────── */}
      <div className={styles.brand}>
        <SunburstMark />
        <div className={styles.title}>
          <span className={styles.brandMark}>ASTER</span>
          <span className={styles.brandSub}>Drum Lab</span>
        </div>
      </div>

      {/* ── KIT セレクタ ───────────────────────────────────────────────── */}
      <div className={styles.kitArea} ref={kitAreaRef}>
        <span className={styles.kitLabel}>KIT:</span>
        <button
          ref={kitButtonRef}
          type="button"
          className={`${styles.kitSelect} ${kitMenuOpen ? styles.kitSelectActive : ''}`}
          onClick={() => setKitMenuOpen(v => !v)}
          aria-haspopup="menu"
          aria-expanded={kitMenuOpen}
        >
          <span className={styles.kitName}>{props.kitName}</span>
          <svg width="10" height="10" viewBox="0 0 10 10" aria-hidden>
            <path d="M2 4 L5 7 L8 4" stroke="currentColor" strokeWidth="1.2" fill="none" />
          </svg>
        </button>
        {kitMenuOpen && createPortal(
          <div ref={kitMenuPanelRef} className={styles.kitMenuPanel} style={kitMenuStyle} role="menu">
            {props.kitItems.map((item, index) => (
              <div key={item.isDefault ? 'default' : item.path}>
                {index === 1 && <div className={styles.kitMenuDivider} />}
                <button
                  type="button"
                  className={`${styles.kitMenuItem} ${isCurrentKit(item) ? styles.kitMenuItemActive : ''}`}
                  onClick={() => chooseKit(item.path)}
                  role="menuitem"
                >
                  <span className={styles.kitCheck}>{isCurrentKit(item) ? '✓' : ''}</span>
                  <span className={styles.kitMenuName}>{item.name}</span>
                </button>
              </div>
            ))}
            <div className={styles.kitMenuDivider} />
            <button
              type="button"
              className={styles.kitMenuItem}
              onClick={() => {
                props.onRescanKitFolder();
                setKitMenuOpen(false);
              }}
              role="menuitem"
            >
              <span className={styles.kitCheck} />
              <span className={styles.kitMenuName}>Rescan Kit Folder</span>
            </button>
            <button
              type="button"
              className={styles.kitMenuItem}
              onClick={() => {
                props.onRevealKitFolder();
                setKitMenuOpen(false);
              }}
              role="menuitem"
            >
              <span className={styles.kitCheck} />
              <span className={styles.kitMenuName}>Reveal Kit Folder</span>
            </button>
          </div>,
          document.body,
        )}
      </div>

      {/* ── 右上ボタン ────────────────────────────────────────────────── */}
      <div className={styles.actions} ref={actionsRef}>
        {/* Undo / Redo (Cmd+Z / Cmd+Shift+Z) */}
        <button
          className={styles.iconBtn}
          onClick={props.onUndo}
          disabled={!props.canUndo}
          aria-label="Undo"
          title="Undo (Cmd+Z)"
        >
          <svg width="14" height="14" viewBox="0 0 14 14" aria-hidden>
            <path d="M4 4 L1.5 6.5 L4 9" stroke="currentColor" strokeWidth="1.4" fill="none" strokeLinecap="round" strokeLinejoin="round" />
            <path d="M1.8 6.5 H 8 a3.5 3.5 0 0 1 0 7" stroke="currentColor" strokeWidth="1.4" fill="none" strokeLinecap="round" />
          </svg>
        </button>
        <button
          className={styles.iconBtn}
          onClick={props.onRedo}
          disabled={!props.canRedo}
          aria-label="Redo"
          title="Redo (Cmd+Shift+Z)"
        >
          <svg width="14" height="14" viewBox="0 0 14 14" aria-hidden>
            <path d="M10 4 L12.5 6.5 L10 9" stroke="currentColor" strokeWidth="1.4" fill="none" strokeLinecap="round" strokeLinejoin="round" />
            <path d="M12.2 6.5 H 6 a3.5 3.5 0 0 0 0 7" stroke="currentColor" strokeWidth="1.4" fill="none" strokeLinecap="round" />
          </svg>
        </button>
        <span className={styles.actionSeparator} aria-hidden />

        <button className={styles.actionBtn} onClick={props.onSaveKit}>SAVE KIT</button>
        <button className={styles.actionBtn} onClick={props.onSaveKitAs}>SAVE KIT AS</button>
        <button
          ref={menuButtonRef}
          className={`${styles.menuBtn} ${menuOpen ? styles.menuBtnActive : ''}`}
          aria-label="menu"
          aria-expanded={menuOpen}
          onClick={() => setMenuOpen(v => !v)}
        >
          <svg width="14" height="14" viewBox="0 0 14 14">
            <line x1="2" y1="4"  x2="12" y2="4"  stroke="currentColor" strokeWidth="1.2" />
            <line x1="2" y1="7"  x2="12" y2="7"  stroke="currentColor" strokeWidth="1.2" />
            <line x1="2" y1="10" x2="12" y2="10" stroke="currentColor" strokeWidth="1.2" />
          </svg>
        </button>

        {menuOpen && createPortal(
          <div ref={menuPanelRef} className={styles.menuPanel} style={menuStyle}>
            <div className={styles.menuTitle}>UI SIZE</div>
            <div className={styles.scaleGrid}>
              {UI_SCALES.map(scale => (
                <button
                  key={scale}
                  className={`${styles.scaleItem} ${props.uiScale === scale ? styles.scaleItemActive : ''}`}
                  onClick={() => chooseScale(scale)}
                >
                  {Math.round(scale * 100)}%
                </button>
              ))}
            </div>
          </div>,
          document.body,
        )}
      </div>
    </header>
  );
}

/* ─────────────────────────────────────────────────────────────────────
   SunburstMark
   16 本の細い放射線（長短を交互）＋ 細い菱形セレブレーション
   シャンパンゴールド寄りのグラデーションと、ごく弱い外側グロー。
   ───────────────────────────────────────────────────────────────────── */
function SunburstMark() {
  // 16 本の放射線：cardinal/intercardinal を長く、その間のサブレイを短く
  const rays = Array.from({ length: 16 }, (_, i) => {
    const angle = (i * 360) / 16;
    const isMajor = i % 2 === 0;
    return {
      angle,
      r1: isMajor ? 4.0 : 3.4,
      r2: isMajor ? 14.0 : 9.4,
      opacity: isMajor ? 1 : 0.74,
      width: isMajor ? 0.95 : 0.7,
    };
  });

  return (
    <svg
      width="36"
      height="36"
      viewBox="-18 -18 36 36"
      className={styles.logo}
      aria-hidden
    >
      <defs>
        <linearGradient id="asterGrad" x1="0" y1="-16" x2="0" y2="16"
                        gradientUnits="userSpaceOnUse">
          <stop offset="0%"  stopColor="#ead8a6" />
          <stop offset="48%" stopColor="#c6a86a" />
          <stop offset="100%" stopColor="#8c7140" />
        </linearGradient>
        <radialGradient id="asterCore" cx="0" cy="0" r="3"
                        gradientUnits="userSpaceOnUse">
          <stop offset="0%"   stopColor="#f2e6bd" stopOpacity="0.94" />
          <stop offset="60%"  stopColor="#c6a86a" stopOpacity="0.46" />
          <stop offset="100%" stopColor="#8c7140" stopOpacity="0" />
        </radialGradient>
      </defs>

      {/* 中心の柔らかい光（コアブルーム） */}
      <circle cx="0" cy="0" r="6" fill="url(#asterCore)" />

      {/* 16 本の放射線 */}
      <g stroke="url(#asterGrad)" strokeLinecap="round" fill="none">
        {rays.map((ray, i) => {
          const rad = (ray.angle * Math.PI) / 180;
          const x1 = Math.cos(rad) * ray.r1;
          const y1 = Math.sin(rad) * ray.r1;
          const x2 = Math.cos(rad) * ray.r2;
          const y2 = Math.sin(rad) * ray.r2;
          return (
            <line
              key={i}
              x1={x1.toFixed(2)}
              y1={y1.toFixed(2)}
              x2={x2.toFixed(2)}
              y2={y2.toFixed(2)}
              strokeWidth={ray.width}
              opacity={ray.opacity}
            />
          );
        })}
      </g>

      {/* 中心の細い菱形コア（シャンパンゴールド） */}
      <path
        d="M 0 -2.6 L 2.0 0 L 0 2.6 L -2.0 0 Z"
        fill="#e0cf9d"
        opacity="0.9"
      />
    </svg>
  );
}
