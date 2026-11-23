import { useCallback, useEffect, useRef, useState } from 'react';

/** Debounced gesture history. Restores are emitted outside React state updaters. */
export function useUndoRedo<T>(
  value: T,
  setValue: (v: T) => void,
  options?: { debounceMs?: number; max?: number; onRestore?: (value: T) => void; isEqual?: (a: T, b: T) => boolean },
): { undo: () => void; redo: () => void; canUndo: boolean; canRedo: boolean; clearHistory: (value?: T) => void } {
  const debounceMs = options?.debounceMs ?? 350;
  const max = options?.max ?? 50;
  const callbacks = useRef({ setValue, onRestore: options?.onRestore, equal: options?.isEqual ?? Object.is });
  callbacks.current = { setValue, onRestore: options?.onRestore, equal: options?.isEqual ?? Object.is };
  const latest = useRef(value); latest.current = value;
  const history = useRef<{ past: T[]; future: T[]; snapshot: T }>({ past: [], future: [], snapshot: value });
  const timer = useRef<ReturnType<typeof setTimeout>>();
  const [, refresh] = useState(0);
  const cancelTimer = useCallback(() => { clearTimeout(timer.current); timer.current = undefined; }, []);

  useEffect(() => {
    const h = history.current;
    if (callbacks.current.equal(value, h.snapshot)) {
      h.snapshot = value;
      return;
    }
    h.future = [];
    cancelTimer();
    timer.current = setTimeout(() => {
      timer.current = undefined;
      h.past = [...h.past, h.snapshot].slice(-max);
      h.snapshot = value;
      refresh(n => n + 1);
    }, debounceMs);
    return cancelTimer;
  }, [value, debounceMs, max, cancelTimer]);

  const restore = useCallback((next: T) => {
    history.current.snapshot = next;
    latest.current = next;
    callbacks.current.setValue(next);
    callbacks.current.onRestore?.(next);
    refresh(n => n + 1);
  }, []);
  const undo = useCallback(() => {
    const h = history.current;
    const current = latest.current;
    const pending = !callbacks.current.equal(current, h.snapshot);
    if (!pending && h.past.length === 0) return;
    cancelTimer();
    const previous = pending ? h.snapshot : h.past.pop()!;
    h.future = pending ? [current] : [current, ...h.future];
    restore(previous);
  }, [cancelTimer, restore]);
  const redo = useCallback(() => {
    const h = history.current;
    if (!h.future.length || !callbacks.current.equal(latest.current, h.snapshot)) return;
    cancelTimer();
    h.past = [...h.past, latest.current].slice(-max);
    restore(h.future.shift()!);
  }, [cancelTimer, max, restore]);
  const clearHistory = useCallback((next: T = latest.current) => {
    cancelTimer();
    history.current = { past: [], future: [], snapshot: next };
    latest.current = next;
    refresh(n => n + 1);
  }, [cancelTimer]);
  const pending = !callbacks.current.equal(value, history.current.snapshot);
  return { undo, redo, canUndo: pending || history.current.past.length > 0,
    canRedo: !pending && history.current.future.length > 0, clearHistory };
}
