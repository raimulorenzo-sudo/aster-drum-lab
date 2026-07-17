// パッド発音時のフラッシュ演出を管理するレジストリ。
//
// 重要な設計判断:
//   旧実装は `element.classList.remove → void offsetWidth → classList.add`
//   というパターンで CSS アニメーションを再スタートさせていた。
//   この `offsetWidth` 読み出しが **強制同期リフロー** を起こすため、
//   ドラム楽曲で複数パッドが同時にヒットする度に「カクッ」と止まる原因に。
//
//   新実装は Web Animations API (`element.animate()`) を使い、
//   - 強制リフローを起こさない
//   - 連打時は同じ Animation を先頭から再生し直すので、ヒットごとの Animation
//     生成と GC を避けられる
//   - keyframe は事前に const 化されているのでオブジェクト生成コストもゼロ
//   - overlay の opacity だけを動かし、paint が必要な box-shadow animation を避ける

interface PadFlashTarget {
  overlayElement: HTMLElement | null;
  overlayAnim?: Animation;
}

const padFlashTargets = new Map<number, Map<symbol, PadFlashTarget>>();

const FLASH_DURATION_MS = 320;

const overlayKeyframes: Keyframe[] = [
  { offset: 0,    opacity: 1 },
  { offset: 0.40, opacity: 0.36 },
  { offset: 1,    opacity: 0 },
];

const animOptions: KeyframeAnimationOptions = {
  duration: FLASH_DURATION_MS,
  easing: 'ease-out',
  fill: 'none',
};

/**
 * Pad のフラッシュターゲットを登録する。
 * - `overlayElement`: 内側 overlay (opacity を animate する)
 *
 * unmount 時に呼ばれる cleanup 関数を返す。
 */
export function registerPadFlashTarget(
  index: number,
  overlayElement: HTMLElement | null,
) {
  const token = Symbol('pad-flash-target');
  let targets = padFlashTargets.get(index);
  if (!targets) {
    targets = new Map();
    padFlashTargets.set(index, targets);
  }

  const target: PadFlashTarget = { overlayElement };
  targets.set(token, target);

  return () => {
    target.overlayAnim?.cancel();
    targets?.delete(token);
    if (targets?.size === 0) padFlashTargets.delete(index);
  };
}

export function triggerPadFlash(index: number) {
  const targets = padFlashTargets.get(index);
  if (!targets) return;

  targets.forEach(target => {
    if (!target.overlayElement) return;

    if (!target.overlayAnim) {
      target.overlayAnim = target.overlayElement.animate(overlayKeyframes, animOptions);
      return;
    }

    // Hit-dense patterns can retrigger this many times per second. Reusing the
    // existing compositor animation avoids allocating a new Animation each hit.
    target.overlayAnim.currentTime = 0;
    target.overlayAnim.play();
  });
}
