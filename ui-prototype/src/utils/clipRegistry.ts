/**
 * Per-pad Clip latch registry.
 *
 * C++ 側 (VoiceManager::padClipLatched) を真実の状態として、levelData broadcast
 * (`padClips: bool[]`) を受けて各チャンネルストリップの Clip LED に反映する。
 *
 * meterRegistry と同じく React state を経由せず DOM 直接更新する設計。
 * Mixer タブで 16 ch 同時描画してもメインスレッド負荷を増やさない。
 */
const padClipNodes = new Map<number, Set<HTMLElement>>();

export function registerPadClipNode(index: number, el: HTMLElement | null): () => void {
  if (!el) return () => {};
  let set = padClipNodes.get(index);
  if (!set) {
    set = new Set<HTMLElement>();
    padClipNodes.set(index, set);
  }
  set.add(el);
  return () => {
    const cur = padClipNodes.get(index);
    if (!cur) return;
    cur.delete(el);
    if (cur.size === 0) padClipNodes.delete(index);
  };
}

/** padIndex の Clip LED 表示を更新。 */
export function updatePadClipLatch(index: number, latched: boolean): void {
  const set = padClipNodes.get(index);
  if (!set) return;
  set.forEach(el => {
    if (el.dataset.clip !== (latched ? 'true' : 'false')) {
      el.dataset.clip = latched ? 'true' : 'false';
    }
  });
}

/** 一括クリア (Kit 切替等で使う想定)。 */
export function clearAllPadClipNodes(): void {
  padClipNodes.forEach(set => set.forEach(el => { el.dataset.clip = 'false'; }));
}
