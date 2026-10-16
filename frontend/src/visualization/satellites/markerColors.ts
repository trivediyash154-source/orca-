/** Shared between `EntityMarkerField` (instanced) and anywhere else a marker's status needs
 * a colour -- one table, not duplicated per component (the same "one documented conversion"
 * discipline `coordinates.ts` established for position). */
export const STATUS_COLORS: Record<string, string> = {
  NOMINAL: '#45b787',
  IDLE: '#45b787',
  WARNING: '#d6a344',
  ASSIGNED: '#4a90d6',
  TRANSFERRING: '#4ac9d6',
  ONSTATION: '#4ac9d6',
  RETURNING: '#4ac9d6',
  CRITICAL: '#d6544a',
  FAILED: '#d6544a',
  RESCUED: '#45b787',
  LOST: '#5a6a80',
  DISABLED: '#5a6a80',
};

export const CRITICAL_STATUSES = new Set(['CRITICAL', 'FAILED']);

export const DEFAULT_MARKER_COLOR = '#7f8fa6';
