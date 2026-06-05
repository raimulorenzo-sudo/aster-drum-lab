import styles from './TabBar.module.css';

export type TabId = 'PADS' | 'MIXER' | 'MISSING';

interface TabBarProps {
  active: TabId;
  onChange: (t: TabId) => void;
  missingCount?: number;
}

const TABS: TabId[] = ['PADS', 'MIXER', 'MISSING'];

export function TabBar({ active, onChange, missingCount = 0 }: TabBarProps) {
  const visibleTabs = missingCount > 0 ? TABS : TABS.filter(t => t !== 'MISSING');

  return (
    <nav className={styles.tabBar}>
      {visibleTabs.map(t => (
        <button
          key={t}
          className={`${styles.tab} ${active === t ? styles.tabActive : ''}`}
          onClick={() => onChange(t)}
        >
          {t}
          {t === 'MISSING' && missingCount > 0 && (
            <span className={`${styles.badge} ${active === t ? styles.badgeActive : ''}`}>
              {missingCount}
            </span>
          )}
        </button>
      ))}
      <div className={styles.spacer} />
    </nav>
  );
}
