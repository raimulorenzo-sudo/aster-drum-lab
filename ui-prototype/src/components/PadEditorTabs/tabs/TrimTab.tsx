import shared from '../../PadControlSections/PadControlSections.module.css';
import { Knob } from '../../Knob/Knob';
import { defaultPadParam } from '../../../data/parameterSpecs';
import { formatMs, formatTrimPercent } from '../../../utils/parameterFormat';
import { trimFromPad } from '../../../utils/sampleTrim';
import { parseNumericText, parsePercentInput } from '../../../utils/numericInput';
import type { PadParams } from '../../../types';
import { selectedLayerIndexOf } from '../../../utils/layerView';
import { padAutomationTarget } from '../../../utils/automationTarget';

interface Props {
  pad: PadParams;
  padIndex: number;
  onChange: (patch: Partial<PadParams>) => void;
}

export function TrimTab({ pad, padIndex, onChange }: Props) {
  const trim = trimFromPad(pad);
  const totalMs = trim.sampleLengthMs;
  const playbackRangeMs = Math.max(1, trim.endMs - trim.startMs);
  const ownerKey = padIndex;
  const layerIdx = selectedLayerIndexOf(pad);
  const target = (suffix: string, name: string) => layerIdx === 0
    ? padAutomationTarget(padIndex, suffix, name)
    : undefined;

  return (
    <div className={shared.knobGrid4} style={{ maxWidth: 540 }}>
      <Knob ownerKey={ownerKey} size={56} label="START" value={trim.startMs / totalMs} defaultValue={0}
        automationTarget={target('start', 'Start')}
        valueText={formatTrimPercent(trim.startMs, totalMs)}
        parseInput={text => {
          const parsed = text.includes('%') ? parsePercentInput(text) : parseNumericText(text);
          return parsed === null ? null : (text.includes('%') ? parsed : parsed / Math.max(1, totalMs));
        }}
        onChange={v => onChange({ startMs: v * totalMs })}
        onReset={() => onChange({ startMs: defaultPadParam('startMs') })} />
      <Knob ownerKey={ownerKey} size={56} label="END" value={trim.endMs / totalMs} defaultValue={1}
        automationTarget={target('end', 'End')}
        valueText={formatTrimPercent(trim.endMs, totalMs)}
        parseInput={text => {
          const parsed = text.includes('%') ? parsePercentInput(text) : parseNumericText(text);
          return parsed === null ? null : (text.includes('%') ? parsed : parsed / Math.max(1, totalMs));
        }}
        onChange={v => onChange({ endMs: v * totalMs })}
        onReset={() => onChange({ endMs: trim.sampleLengthMs })} />
      <Knob ownerKey={ownerKey} size={56} label="FADE IN" value={playbackRangeMs > 0 ? trim.fadeInMs / playbackRangeMs : 0} defaultValue={0}
        automationTarget={target('fadeIn', 'Fade In')}
        valueText={formatMs(trim.fadeInMs)}
        parseInput={text => {
          const parsed = parseNumericText(text);
          return parsed === null ? null : parsed / playbackRangeMs;
        }}
        onChange={v => onChange({ fadeInMs: v * playbackRangeMs })}
        onReset={() => onChange({ fadeInMs: defaultPadParam('fadeInMs') })} />
      <Knob ownerKey={ownerKey} size={56} label="FADE OUT" value={playbackRangeMs > 0 ? trim.fadeOutMs / playbackRangeMs : 0} defaultValue={0}
        automationTarget={target('fadeOut', 'Fade Out')}
        valueText={formatMs(trim.fadeOutMs)}
        parseInput={text => {
          const parsed = parseNumericText(text);
          return parsed === null ? null : parsed / playbackRangeMs;
        }}
        onChange={v => onChange({ fadeOutMs: v * playbackRangeMs })}
        onReset={() => onChange({ fadeOutMs: defaultPadParam('fadeOutMs') })} />
    </div>
  );
}
