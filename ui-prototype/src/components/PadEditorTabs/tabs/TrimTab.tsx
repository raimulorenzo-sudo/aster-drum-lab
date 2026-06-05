import shared from '../../PadControlSections/PadControlSections.module.css';
import { Knob } from '../../Knob/Knob';
import { defaultPadParam } from '../../../data/parameterSpecs';
import { formatMs, formatTrimPercent } from '../../../utils/parameterFormat';
import { trimFromPad } from '../../../utils/sampleTrim';
import { parseNumericText, parsePercentInput } from '../../../utils/numericInput';
import type { PadParams } from '../../../types';

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

  return (
    <div className={shared.knobGrid4} style={{ maxWidth: 540 }}>
      <Knob ownerKey={ownerKey} size={56} label="START" value={trim.startMs / totalMs} defaultValue={0}
        valueText={formatTrimPercent(trim.startMs, totalMs)}
        parseInput={text => {
          const parsed = text.includes('%') ? parsePercentInput(text) : parseNumericText(text);
          return parsed === null ? null : (text.includes('%') ? parsed : parsed / Math.max(1, totalMs));
        }}
        onChange={v => onChange({ startMs: v * totalMs })}
        onReset={() => onChange({ startMs: defaultPadParam('startMs') })} />
      <Knob ownerKey={ownerKey} size={56} label="END" value={trim.endMs / totalMs} defaultValue={1}
        valueText={formatTrimPercent(trim.endMs, totalMs)}
        parseInput={text => {
          const parsed = text.includes('%') ? parsePercentInput(text) : parseNumericText(text);
          return parsed === null ? null : (text.includes('%') ? parsed : parsed / Math.max(1, totalMs));
        }}
        onChange={v => onChange({ endMs: v * totalMs })}
        onReset={() => onChange({ endMs: trim.sampleLengthMs })} />
      <Knob ownerKey={ownerKey} size={56} label="FADE IN" value={playbackRangeMs > 0 ? trim.fadeInMs / playbackRangeMs : 0} defaultValue={0}
        valueText={formatMs(trim.fadeInMs)}
        parseInput={text => {
          const parsed = parseNumericText(text);
          return parsed === null ? null : parsed / playbackRangeMs;
        }}
        onChange={v => onChange({ fadeInMs: v * playbackRangeMs })}
        onReset={() => onChange({ fadeInMs: defaultPadParam('fadeInMs') })} />
      <Knob ownerKey={ownerKey} size={56} label="FADE OUT" value={playbackRangeMs > 0 ? trim.fadeOutMs / playbackRangeMs : 0} defaultValue={0}
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
