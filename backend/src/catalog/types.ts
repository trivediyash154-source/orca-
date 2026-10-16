/**
 * The satellite-catalog domain, as served by the bridge.
 *
 * ## Provenance is part of the type
 *
 * ORCA's architecture rule is that the frontend displays and never invents
 * (`docs/ARCHITECTURE.md` §6.1). A catalog introduces a second kind of truth alongside the C++
 * engine's simulation state, so every record carries where it came from and when — a reader can
 * always tell a real CelesTrak element set from a value the UI made up, because the UI cannot
 * make one up: fields the sources do not supply are `null`, never a plausible-looking guess.
 *
 * ## Two real sources, joined on NORAD catalog number
 *
 *   1. CelesTrak GP (`gp.php?GROUP=...&FORMAT=tle`) — the orbital element set, and the exact
 *      TLE line pair, which is the canonical input to SGP4. Elements only; no descriptive
 *      metadata at all.
 *   2. CelesTrak SATCAT (`pub/satcat.csv`) — object type, owner, operational status, launch and
 *      decay dates, radar cross-section. Descriptive metadata only; its orbital columns are
 *      rounded summaries and are deliberately NOT used here, because the GP set is authoritative.
 *
 * Neither requires an API key. Space-Track does, which is why it is behind the credential
 * boundary in `config/index.ts` rather than the default.
 */

/** CelesTrak SATCAT `OBJECT_TYPE`. `null` when the object has no SATCAT row. */
export type ObjectType = 'PAYLOAD' | 'ROCKET BODY' | 'DEBRIS' | 'UNKNOWN';

/**
 * CelesTrak SATCAT `OPS_STATUS_CODE`, expanded. The single-character codes are documented at
 * celestrak.org/satcat/status.php; they are expanded here so the wire format is self-describing
 * and the frontend never has to carry a lookup table of one-letter codes.
 */
export type OpsStatus =
  | 'OPERATIONAL'
  | 'PARTIALLY_OPERATIONAL'
  | 'BACKUP_STANDBY'
  | 'SPARE'
  | 'EXTENDED_MISSION'
  | 'NONOPERATIONAL'
  | 'DECAYED'
  | 'UNKNOWN';

/**
 * Orbit regime, derived from the element set by `derive.ts` using published altitude/period
 * boundaries. Derived — not read from SATCAT's `ORBIT_TYPE`, which is coarser.
 */
export type OrbitRegime = 'LEO' | 'MEO' | 'GEO' | 'HEO' | 'UNKNOWN';

/**
 * The catalog half of the colour system (`classify.ts`). Deliberately does NOT include the
 * green "mission relevant" state: that one is not a property of the object, it is a property of
 * the object *relative to the active planning corridor*, so it is computed against a mission
 * context rather than baked into the record. See `MissionRelevance`.
 */
export type CatalogClass = 'NOMINAL' | 'ATTENTION' | 'UNKNOWN';

/** How an object relates to the currently-selected mission corridor, if any. */
export type MissionRelevance = 'CANDIDATE' | 'TARGET' | 'RESCUE_VEHICLE' | 'NONE';

/** A parsed NORAD two-line element set, with the raw lines retained for SGP4. */
export interface ElementSet {
  readonly noradId: number;
  /** From the TLE's own name line. The catalog's official designation, never a generated one. */
  readonly name: string;
  /** COSPAR/international designator, e.g. "1998-067A". */
  readonly intlDesignator: string;
  /** Element-set epoch, ISO 8601 UTC. */
  readonly epoch: string;
  readonly inclinationDeg: number;
  readonly raanDeg: number;
  readonly eccentricity: number;
  readonly argPerigeeDeg: number;
  readonly meanAnomalyDeg: number;
  readonly meanMotionRevPerDay: number;
  readonly bstar: number;
  readonly revAtEpoch: number;
  /**
   * The two element lines exactly as published. Kept because SGP4 is specified in terms of the
   * TLE's own fixed-column encoding: re-serialising parsed floats back into a TLE loses
   * precision, so the propagator is handed the original text.
   */
  readonly line1: string;
  readonly line2: string;
}

/** Quantities computed from the element set. Every one is derived, none is fetched. */
export interface DerivedOrbit {
  readonly semiMajorAxisKm: number;
  readonly perigeeAltitudeKm: number;
  readonly apogeeAltitudeKm: number;
  readonly periodMinutes: number;
  readonly regime: OrbitRegime;
}

/** SATCAT descriptive metadata. Every field is nullable: not every catalogued object has a row. */
export interface SatcatMetadata {
  readonly objectType: ObjectType | null;
  readonly opsStatus: OpsStatus | null;
  /** SATCAT `OWNER` — the responsible country or organisation code, e.g. "ISS", "US", "PRC". */
  readonly owner: string | null;
  readonly launchDate: string | null;
  readonly launchSite: string | null;
  /** Non-null means the object has re-entered; it should not be rendered as on-orbit. */
  readonly decayDate: string | null;
  /** Radar cross-section, m². SATCAT reports this only for some objects. */
  readonly rcsM2: number | null;
}

/** One catalogued orbital object: real elements, real metadata, explicit provenance. */
export interface CatalogObject {
  readonly elements: ElementSet;
  readonly derived: DerivedOrbit;
  readonly metadata: SatcatMetadata;
  readonly catalogClass: CatalogClass;
  /** Human-readable justification for `catalogClass`, so the colour is never a mystery. */
  readonly classReason: string;
  readonly source: 'CELESTRAK';
  /** When the bridge fetched this record, ISO 8601 UTC. */
  readonly fetchedAt: string;
}

/** Whether the bridge currently holds a catalog, and how it got there. */
export type SyncState =
  | 'NOT_CONFIGURED'
  | 'NEVER_SYNCHRONISED'
  | 'SYNCHRONISING'
  | 'SYNCHRONISED'
  | 'FAILED'
  | 'STALE';

/**
 * The honest status object. The frontend renders this verbatim rather than inferring liveness
 * from whether the object list happens to be non-empty — "we have 16,000 records" and "those
 * records are current" are different claims, and only this type can distinguish them.
 */
export interface CatalogStatus {
  readonly state: SyncState;
  readonly objectCount: number;
  readonly lastSyncStartedAt: string | null;
  readonly lastSyncCompletedAt: string | null;
  readonly lastError: string | null;
  /** Age of the element sets in hours, or null when nothing has been fetched. */
  readonly ageHours: number | null;
  /** Beyond this age the state becomes STALE: TLEs decay in accuracy and must be refreshed. */
  readonly staleAfterHours: number;
  readonly sources: readonly CatalogSourceHealth[];
  /** The catalog group requested from the provider, e.g. "active". */
  readonly group: string;
}

export interface CatalogSourceHealth {
  readonly name: string;
  readonly url: string;
  readonly ok: boolean;
  readonly recordCount: number;
  readonly lastError: string | null;
  readonly fetchedAt: string | null;
}
