import styles from './IconButton.module.css';

interface IconButtonProps {
  onClick?: () => void;
  active?: boolean;
  title?: string;
  children: React.ReactNode;
  variant?: 'default' | 'subtle';
}

export function IconButton({
  onClick,
  active,
  title,
  children,
  variant = 'default',
}: IconButtonProps) {
  return (
    <button
      className={`${styles.btn} ${active ? styles.active : ''} ${variant === 'subtle' ? styles.subtle : ''}`}
      onClick={onClick}
      title={title}
    >
      {children}
    </button>
  );
}
