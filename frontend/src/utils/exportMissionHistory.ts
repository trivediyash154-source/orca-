/** Client-side export of the CURRENT in-memory mission history -- Phase 11 brief §60's
 * "mission history export" request. No new data: exactly `snapshot.missions`, the same
 * fields `MissionHistoryPage`'s table already renders (`MissionHistory.tsx`'s own honesty
 * note applies here too -- session-only, never a fabricated persistence layer). */

import type { MissionView } from '../types/engine';

const CSV_COLUMNS = ['id', 'vehicleId', 'emergencyId', 'state', 'startedAtSec', 'completedAtSec', 'replanCount'] as const;

function csvEscape(value: string): string {
  return /[",\n]/.test(value) ? `"${value.replace(/"/g, '""')}"` : value;
}

export function missionsToCsv(missions: readonly MissionView[]): string {
  const header = CSV_COLUMNS.join(',');
  const rows = missions.map((m) =>
    CSV_COLUMNS.map((col) => csvEscape(String(m[col] ?? ''))).join(','),
  );
  return [header, ...rows].join('\n') + '\n';
}

export function missionsToJson(missions: readonly MissionView[]): string {
  return JSON.stringify(missions, null, 2);
}

/** Triggers a real browser download of `content` -- no server round-trip, since everything
 * being exported already lives in the frontend's own received snapshot. */
export function downloadTextFile(filename: string, content: string, mimeType: string): void {
  const blob = new Blob([content], { type: mimeType });
  const url = URL.createObjectURL(blob);
  const link = document.createElement('a');
  link.href = url;
  link.download = filename;
  document.body.appendChild(link);
  link.click();
  document.body.removeChild(link);
  URL.revokeObjectURL(url);
}
