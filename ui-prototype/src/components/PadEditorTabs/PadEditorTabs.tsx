import { memo, useState } from 'react';
import styles from './PadEditorTabs.module.css';
import { TrimTab } from './tabs/TrimTab';
import { PlaybackTab } from './tabs/PlaybackTab';
import { PadTab } from './tabs/PadTab';
import { FxTab } from './tabs/FxTab';
import { selectedLayerIndexOf } from '../../utils/layerView';
import type { PadParams } from '../../types';

export type PadEditorTabId = 'TRIM' | 'PLAYBACK' | 'PAD' | 'FX';

const TAB_IDS: PadEditorTabId[] = ['TRIM', 'PLAYBACK', 'PAD', 'FX'];

interface Props {
  pad: PadParams;
  padIndex: number;
  onChange: (patch: Partial<PadParams>) => void;
}

function PadEditorTabsComponent({ pad, padIndex, onChange }: Props) {
  const [active, setActive] = useState<PadEditorTabId>('TRIM');
  const layerIdx = selectedLayerIndexOf(pad);

  return (
    <div className={styles.root}>
      <div className={styles.tabBar} role="tablist" aria-label="Pad editor tabs">
        {TAB_IDS.map(id => (
          <button
            key={id}
            role="tab"
            aria-selected={active === id}
            className={`${styles.tab} ${active === id ? styles.tabActive : ''}`}
            onClick={() => setActive(id)}
          >
            {id}
          </button>
        ))}
        <div className={styles.spacer} />
        <div className={styles.selectionBadge}>
          <span>Selected Pad:</span>
          <b>{pad.padName || `P${String(padIndex + 1).padStart(2, '0')}`}</b>
          <span className={styles.layerChip}>L{layerIdx + 1}</span>
        </div>
      </div>

      <div className={styles.body} data-accent={layerIdx >= 1 ? 'gold' : undefined}>
        {active === 'TRIM'     && <TrimTab     pad={pad} padIndex={padIndex} onChange={onChange} />}
        {active === 'PLAYBACK' && <PlaybackTab pad={pad} padIndex={padIndex} onChange={onChange} />}
        {active === 'PAD'      && <PadTab      pad={pad} padIndex={padIndex} onChange={onChange} />}
        {active === 'FX'       && <FxTab       pad={pad}                     onChange={onChange} />}
      </div>
    </div>
  );
}

export const PadEditorTabs = memo(PadEditorTabsComponent);
