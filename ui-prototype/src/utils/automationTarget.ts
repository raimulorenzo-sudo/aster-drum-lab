export interface AutomationTarget {
  id: string;
  name: string;
}

const padToken = (padIndex: number) => String(padIndex + 1).padStart(2, '0');
const layerToken = (layerIndex: number) => String(layerIndex + 1).padStart(2, '0');

export function padAutomationTarget(
  padIndex: number,
  suffix: string,
  name: string,
): AutomationTarget {
  return {
    id: `pad${padToken(padIndex)}.${suffix}`,
    name: `Pad ${padToken(padIndex)} ${name}`,
  };
}

export function layerAutomationTarget(
  padIndex: number,
  layerIndex: number,
  suffix: string,
  name: string,
): AutomationTarget {
  return {
    id: `pad${padToken(padIndex)}.layer${layerToken(layerIndex)}.${suffix}`,
    name: `Pad ${padToken(padIndex)} L${layerToken(layerIndex)} ${name}`,
  };
}

export function fxAutomationTarget(
  padIndex: number,
  layerIndex: number,
  fxType: string,
  parameter: string,
  name: string,
): AutomationTarget {
  const type = fxType.toLowerCase();
  return {
    id: `pad${padToken(padIndex)}.layer${layerToken(layerIndex)}.fx.${type}.${parameter}`,
    name: `Pad ${padToken(padIndex)} L${layerToken(layerIndex)} ${fxType} ${name}`,
  };
}

export const masterAutomationTarget: AutomationTarget = {
  id: 'masterVolume',
  name: 'Master Volume',
};
