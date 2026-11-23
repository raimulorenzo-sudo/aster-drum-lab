import { useCallback, useLayoutEffect, useRef } from 'react';

// A pointer gesture belongs to the mounted edit target, not to the window.
// Disposing it must remove callbacks without committing to a different target.
export function usePointerDrag(ownerKey: string | number, onDispose?: () => void) {
  const cleanupRef = useRef<(() => void) | null>(null);
  const disposeRef = useRef(onDispose);
  useLayoutEffect(() => { disposeRef.current = onDispose; }, [onDispose]);
  useLayoutEffect(() => () => { cleanupRef.current?.(); }, [ownerKey]);

  return useCallback((
    event: { pointerId: number; currentTarget: Element },
    onMove: (event: PointerEvent) => void,
    onEnd?: () => void,
    capture = true,
  ) => {
    cleanupRef.current?.();
    const element = event.currentTarget;
    const pointerId = event.pointerId;
    let active = true;
    const matches = (ev: Event) => !('pointerId' in ev) || ev.pointerId === pointerId;
    const move = (ev: PointerEvent) => { if (active && matches(ev)) onMove(ev); };
    const cleanup = () => {
      if (!active) return;
      active = false;
      cleanupRef.current = null;
      window.removeEventListener('pointermove', move);
      window.removeEventListener('pointerup', up);
      window.removeEventListener('pointercancel', cancel);
      window.removeEventListener('blur', cancel);
      element.removeEventListener('lostpointercapture', cancel);
      if (capture && element.hasPointerCapture?.(pointerId)) element.releasePointerCapture(pointerId);
      disposeRef.current?.();
    };
    const end = () => { if (active) { try { onEnd?.(); } finally { cleanup(); } } };
    const up = (ev: PointerEvent) => { if (matches(ev)) { move(ev); end(); } };
    const cancel = (ev: Event) => { if (matches(ev)) end(); };
    cleanupRef.current = cleanup;
    if (capture) {
      element.setPointerCapture?.(pointerId);
      element.addEventListener('lostpointercapture', cancel);
    }
    window.addEventListener('pointermove', move);
    window.addEventListener('pointerup', up);
    window.addEventListener('pointercancel', cancel);
    window.addEventListener('blur', cancel);
    return cleanup;
  }, []);
}
