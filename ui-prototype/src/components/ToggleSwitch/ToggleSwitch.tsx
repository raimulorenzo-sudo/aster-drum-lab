import styles from './ToggleSwitch.module.css';

interface ToggleSwitchProps {
  on: boolean;
  onChange: (next: boolean) => void;
  label?: string;
  /** 左に小さなドット型インジケータを出すか (Smart Trim 表示用) */
  withIndicator?: boolean;
  size?: 'sm' | 'md';
}

export function ToggleSwitch({
  on,
  onChange,
  label,
  withIndicator,
  size = 'md',
}: ToggleSwitchProps) {
  return (
    <button
      className={`${styles.toggle} ${on ? styles.on : ''} ${size === 'sm' ? styles.sm : ''}`}
      onClick={() => onChange(!on)}
    >
      {withIndicator && (
        <span className={`${styles.dot} ${on ? styles.dotOn : ''}`} aria-hidden />
      )}
      {label && <span className={styles.text}>{label}</span>}
      <span className={styles.track}>
        <span className={styles.thumb} />
      </span>
    </button>
  );
}
