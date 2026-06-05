const PRIMARY_OUTPUT_COUNT = 16;
const MORE_OUTPUT_VALUE = -1;

export type OutputOption = { value: number; label: string };

export function outputAssignLabel(index: number): string {
  return `${index * 2 + 1}-${index * 2 + 2}`;
}

export function hiddenOutputAssignLabel(index: number): string {
  return `${index * 2 + 1}-${index * 2 + 2}*`;
}

export function primaryOutputOptions(outputCount: number): OutputOption[] {
  const count = Math.max(0, Math.min(PRIMARY_OUTPUT_COUNT, outputCount));
  const options = Array.from({ length: count }, (_, i) => ({
    value: i,
    label: outputAssignLabel(i),
  }));

  if (outputCount > PRIMARY_OUTPUT_COUNT) {
    options.push({ value: MORE_OUTPUT_VALUE, label: 'More...' });
  }

  return options;
}

export function moreOutputOptions(outputCount: number): OutputOption[] {
  const count = Math.max(0, outputCount - PRIMARY_OUTPUT_COUNT);
  return Array.from({ length: count }, (_, i) => {
    const value = i + PRIMARY_OUTPUT_COUNT;
    return { value, label: outputAssignLabel(value) };
  });
}

export function isMoreOutputValue(value: number): boolean {
  return value === MORE_OUTPUT_VALUE;
}
