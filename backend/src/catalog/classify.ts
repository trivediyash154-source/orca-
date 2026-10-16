/**
 * The catalog classification layer — the rules behind the blue / red / grey colouring.
 *
 * ## Why this is a data table and not a set of `if`s in a component
 *
 * A colour on a mission display is a claim about an object. If the UI hardcodes "red means
 * dangerous", nobody can audit what "dangerous" meant, and the meaning drifts the moment
 * somebody edits a component. So the rules live here, ordered, each with the reason it will
 * attach to a matching object, and the frontend renders its legend FROM this table — the swatch
 * and its explanation cannot disagree, because they are the same record.
 *
 * ## Why green is not in this file
 *
 * Blue and red are properties of the object: an operational payload is operational regardless
 * of what mission is loaded. Green — "mission relevant", "candidate rescue asset", "inside the
 * planning corridor" — is a property of the object *relative to an active mission*, so it is
 * computed against a corridor at query time (`missionRelevance` in `routes.ts`) and layered on
 * top. Baking it in here would mean an object was permanently a rescue candidate for a mission
 * that had not been defined yet.
 *
 * Every rule below is grounded in a real SATCAT field. None infers a hazard the catalog does
 * not actually record.
 */

import type { CatalogClass, SatcatMetadata } from './types.js';

export interface ClassificationRule {
  readonly id: string;
  readonly result: CatalogClass;
  /** Shown in the legend. */
  readonly label: string;
  /** The reason attached to a matching object, and the legend's explanatory line. */
  readonly reason: string;
  readonly matches: (m: SatcatMetadata) => boolean;
}

const OPERATIONAL_STATUSES = new Set([
  'OPERATIONAL',
  'PARTIALLY_OPERATIONAL',
  'BACKUP_STANDBY',
  'SPARE',
  'EXTENDED_MISSION',
]);

/**
 * Evaluated in order; the first match wins. Ordering matters: a decayed payload is decayed
 * first and a payload second, so the re-entry rule has to precede the operational one.
 */
export const CLASSIFICATION_RULES: readonly ClassificationRule[] = [
  {
    id: 'decayed',
    result: 'ATTENTION',
    label: 'Re-entered',
    reason: 'SATCAT records a decay date: the object has re-entered and is not on orbit.',
    matches: (m) => m.decayDate !== null || m.opsStatus === 'DECAYED',
  },
  {
    id: 'debris',
    result: 'ATTENTION',
    label: 'Debris',
    reason: 'Catalogued as debris: uncontrolled, cannot manoeuvre, a conjunction hazard.',
    matches: (m) => m.objectType === 'DEBRIS',
  },
  {
    id: 'rocket-body',
    result: 'ATTENTION',
    label: 'Spent rocket body',
    reason: 'Spent upper stage: uncontrolled and unable to manoeuvre out of a conjunction.',
    matches: (m) => m.objectType === 'ROCKET BODY',
  },
  {
    id: 'nonoperational',
    result: 'ATTENTION',
    label: 'Non-operational payload',
    reason: 'Catalogued as non-operational: on orbit, but not responding to control.',
    matches: (m) => m.opsStatus === 'NONOPERATIONAL',
  },
  {
    id: 'operational-payload',
    result: 'NOMINAL',
    label: 'Operational payload',
    reason: 'Catalogued as an operational payload with a working or standby status.',
    matches: (m) => m.objectType === 'PAYLOAD' && m.opsStatus !== null && OPERATIONAL_STATUSES.has(m.opsStatus),
  },
  {
    id: 'status-unknown',
    result: 'UNKNOWN',
    label: 'Status not stated',
    reason: 'The catalog does not state an operational status for this object.',
    matches: (m) => m.opsStatus === 'UNKNOWN' || m.opsStatus === null,
  },
  {
    id: 'no-satcat-row',
    result: 'UNKNOWN',
    label: 'Metadata unavailable',
    reason: 'The object has an element set but no SATCAT record, so its type and status are unknown.',
    matches: (m) => m.objectType === null,
  },
];

export interface Classification {
  readonly catalogClass: CatalogClass;
  readonly reason: string;
  readonly ruleId: string;
}

/** The result when no rule matches — reported as unknown rather than assumed nominal. */
const UNCLASSIFIED: Classification = {
  catalogClass: 'UNKNOWN',
  reason: 'No classification rule matched this object.',
  ruleId: 'unmatched',
};

export function classify(metadata: SatcatMetadata): Classification {
  for (const rule of CLASSIFICATION_RULES) {
    if (rule.matches(metadata)) {
      return { catalogClass: rule.result, reason: rule.reason, ruleId: rule.id };
    }
  }
  return UNCLASSIFIED;
}

/**
 * The legend, served to the frontend so the swatches and their meanings come from the same
 * source as the colouring itself. `mission-relevant` is included here because it belongs in the
 * legend, even though it is applied at query time rather than by `classify()`.
 */
export interface LegendEntry {
  readonly key: string;
  readonly label: string;
  readonly description: string;
}

export function buildLegend(): readonly LegendEntry[] {
  const fromRules = CLASSIFICATION_RULES.map((r) => ({
    key: `${r.result}:${r.id}`,
    label: r.label,
    description: r.reason,
  }));
  return [
    {
      key: 'MISSION:relevant',
      label: 'Mission relevant',
      description:
        'Inside the active planning corridor, or the mission target or rescue vehicle. ' +
        'Computed against the loaded mission, not a property of the object.',
    },
    ...fromRules,
  ];
}
