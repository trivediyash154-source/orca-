/** A minimal CSV parser for the real `orca_engine --bench` output
 * (`cpp/analytics/CsvWriter.cpp`) -- no quoted/embedded-comma fields exist in that format
 * (every field is a plain number, algorithm name, or scenario family identifier), so a
 * simple split is honest here, not a shortcut around a real edge case. Empty cells (the
 * writer's own "N/A, not zero" convention for a field that does not apply to a given
 * algorithm) parse to `undefined` via `row(field)`. */
export interface CsvTable {
  readonly rows: ReadonlyArray<Record<string, string>>;
}

export function parseCsv(text: string): CsvTable {
  const lines = text.trim().split('\n');
  if (lines.length === 0) return { rows: [] };
  const header = lines[0]!.split(',');
  const rows = lines.slice(1).map((line) => {
    const cells = line.split(',');
    const row: Record<string, string> = {};
    header.forEach((key, i) => {
      row[key] = cells[i] ?? '';
    });
    return row;
  });
  return { rows };
}

export function numField(row: Record<string, string>, key: string): number | undefined {
  const v = row[key];
  if (v === undefined || v === '') return undefined;
  const n = Number(v);
  return Number.isFinite(n) ? n : undefined;
}

export function mean(values: readonly number[]): number {
  return values.length === 0 ? 0 : values.reduce((a, b) => a + b, 0) / values.length;
}
