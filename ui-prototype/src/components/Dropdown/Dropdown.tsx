import { useState, useRef, useEffect, useLayoutEffect, type CSSProperties } from 'react';
import { createPortal } from 'react-dom';
import styles from './Dropdown.module.css';
import { measurePopup, positionPopupFromAnchor } from '../../utils/popupPosition';

interface DropdownProps<T extends string | number> {
  value: T;
  options: Array<{ value: T; label: string }>;
  onChange: (v: T) => void | false;
  width?: number | string;
  compact?: boolean;
  /** 接頭辞アイコン (例: フォルダ) */
  leftIcon?: React.ReactNode;
  /** 値が空のときの placeholder */
  placeholder?: string;
  /** options に現在値が含まれない場合の表示名 */
  selectedLabel?: string;
  title?: string;
  onOpenChange?: (open: boolean) => void;
}

export function Dropdown<T extends string | number>({
  value,
  options,
  onChange,
  width,
  compact = false,
  leftIcon,
  placeholder,
  selectedLabel,
  title,
  onOpenChange,
}: DropdownProps<T>) {
  const [open, setOpen] = useState(false);
  const [menuStyle, setMenuStyle] = useState<CSSProperties>({});
  const ref = useRef<HTMLDivElement>(null);
  const buttonRef = useRef<HTMLButtonElement>(null);
  const menuRef = useRef<HTMLDivElement>(null);

  const updateMenuPosition = () => {
    if (!buttonRef.current) return;

    const rect = buttonRef.current.getBoundingClientRect();
    const estimatedHeight = Math.min(320, Math.max(40, options.length * 34 + 8));
    const size = measurePopup(menuRef.current, {
      width: rect.width,
      height: estimatedHeight,
    });
    setMenuStyle(positionPopupFromAnchor(rect, size, {
      width: rect.width,
      minHeight: 96,
      gap: 4,
    }));
  };

  const setOpenState = (next: boolean) => {
    setOpen(next);
    onOpenChange?.(next);
  };

  // クリックアウトサイドで閉じる
  useEffect(() => {
    if (!open) return;
    const handler = (e: MouseEvent) => {
      const target = e.target as Node;
      if (
        ref.current && ref.current.contains(target)
      ) {
        return;
      }
      if (
        menuRef.current && menuRef.current.contains(target)
      ) {
        return;
      }
      setOpenState(false);
    };
    document.addEventListener('mousedown', handler);
    return () => document.removeEventListener('mousedown', handler);
  }, [open]);

  useLayoutEffect(() => {
    if (!open) return;

    updateMenuPosition();
    const handleViewportChange = () => updateMenuPosition();
    window.addEventListener('resize', handleViewportChange);
    window.addEventListener('scroll', handleViewportChange, true);
    return () => {
      window.removeEventListener('resize', handleViewportChange);
      window.removeEventListener('scroll', handleViewportChange, true);
    };
  }, [open, options.length]);

  const selected = options.find(o => o.value === value);

  return (
    <div className={`${styles.wrap} ${compact ? styles.compact : ''}`} style={{ width }} ref={ref}>
      <button
        ref={buttonRef}
        className={`${styles.button} ${open ? styles.open : ''}`}
        title={title}
        onClick={() => setOpenState(!open)}
      >
        {leftIcon && <span className={styles.icon}>{leftIcon}</span>}
        <span className={styles.value}>
          {selectedLabel ?? (selected ? selected.label : placeholder ?? '–')}
        </span>
        <svg width="10" height="10" viewBox="0 0 10 10" className={styles.chev}>
          <path d="M2 4 L5 7 L8 4" stroke="currentColor" strokeWidth="1.2" fill="none" />
        </svg>
      </button>

      {open && createPortal(
        <div ref={menuRef} className={`${styles.menu} ${styles.menuPortal}`} style={menuStyle}>
          {options.map(opt => (
            <button
              key={String(opt.value)}
              className={`${styles.item} ${opt.value === value ? styles.itemActive : ''}`}
              onClick={() => {
                const shouldClose = onChange(opt.value);
                if (shouldClose !== false) setOpenState(false);
              }}
            >
              {opt.label}
            </button>
          ))}
        </div>,
        document.body,
      )}
    </div>
  );
}
