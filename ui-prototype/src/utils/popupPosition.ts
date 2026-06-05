import type { CSSProperties } from 'react';

const DEFAULT_MARGIN = 8;
const DEFAULT_GAP = 4;
const MIN_SCROLL_HEIGHT = 96;

export interface PopupSize {
  width: number;
  height: number;
}

export interface PopupBounds {
  left: number;
  top: number;
  right: number;
  bottom: number;
}

export function getPopupBounds(margin = DEFAULT_MARGIN): PopupBounds {
  return {
    left: margin,
    top: margin,
    right: Math.max(margin, window.innerWidth - margin),
    bottom: Math.max(margin, window.innerHeight - margin),
  };
}

export function measurePopup(
  element: HTMLElement | null,
  fallback: PopupSize,
): PopupSize {
  if (!element) return fallback;
  const rect = element.getBoundingClientRect();
  return {
    width: Math.max(1, rect.width || fallback.width),
    height: Math.max(1, rect.height || fallback.height),
  };
}

function clamp(value: number, min: number, max: number): number {
  if (max < min) return min;
  return Math.min(Math.max(value, min), max);
}

function styleFromBox(left: number, top: number, width: number, maxHeight?: number): CSSProperties {
  return {
    position: 'fixed',
    left,
    top,
    width,
    maxHeight,
  };
}

export function positionPopupFromPoint(
  x: number,
  y: number,
  size: PopupSize,
  options: {
    margin?: number;
    gap?: number;
    minHeight?: number;
    width?: number;
  } = {},
): CSSProperties {
  const margin = options.margin ?? DEFAULT_MARGIN;
  const gap = options.gap ?? DEFAULT_GAP;
  const bounds = getPopupBounds(margin);
  const width = Math.min(options.width ?? size.width, Math.max(1, bounds.right - bounds.left));
  const spaceBelow = bounds.bottom - y - gap;
  const spaceAbove = y - bounds.top - gap;
  const placeAbove = spaceBelow < size.height && spaceAbove > spaceBelow;
  const availableHeight = Math.max(
    options.minHeight ?? MIN_SCROLL_HEIGHT,
    placeAbove ? spaceAbove : spaceBelow,
  );
  const maxHeight = Math.min(size.height, availableHeight);
  const top = placeAbove
    ? clamp(y - gap - maxHeight, bounds.top, bounds.bottom - maxHeight)
    : clamp(y + gap, bounds.top, bounds.bottom - maxHeight);
  const left = clamp(x, bounds.left, bounds.right - width);

  return styleFromBox(left, top, width, maxHeight);
}

export function positionPopupFromAnchor(
  anchorRect: DOMRect,
  size: PopupSize,
  options: {
    margin?: number;
    gap?: number;
    align?: 'start' | 'end';
    minWidth?: number;
    width?: number;
    minHeight?: number;
  } = {},
): CSSProperties {
  const margin = options.margin ?? DEFAULT_MARGIN;
  const gap = options.gap ?? DEFAULT_GAP;
  const bounds = getPopupBounds(margin);
  const width = Math.min(
    Math.max(options.width ?? anchorRect.width, options.minWidth ?? 1),
    Math.max(1, bounds.right - bounds.left),
  );
  const spaceBelow = bounds.bottom - anchorRect.bottom - gap;
  const spaceAbove = anchorRect.top - bounds.top - gap;
  const placeAbove = spaceBelow < size.height && spaceAbove > spaceBelow;
  const availableHeight = Math.max(
    options.minHeight ?? MIN_SCROLL_HEIGHT,
    placeAbove ? spaceAbove : spaceBelow,
  );
  const maxHeight = Math.min(size.height, availableHeight);
  const top = placeAbove
    ? clamp(anchorRect.top - gap - maxHeight, bounds.top, bounds.bottom - maxHeight)
    : clamp(anchorRect.bottom + gap, bounds.top, bounds.bottom - maxHeight);
  const rawLeft = options.align === 'end'
    ? anchorRect.right - width
    : anchorRect.left;
  const left = clamp(rawLeft, bounds.left, bounds.right - width);

  return styleFromBox(left, top, width, maxHeight);
}

export function positionSubmenuFromAnchor(
  anchorRect: DOMRect,
  size: PopupSize,
  options: {
    margin?: number;
    gap?: number;
    width?: number;
    minHeight?: number;
  } = {},
): CSSProperties {
  const margin = options.margin ?? DEFAULT_MARGIN;
  const gap = options.gap ?? 2;
  const bounds = getPopupBounds(margin);
  const width = Math.min(options.width ?? size.width, Math.max(1, bounds.right - bounds.left));
  const spaceRight = bounds.right - anchorRect.right - gap;
  const spaceLeft = anchorRect.left - bounds.left - gap;
  const placeLeft = spaceRight < width && spaceLeft > spaceRight;
  const left = placeLeft
    ? clamp(anchorRect.left - gap - width, bounds.left, bounds.right - width)
    : clamp(anchorRect.right + gap, bounds.left, bounds.right - width);
  const spaceBelow = bounds.bottom - anchorRect.top;
  const spaceAbove = anchorRect.bottom - bounds.top;
  const availableHeight = Math.max(
    options.minHeight ?? MIN_SCROLL_HEIGHT,
    spaceBelow >= Math.min(size.height, spaceAbove) ? spaceBelow : spaceAbove,
  );
  const maxHeight = Math.min(size.height, availableHeight);
  const top = clamp(anchorRect.top, bounds.top, bounds.bottom - maxHeight);

  return styleFromBox(left, top, width, maxHeight);
}
