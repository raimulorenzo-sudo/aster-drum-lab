import type { MouseEvent, FocusEvent } from 'react';
import styles from './OverflowMarquee.module.css';

interface OverflowMarqueeProps {
  text: string;
  className?: string;
  title?: string;
  disabled?: boolean;
}

function prepareMarquee(label: HTMLElement, disabled: boolean) {
  const text = label.firstElementChild as HTMLElement | null;
  if (!text || disabled) {
    label.dataset.overflow = 'false';
    return;
  }

  const distance = Math.max(0, text.scrollWidth - label.clientWidth);
  label.dataset.overflow = distance > 1 ? 'true' : 'false';
  label.style.setProperty('--marquee-distance', `${distance}px`);
  label.style.setProperty('--marquee-duration', `${Math.min(9, Math.max(3.5, 2 + distance / 22))}s`);
}

export function OverflowMarquee({
  text,
  className = '',
  title,
  disabled = false,
}: OverflowMarqueeProps) {
  const measure = (event: MouseEvent<HTMLSpanElement> | FocusEvent<HTMLSpanElement>) => {
    prepareMarquee(event.currentTarget, disabled);
  };

  return (
    <span
      className={`${styles.root} ${className}`}
      title={title}
      onMouseEnter={measure}
      onFocus={measure}
    >
      <span className={styles.text}>{text}</span>
    </span>
  );
}
