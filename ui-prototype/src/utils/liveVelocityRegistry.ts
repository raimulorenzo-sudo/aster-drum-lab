// Live MIDI velocity is visual-only feedback for the open Velocity Range panel.
// Keeping it outside App state prevents every drum hit from reconciling the
// whole plugin UI while the transport is running.

type Listener = (velocity: number | null) => void;

const listenersByPad = new Map<number, Set<Listener>>();
const clearTimers = new Map<number, number>();

function notify(padIndex: number, velocity: number | null) {
  listenersByPad.get(padIndex)?.forEach(listener => listener(velocity));
}

export function registerLiveVelocityListener(padIndex: number, listener: Listener): () => void {
  let listeners = listenersByPad.get(padIndex);
  if (!listeners) {
    listeners = new Set<Listener>();
    listenersByPad.set(padIndex, listeners);
  }
  listeners.add(listener);

  return () => {
    const current = listenersByPad.get(padIndex);
    if (!current) return;
    current.delete(listener);
    if (current.size > 0) return;

    listenersByPad.delete(padIndex);
    const timer = clearTimers.get(padIndex);
    if (timer !== undefined) {
      window.clearTimeout(timer);
      clearTimers.delete(padIndex);
    }
  };
}

export function publishLiveVelocity(padIndex: number, velocity: number): void {
  if (!listenersByPad.has(padIndex)) return;

  notify(padIndex, velocity);
  const existingTimer = clearTimers.get(padIndex);
  if (existingTimer !== undefined) window.clearTimeout(existingTimer);

  clearTimers.set(padIndex, window.setTimeout(() => {
    clearTimers.delete(padIndex);
    notify(padIndex, null);
  }, 700));
}
