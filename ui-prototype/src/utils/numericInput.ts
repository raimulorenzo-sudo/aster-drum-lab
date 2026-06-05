export function parseNumericText(text: string): number | null {
  const normalized = text
    .trim()
    .toLowerCase()
    .replace(',', '.')
    .replace(/\s+/g, '')
    .replace(/db|ms|%|st|semitones?|l|r|c/g, '');

  if (normalized === '' || normalized === '+') return null;
  if (normalized === '-inf' || normalized === 'inf') return -Infinity;

  const value = Number(normalized);
  return Number.isFinite(value) || value === -Infinity ? value : null;
}

export function parsePercentInput(text: string): number | null {
  const value = parseNumericText(text);
  if (value === null || !Number.isFinite(value)) return value;
  return value / 100;
}

export function parsePanInput(text: string): number | null {
  const trimmed = text.trim().toLowerCase();
  if (trimmed === 'c' || trimmed === 'center' || trimmed === 'centre') return 0;

  const value = parseNumericText(trimmed);
  if (value === null || !Number.isFinite(value)) return value;

  if (/^\s*l/i.test(text)) return -Math.abs(value) / 100;
  if (/^\s*r/i.test(text)) return Math.abs(value) / 100;
  return value / 100;
}
