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
//   - 連打時は前のアニメーションを `.cancel()` するだけで即座にやり直せる
//   - keyframe は事前に const 化されているのでオブジェクト生成コストもゼロ
//   - cell の box-shadow と overlay の opacity を並列に走らせ、compositor で
//     合成されるため負荷も低い

interface PadFlashTarget {
  cellElement: HTMLElement | null;
  overlayElement: HTMLElement | null;
  cellAnim?: Animation;
  overlayAnim?: Animation;
}

const padFlashTargets = new Map<number, Map<symbol, PadFlashTarget>>();

const FLASH_DURATION_MS = 320;

// 再利用される keyframe (毎フレーム allocate しない)
const cellKeyframes: Keyframe[] = [
  {
    offset: 0,
    boxShadow:
      'inset 0 0 0 1px rgba(160, 210, 240, 0.70),' +
      '0 0 0 2px rgba(82, 200, 232, 0.55),' +
      '0 0 14px rgba(82, 200, 232, 0.60),' +
      '0 0 28px rgba(82, 200, 232, 0.25)',
  },
  {
    offset: 0.30,
    boxShadow:
      'inset 0 0 0 1px rgba(160, 210, 240, 0.40),' +
      '0 0 0 1px rgba(82, 200, 232, 0.28),' +
      '0 0 10px rgba(82, 200, 232, 0.30),' +
      '0 0 18px rgba(82, 200, 232, 0.10)',
  },
  {
    offset: 1,
    boxShadow:
      '0 0 0 1px rgba(191, 163, 106, 0.06),' +
      '0 0 4px rgba(82, 200, 232, 0.04)',
  },
];

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
 * - `cellElement`: cell 本体 (box-shadow を animate する)
 * - `overlayElement`: 内側 overlay (opacity を animate する)
 *
 * unmount 時に呼ばれる cleanup 関数を返す。
 */
export function registerPadFlashTarget(
  index: number,
  cellElement: HTMLElement | null,
  overlayElement: HTMLElement | null,
) {
  const token = Symbol('pad-flash-target');
  let targets = padFlashTargets.get(index);
  if (!targets) {
    targets = new Map();
    padFlashTargets.set(index, targets);
  }

  const target: PadFlashTarget = { cellElement, overlayElement };
  targets.set(token, target);

  return () => {
    target.cellAnim?.cancel();
    target.overlayAnim?.cancel();
    targets?.delete(token);
    if (targets?.size === 0) padFlashTargets.delete(index);
  };
}

export function triggerPadFlash(index: number) {
  const targets = padFlashTargets.get(index);
  if (!targets) return;

  targets.forEach(target => {
    // 連打: 前のアニメーションを即座にキャンセル
    target.cellAnim?.cancel();
    target.overlayAnim?.cancel();

    if (target.cellElement) {
      target.cellAnim = target.cellElement.animate(cellKeyframes, animOptions);
    }
    if (target.overlayElement) {
      target.overlayAnim = target.overlayElement.animate(overlayKeyframes, animOptions);
    }
  });
}
