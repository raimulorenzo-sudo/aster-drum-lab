import { memo, useCallback, useEffect, useLayoutEffect, useRef, useState, type MouseEvent, type WheelEvent } from 'react';
import styles from './LayerTabs.module.css';
import type { PadParams } from '../../types';
import {
  MAX_LAYERS_PER_PAD,
  ensureLayers,
  layerDisplayName,
  selectedLayerIndexOf,
} from '../../utils/layerView';
import { registerLayerTab } from '../../utils/layerLevelRegistry';

interface LayerTabsProps {
  pad: PadParams;
  /** Index of the currently selected pad — needed to route flash/meter events. */
  padIndex: number;
  onSelect: (layerIndex: number) => void;
  onAdd: () => void;
  onRemove: (layerIndex: number) => void;
  onToggleMute: (layerIndex: number) => void;
  onToggleSolo: (layerIndex: number) => void;
}

function LayerTabsComponent({
  pad,
  padIndex,
  onSelect,
  onAdd,
  onRemove,
  onToggleMute,
  onToggleSolo,
}: LayerTabsProps) {
  const layers = ensureLayers(pad);
  const count = layers.length;
  const active = selectedLayerIndexOf(pad);
  const canAdd = count < MAX_LAYERS_PER_PAD;
  const canRemove = count > 1;

  // Solo 視認用: 同 Pad 内に Solo Layer があれば他をディム
  const anySolo = layers.some(l => l.solo);

  const stripRef = useRef<HTMLDivElement | null>(null);
  const [overflowState, setOverflowState] = useState({ left: false, right: false });

  // ── Layer flash / meter registration ─────────────────────────────────────
  // refs: layerIndex → DOM element. Populated via callback refs in JSX.
  const meterClipRefs   = useRef<Map<number, HTMLElement | null>>(new Map());
  const meterFillRefs   = useRef<Map<number, HTMLElement | null>>(new Map());
  const flashOverlayRefs = useRef<Map<number, HTMLElement | null>>(new Map());

  useEffect(() => {
    if (count <= 1) return; // 1-layer minimal view: no tabs shown
    const cleanups: (() => void)[] = [];
    for (let i = 0; i < count; i++) {
      const clipEl  = meterClipRefs.current.get(i) ?? null;
      const meterEl = meterFillRefs.current.get(i) ?? null;
      const flashEl = flashOverlayRefs.current.get(i) ?? null;
      cleanups.push(registerLayerTab(padIndex, i, clipEl, meterEl, flashEl));
    }
    return () => { cleanups.forEach(fn => fn()); };
  }, [padIndex, count]);

  const recomputeOverflow = useCallback(() => {
    const el = stripRef.current;
    if (!el) return;
    const overflow = el.scrollWidth > el.clientWidth + 1;
    setOverflowState({
      left: overflow && el.scrollLeft > 1,
      right: overflow && el.scrollLeft + el.clientWidth < el.scrollWidth - 1,
    });
  }, []);

  useLayoutEffect(() => { recomputeOverflow(); }, [recomputeOverflow, count]);
  useEffect(() => {
    const onResize = () => recomputeOverflow();
    window.addEventListener('resize', onResize);
    return () => window.removeEventListener('resize', onResize);
  }, [recomputeOverflow]);

  // 縦ホイールでも横スクロールできるように
  const onWheel = useCallback((e: WheelEvent<HTMLDivElement>) => {
    const el = stripRef.current;
    if (!el) return;
    const delta = Math.abs(e.deltaX) > Math.abs(e.deltaY) ? e.deltaX : e.deltaY;
    if (delta === 0) return;
    el.scrollLeft += delta;
    recomputeOverflow();
  }, [recomputeOverflow]);

  const scrollBy = (dir: -1 | 1) => () => {
    const el = stripRef.current;
    if (!el) return;
    el.scrollBy({ left: dir * 120, behavior: 'smooth' });
    // smooth scroll の直後に状態更新（軽い遅延）
    window.requestAnimationFrame(recomputeOverflow);
  };

  const stop = (e: MouseEvent) => {
    e.preventDefault();
    e.stopPropagation();
  };

  // タブ表記: 最初のLayerは "MAIN"、それ以降は L2/L3/L4...
  const labelOf = (i: number) => (i === 0 ? 'MAIN' : `L${i + 1}`);

  const handleRemove = (idx: number, _hasSample: boolean) => {
    if (!canRemove) return;
    onRemove(idx);
  };

  // 1Layer時はミニマル表示: タブ/L1バッジ/M/S/× をすべて隠し、
  // "+ ADD LAYER" だけを右端に控えめに置く。
  if (count === 1) {
    return (
      <div className={`${styles.bar} ${styles.barMinimal}`} aria-label="Layer selector">
        <button
          type="button"
          className={styles.addBtnInline}
          onClick={onAdd}
          disabled={!canAdd}
          aria-label="Add layer"
          title="Add layer"
        >
          + ADD LAYER
        </button>
      </div>
    );
  }

  return (
    <div className={styles.bar} aria-label="Layer selector">
      {overflowState.left && (
        <button
          type="button"
          className={`${styles.edgeBtn} ${styles.edgeLeft}`}
          onClick={scrollBy(-1)}
          aria-label="Scroll layers left"
        >
          ‹
        </button>
      )}
      <div
        className={styles.strip}
        ref={stripRef}
        onWheel={onWheel}
        onScroll={recomputeOverflow}
      >
        {layers.map((layer, i) => {
          const isActive = i === active;
          const isMuted = layer.mute || (anySolo && !layer.solo);
          const hasSample = Boolean(layer.sampleFileName && layer.sampleFileName.length > 0);
          return (
            <div
              key={i}
              className={`${styles.tab} ${isActive ? styles.tabActive : ''} ${isMuted ? styles.tabDimmed : ''}`}
            >
              <button
                type="button"
                className={styles.tabSelect}
                onClick={() => onSelect(i)}
                aria-pressed={isActive}
                title={layerDisplayName(layer, i)}
              >
                {labelOf(i)}
              </button>
              <button
                type="button"
                className={`${styles.miniBtn} ${styles.miniMute} ${layer.mute ? styles.miniMuteOn : ''}`}
                onClick={e => { stop(e); onToggleMute(i); }}
                aria-pressed={layer.mute}
                title="Mute layer"
              >
                M
              </button>
              <button
                type="button"
                className={`${styles.miniBtn} ${styles.miniSolo} ${layer.solo ? styles.miniSoloOn : ''}`}
                onClick={e => { stop(e); onToggleSolo(i); }}
                aria-pressed={layer.solo}
                title="Solo layer"
              >
                S
              </button>
              <button
                type="button"
                className={styles.miniBtnX}
                onClick={e => { stop(e); handleRemove(i, hasSample); }}
                disabled={!canRemove}
                aria-label={`Remove layer ${i + 1}`}
                title={canRemove ? 'Delete layer' : 'Cannot remove the last layer'}
              >
                ×
              </button>
              {/* Flash overlay — animated by layerLevelRegistry on trigger */}
              <div
                className={styles.tabFlash}
                ref={el => { flashOverlayRefs.current.set(i, el); }}
                aria-hidden="true"
              />
              {/* Level meter — 2px horizontal bar at bottom of tab */}
              <div className={styles.tabMeter} aria-hidden="true">
                <div
                  className={styles.tabMeterClip}
                  ref={el => { meterClipRefs.current.set(i, el); }}
                >
                  <div
                    className={styles.tabMeterFill}
                    ref={el => { meterFillRefs.current.set(i, el); }}
                  />
                </div>
              </div>
            </div>
          );
        })}
        <button
          type="button"
          className={styles.addBtn}
          onClick={onAdd}
          disabled={!canAdd}
          aria-label="Add layer"
          title={canAdd ? 'Add layer' : `Max ${MAX_LAYERS_PER_PAD} layers`}
        >
          +
        </button>
      </div>
      {overflowState.right && (
        <button
          type="button"
          className={`${styles.edgeBtn} ${styles.edgeRight}`}
          onClick={scrollBy(1)}
          aria-label="Scroll layers right"
        >
          ›
        </button>
      )}
      <span className={styles.countLabel}>
        {count}/{MAX_LAYERS_PER_PAD}
      </span>
    </div>
  );
}

export const LayerTabs = memo(LayerTabsComponent);
