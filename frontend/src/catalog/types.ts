/**
 * The catalog wire types, mirroring what the bridge's `/catalog/*` routes actually serve
 * (`backend/src/catalog/`).
 *
 * ## Provenance is carried, not assumed
 *
 * ORCA already distinguishes engine truth from presentation (`docs/VISUAL_DESIGN.md` §3). The
 * catalog adds a third category, and the UI has to be able to name it: these positions are
 * neither engine output nor live telemetry, they are *propagated catalogue element sets*. That
 * string travels with the data so a readout can label itself correctly instead of a component
 * having to remember which of three kinds of value it is showing.
 */

/** Where a displayed value came from. Rendered next to readouts, never inferred. */
export type Provenance =
  | 'PROPAGATED_CATALOG'
  | 'ENGINE_SIMULATION'
  | 'LIVE_TELEMETRY'
  | 'SYNTHETIC_DEMONSTRATION'
  | 'ILLUSTRATIVE';

export type CatalogClass = 'NOMINAL' | 'ATTENTION' | 'UNKNOWN';
export type MissionRelevance = 'CANDIDATE' | 'TARGET' | 'RESCUE_VEHICLE' | 'NONE';
export type OrbitRegime = 'LEO' | 'MEO' | 'GEO' | 'HEO' | 'UNKNOWN';
export type ObjectType = 'PAYLOAD' | 'ROCKET BODY' | 'DEBRIS' | 'UNKNOWN';

export type SyncState =
  | 'NOT_CONFIGURED'
  | 'NEVER_SYNCHRONISED'
  | 'SYNCHRONISING'
  | 'SYNCHRONISED'
  | 'FAILED'
  | 'STALE';

export interface CatalogSourceHealth {
  readonly name: string;
  readonly url: string;
  readonly ok: boolean;
  readonly recordCount: number;
  readonly lastError: string | null;
  readonly fetchedAt: string | null;
}

export interface CatalogStatus {
  readonly state: SyncState;
  readonly objectCount: number;
  readonly lastSyncStartedAt: string | null;
  readonly lastSyncCompletedAt: string | null;
  readonly lastError: string | null;
  readonly ageHours: number | null;
  readonly staleAfterHours: number;
  readonly sources: readonly CatalogSourceHealth[];
  readonly group: string;
}

/**
 * The compact projection served by `/catalog/elements`. Short keys are deliberate: this array
 * carries ten thousand entries, and the descriptive fields a metadata panel needs are fetched
 * per object instead of multiplied across the whole payload.
 */
export interface CatalogElement {
  /** NORAD catalog number — the stable identity for every object. */
  readonly id: number;
  readonly name: string;
  /** TLE line 1, verbatim. The propagator is handed this rather than reconstructed floats. */
  readonly l1: string;
  readonly l2: string;
  readonly cls: CatalogClass;
  readonly rel: MissionRelevance;
  readonly reg: OrbitRegime;
  readonly inc: number;
  readonly ecc: number;
  readonly per: number;
  readonly apo: number;
  readonly pmin: number;
}

export interface CatalogElementsResponse {
  readonly status: CatalogStatus;
  readonly matched: number;
  readonly returned: number;
  readonly offset: number;
  readonly limit: number;
  readonly provenance: Provenance;
  readonly objects: readonly CatalogElement[];
}

export interface CatalogObjectDetail {
  readonly noradId: number;
  readonly name: string;
  readonly intlDesignator: string;
  readonly epoch: string;
  readonly elements: {
    readonly inclinationDeg: number;
    readonly raanDeg: number;
    readonly eccentricity: number;
    readonly argPerigeeDeg: number;
    readonly meanAnomalyDeg: number;
    readonly meanMotionRevPerDay: number;
    readonly bstar: number;
    readonly revAtEpoch: number;
  };
  readonly derived: {
    readonly semiMajorAxisKm: number;
    readonly perigeeAltitudeKm: number;
    readonly apogeeAltitudeKm: number;
    readonly periodMinutes: number;
    readonly regime: OrbitRegime;
  };
  readonly metadata: {
    readonly objectType: ObjectType | null;
    readonly opsStatus: string | null;
    readonly owner: string | null;
    readonly launchDate: string | null;
    readonly launchSite: string | null;
    readonly decayDate: string | null;
    readonly rcsM2: number | null;
  };
  readonly catalogClass: CatalogClass;
  readonly classReason: string;
  readonly missionRelevance: MissionRelevance;
  readonly tle: { readonly line1: string; readonly line2: string };
  readonly source: string;
  readonly fetchedAt: string;
  readonly provenance: Provenance;
}

export interface LegendEntry {
  readonly key: string;
  readonly label: string;
  readonly description: string;
}

export interface CatalogLegendResponse {
  readonly legend: readonly LegendEntry[];
  readonly rules: readonly {
    readonly id: string;
    readonly result: CatalogClass;
    readonly label: string;
    readonly reason: string;
  }[];
}

export interface CatalogSummary {
  readonly status: CatalogStatus;
  readonly total: number;
  readonly byClass: Readonly<Record<string, number>>;
  readonly byRegime: Readonly<Record<string, number>>;
  readonly byType: Readonly<Record<string, number>>;
  readonly byRelevance: Readonly<Record<string, number>>;
}

/** Filters sent to the bridge. Every one is applied server-side; see `catalog/routes.ts`. */
export interface CatalogQuery {
  readonly regime?: readonly OrbitRegime[];
  readonly klass?: readonly CatalogClass[];
  readonly objectType?: readonly ObjectType[];
  readonly owner?: string;
  readonly altMin?: number;
  readonly altMax?: number;
  readonly incMin?: number;
  readonly incMax?: number;
  readonly q?: string;
  readonly limit?: number;
  readonly missionRelevantOnly?: boolean;
  /** Mission corridor — what makes an object "mission relevant" rather than merely present. */
  readonly corridorAltMin?: number;
  readonly corridorAltMax?: number;
  readonly corridorIncMin?: number;
  readonly corridorIncMax?: number;
  readonly targetNorad?: number;
  readonly rescueNorad?: number;
}
