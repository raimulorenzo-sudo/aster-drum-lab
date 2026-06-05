import { useMemo, useState } from 'react';
import { Dropdown } from '../Dropdown/Dropdown';
import {
  isMoreOutputValue,
  hiddenOutputAssignLabel,
  moreOutputOptions,
  outputAssignLabel,
  primaryOutputOptions,
} from '../../utils/outputRouting';

type OutputAssignDropdownProps = {
  value: number;
  outputCount?: number;
  onChange: (value: number) => void;
  width?: number | string;
  compact?: boolean;
};

export function OutputAssignDropdown({
  value,
  outputCount = 48,
  onChange,
  width,
  compact = false,
}: OutputAssignDropdownProps) {
  const [showMore, setShowMore] = useState(false);
  const options = useMemo(
    () => (showMore ? moreOutputOptions(outputCount) : primaryOutputOptions(outputCount)),
    [outputCount, showMore],
  );
  const isHiddenOutput = value >= outputCount;
  const selectedLabel = isHiddenOutput ? hiddenOutputAssignLabel(value) : outputAssignLabel(value);

  return (
    <Dropdown<number>
      width={width}
      compact={compact}
      value={value}
      selectedLabel={selectedLabel}
      options={options}
      title={isHiddenOutput ? `OUT ${value + 1} is hidden in the current Output Mode` : undefined}
      onOpenChange={(open) => {
        if (open) setShowMore(false);
      }}
      onChange={(nextValue) => {
        if (isMoreOutputValue(nextValue)) {
          setShowMore(true);
          return false;
        }
        onChange(nextValue);
      }}
    />
  );
}
