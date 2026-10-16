/** Brief §35: event visual categories, mapped from the real `EventKind` wire names
 * (`cpp/simulation/Event.hpp::toString(EventKind)`). A pure lookup, not a computation. */

export type EventCategory = 'environmental' | 'emergency' | 'mission' | 'planning' | 'success' | 'failure';

const CATEGORY_BY_KIND: Record<string, EventCategory> = {
  DEBRIS_ALERT: 'environmental',
  SATELLITE_FAILURE: 'emergency',
  PROPULSION_FAILURE: 'emergency',
  COMMUNICATION_LOSS: 'emergency',
  FUEL_CRITICAL: 'emergency',
  EMERGENCY_ESCALATION: 'emergency',
  VEHICLE_STATE_CHANGE: 'emergency',
  MISSION_START: 'mission',
  RENDEZVOUS: 'mission',
  RESCUE_START: 'mission',
  MISSION_REPLAN: 'planning',
  RESCUE_COMPLETE: 'success',
  EMERGENCY_RESOLVED: 'success',
  MISSION_FAILURE: 'failure',
};

const GLYPH_BY_CATEGORY: Record<EventCategory, string> = {
  environmental: '☄',
  emergency: '⚠',
  mission: '➤',
  planning: '↻',
  success: '✓',
  failure: '✕',
};

export function eventCategory(kind: string): EventCategory {
  return CATEGORY_BY_KIND[kind] ?? 'mission';
}

export function eventGlyph(kind: string): string {
  return GLYPH_BY_CATEGORY[eventCategory(kind)];
}
