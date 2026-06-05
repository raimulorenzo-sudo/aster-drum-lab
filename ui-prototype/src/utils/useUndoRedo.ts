import { useCallback, useEffect, useRef, useState } from 'react';

/**
 * 値のスナップショット履歴を保持し、Undo / Redo を提供するフック。
 *
 * - value の変化を debounce で監視し、debounceMs 以上連続変化がなければスナップ。
 *   (ノブ連続ドラッグや wheel スクロールで履歴を細切れにしないため)
 * - undo/redo の実行直後は次の自動スナップを 1 回スキップ。
 * - past / future の上限は max。古いほうから捨てる。
 */
export function useUndoRedo<T>(
  value: T,
  setValue: (v: T) => void,
  options?: { debounceMs?: number; max?: number; onRestore?: (value: T) => void },
): {
  undo: () => void;
  redo: () => void;
  canUndo: boolean;
  canRedo: boolean;
  clearHistory: () => void;
} {
  const debounceMs = options?.debounceMs ?? 350;
  const max        = options?.max        ?? 50;
  const onRestoreRef = useRef(options?.onRestore);

  const [past,   setPast]   = useState<T[]>([]);
  const [future, setFuture] = useState<T[]>([]);
  const lastSnapshotRef = useRef<T>(value);
  const skipNextRef     = useRef(false);
  const timerRef        = useRef<number | null>(null);

  useEffect(() => {
    onRestoreRef.current = options?.onRestore;
  }, [options?.onRestore]);

  useEffect(() => {
    if (skipNextRef.current) {
      // undo / redo 直後: スナップを取らず、最終スナップ値だけ更新
      skipNextRef.current = false;
      lastSnapshotRef.current = value;
      return;
    }
    if (Object.is(value, lastSnapshotRef.current)) return;

    if (timerRef.current != null) window.clearTimeout(timerRef.current);
    const prevValue = lastSnapshotRef.current;
    timerRef.current = window.setTimeout(() => {
      timerRef.current = null;
      setPast(p => {
        const next = [...p, prevValue];
        return next.length > max ? next.slice(next.length - max) : next;
      });
      setFuture([]);
      lastSnapshotRef.current = value;
    }, debounceMs);

    return () => {
      // 値がさらに変わったら前回の timeout をキャンセル (debounce 再開)
      if (timerRef.current != null) {
        window.clearTimeout(timerRef.current);
        timerRef.current = null;
      }
    };
  }, [value, debounceMs, max]);

  const undo = useCallback(() => {
    setPast(p => {
      if (p.length === 0) return p;
      const prev = p[p.length - 1];
      const current = lastSnapshotRef.current;
      setFuture(f => [current, ...f]);
      skipNextRef.current = true;
      lastSnapshotRef.current = prev;
      // 進行中の debounce snapshot を確定させない
      if (timerRef.current != null) {
        window.clearTimeout(timerRef.current);
        timerRef.current = null;
      }
      setValue(prev);
      onRestoreRef.current?.(prev);
      return p.slice(0, -1);
    });
  }, [setValue]);

  const redo = useCallback(() => {
    setFuture(f => {
      if (f.length === 0) return f;
      const next = f[0];
      const current = lastSnapshotRef.current;
      setPast(p => [...p, current]);
      skipNextRef.current = true;
      lastSnapshotRef.current = next;
      if (timerRef.current != null) {
        window.clearTimeout(timerRef.current);
        timerRef.current = null;
      }
      setValue(next);
      onRestoreRef.current?.(next);
      return f.slice(1);
    });
  }, [setValue]);

  const clearHistory = useCallback(() => {
    setPast([]);
    setFuture([]);
    lastSnapshotRef.current = value;
    if (timerRef.current != null) {
      window.clearTimeout(timerRef.current);
      timerRef.current = null;
    }
  }, [value]);

  return {
    undo,
    redo,
    canUndo: past.length > 0,
    canRedo: future.length > 0,
    clearHistory,
  };
}
