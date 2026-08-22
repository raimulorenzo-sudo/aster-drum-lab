import { useCallback, useRef } from 'react';
import styles from './EqTab.module.css';
import { EqBox } from '../EqBox';
import type { EqParams, PadParams } from '../../../types';
import { NEUTRAL_EQ } from '../../../types';
import { selectedLayerIndexOf } from '../../../utils/layerView';

interface Props {
  pad: PadParams;
  padIndex: number;
  onChange: (patch: Partial<PadParams>) => void;
}

function cloneEq(eq: EqParams): EqParams {
  return {
    bypassed: eq.bypassed,
    lowMode: eq.lowMode === 'cut' ? 'cut' : 'shelf',
    highMode: eq.highMode === 'cut' ? 'cut' : 'shelf',
    low: { ...eq.low },
    lowMid: { ...eq.lowMid },
    highMid: { ...eq.highMid },
    high: { ...eq.high },
  };
}

function eqOfSelectedLayer(pad: PadParams): EqParams {
  const layer = pad.layers?.[selectedLayerIndexOf(pad)];
  if (layer?.eq) return layer.eq;

  const legacyEq = layer?.fxChain?.find(slot => slot.type === 'EQ')?.params;
  if (legacyEq && 'low' in legacyEq) return legacyEq as EqParams;

  return cloneEq(NEUTRAL_EQ);
}

export function EqTab({ pad, padIndex, onChange }: Props) {
  const eq = eqOfSelectedLayer(pad);

  const stateRef = useRef<{ eq: EqParams; onChange: (p: Partial<PadParams>) => void }>({
    eq,
    onChange,
  });
  stateRef.current = { eq, onChange };

  const patchEq = useCallback((updater: (eq: EqParams) => EqParams) => {
    const { eq, onChange } = stateRef.current;
    onChange({ eq: updater(cloneEq(eq)) });
  }, []);

  return (
    <div className={styles.root}>
      <div className={styles.chain}>
        <EqBox eq={eq} patchEq={patchEq} padIndex={padIndex} layerIndex={selectedLayerIndexOf(pad)} />
      </div>
    </div>
  );
}
