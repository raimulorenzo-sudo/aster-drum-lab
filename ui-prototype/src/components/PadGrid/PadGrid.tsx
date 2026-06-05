import { memo } from 'react';
import styles from './PadGrid.module.css';
import { PadCell } from './PadCell';
import { pageRange } from '../../data/padData';
import type { PadParams, KitPage } from '../../types';

interface PadGridProps {
  pads: PadParams[];           // 48 要素全体
  page: KitPage;
  selectedIndex: number;       // 絶対 index (0..47)
  onSelect: (i: number) => void;
  onPageChange: (p: KitPage) => void;
  onPadContextMenu?: (index: number, x: number, y: number) => void;
  onChangePad?: (index: number, patch: Partial<PadParams>) => void;
  onSampleDrop?: (index: number, file: File) => void;
  onPadSwap?: (sourceIndex: number, targetIndex: number) => void;
}

const PAGES: KitPage[] = ['A', 'B', 'C'];

function PadGridComponent({
  pads,
  page,
  selectedIndex,
  onSelect,
  onPageChange,
  onPadContextMenu,
  onChangePad,
  onSampleDrop,
  onPadSwap,
}: PadGridProps) {
  const [start, end] = pageRange(page);
  const visiblePads = pads.slice(start, end);

  return (
    <div className={styles.grid}>
      {/* ── Page selector ─────────────────────────────────────────────── */}
      <div className={styles.pageRow}>
        <span className={styles.pageLabel}>PAGE</span>
        <div className={styles.pageButtons}>
          {PAGES.map(p => (
            <button
              key={p}
              className={`${styles.pageBtn} ${p === page ? styles.pageBtnActive : ''}`}
              onClick={() => onPageChange(p)}
            >
              {p}
            </button>
          ))}
        </div>
      </div>

      {/* ── 4x4 pad grid ──────────────────────────────────────────────── */}
      <div className={styles.cells}>
        {visiblePads.map((p, i) => {
          const absoluteIndex = start + i;
          return (
            <PadCell
              key={absoluteIndex}
              index={absoluteIndex}
              pad={p}
              selected={absoluteIndex === selectedIndex}
              onClick={() => onSelect(absoluteIndex)}
              onContextMenu={onPadContextMenu}
              onChange={(patch) => onChangePad?.(absoluteIndex, patch)}
              onSampleDrop={onSampleDrop}
              onPadSwap={onPadSwap}
            />
          );
        })}
      </div>
    </div>
  );
}

// memo: PadGrid re-renders when its pads / page / selectedIndex change.
// Per-cell PadCell already has its own memo so most cells skip render.
export const PadGrid = memo(PadGridComponent);
