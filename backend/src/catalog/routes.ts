/**
 * Catalog HTTP surface.
 *
 * ## Server-side filtering is the point
 *
 * A browser rendering a 400-600 km, 50-60° slice of the catalog should receive that slice, not
 * 16,000 objects it will immediately discard. Every filter below therefore runs here, and the
 * compact `/catalog/elements` projection carries only what the renderer and the propagator
 * actually need. The full record — owner, launch site, radar cross-section, the classification
 * reason — is one request away per object, which is the access pattern a metadata panel has.
 *
 * ## Mission relevance is a query parameter, not stored state
 *
 * "Green" means *relative to the mission currently loaded*. The corridor bounds and the target
 * and rescue-vehicle catalog numbers arrive as explicit query parameters and are applied here,
 * so the answer is reproducible from the request alone and no object is permanently marked a
 * rescue candidate for a mission nobody has defined.
 */

import { Router, type Request, type Response } from 'express';

import { buildLegend, CLASSIFICATION_RULES } from './classify.js';
import { ORBIT_CONSTANTS } from './derive.js';
import type { CatalogStore } from './CatalogStore.js';
import type { CatalogObject, MissionRelevance } from './types.js';

/** Bounds that define which catalogued objects are relevant to the loaded mission. */
interface MissionCorridor {
  readonly altMinKm: number | null;
  readonly altMaxKm: number | null;
  readonly incMinDeg: number | null;
  readonly incMaxDeg: number | null;
  readonly targetNorad: number | null;
  readonly rescueNorad: number | null;
}

interface CatalogFilters {
  readonly regimes: ReadonlySet<string> | null;
  readonly classes: ReadonlySet<string> | null;
  readonly objectTypes: ReadonlySet<string> | null;
  readonly statuses: ReadonlySet<string> | null;
  readonly owner: string | null;
  readonly altMinKm: number | null;
  readonly altMaxKm: number | null;
  readonly incMinDeg: number | null;
  readonly incMaxDeg: number | null;
  readonly query: string | null;
  /** Restrict to objects the corridor marks relevant. */
  readonly missionRelevantOnly: boolean;
}

const MAX_LIMIT = 20_000;
const DEFAULT_LIMIT = 10_000;

function numParam(raw: unknown): number | null {
  if (typeof raw !== 'string' || raw.trim() === '') return null;
  const n = Number(raw);
  return Number.isFinite(n) ? n : null;
}

function setParam(raw: unknown): ReadonlySet<string> | null {
  if (typeof raw !== 'string' || raw.trim() === '') return null;
  const values = raw
    .split(',')
    .map((s) => s.trim().toUpperCase())
    .filter((s) => s !== '');
  return values.length > 0 ? new Set(values) : null;
}

function parseCorridor(q: Request['query']): MissionCorridor {
  return {
    altMinKm: numParam(q.corridorAltMin),
    altMaxKm: numParam(q.corridorAltMax),
    incMinDeg: numParam(q.corridorIncMin),
    incMaxDeg: numParam(q.corridorIncMax),
    targetNorad: numParam(q.targetNorad),
    rescueNorad: numParam(q.rescueNorad),
  };
}

function parseFilters(q: Request['query']): CatalogFilters {
  const query = typeof q.q === 'string' && q.q.trim() !== '' ? q.q.trim().toLowerCase() : null;
  return {
    regimes: setParam(q.regime),
    classes: setParam(q.class),
    objectTypes: setParam(q.objectType),
    statuses: setParam(q.status),
    owner: typeof q.owner === 'string' && q.owner.trim() !== '' ? q.owner.trim().toUpperCase() : null,
    altMinKm: numParam(q.altMin),
    altMaxKm: numParam(q.altMax),
    incMinDeg: numParam(q.incMin),
    incMaxDeg: numParam(q.incMax),
    query,
    missionRelevantOnly: q.missionRelevantOnly === 'true',
  };
}

/**
 * Mission relevance. The target and rescue vehicle are named explicitly; everything else earns
 * CANDIDATE only by sitting inside every corridor bound that was actually supplied. An absent
 * bound is not treated as an open one — with no corridor at all, nothing is a candidate, which
 * is the honest answer to "which objects suit a mission you have not described".
 */
function missionRelevance(o: CatalogObject, c: MissionCorridor): MissionRelevance {
  if (c.targetNorad !== null && o.elements.noradId === c.targetNorad) return 'TARGET';
  if (c.rescueNorad !== null && o.elements.noradId === c.rescueNorad) return 'RESCUE_VEHICLE';

  const hasCorridor =
    c.altMinKm !== null || c.altMaxKm !== null || c.incMinDeg !== null || c.incMaxDeg !== null;
  if (!hasCorridor) return 'NONE';

  // A candidate must be reachable throughout its orbit, so both apses are tested rather than a
  // mean altitude: an object whose perigee dips below the corridor is not inside it.
  const { perigeeAltitudeKm, apogeeAltitudeKm } = o.derived;
  if (!Number.isFinite(perigeeAltitudeKm) || !Number.isFinite(apogeeAltitudeKm)) return 'NONE';
  if (c.altMinKm !== null && perigeeAltitudeKm < c.altMinKm) return 'NONE';
  if (c.altMaxKm !== null && apogeeAltitudeKm > c.altMaxKm) return 'NONE';
  if (c.incMinDeg !== null && o.elements.inclinationDeg < c.incMinDeg) return 'NONE';
  if (c.incMaxDeg !== null && o.elements.inclinationDeg > c.incMaxDeg) return 'NONE';

  // Only a controllable payload can serve as a rescue candidate; debris and spent stages
  // cannot manoeuvre, so proximity alone does not make them useful.
  if (o.catalogClass !== 'NOMINAL') return 'NONE';
  return 'CANDIDATE';
}

function matches(o: CatalogObject, f: CatalogFilters, relevance: MissionRelevance): boolean {
  if (f.regimes && !f.regimes.has(o.derived.regime)) return false;
  if (f.classes && !f.classes.has(o.catalogClass)) return false;
  if (f.objectTypes && !f.objectTypes.has(o.metadata.objectType ?? 'UNKNOWN')) return false;
  if (f.statuses && !f.statuses.has(o.metadata.opsStatus ?? 'UNKNOWN')) return false;
  if (f.owner !== null && (o.metadata.owner ?? '').toUpperCase() !== f.owner) return false;

  const { perigeeAltitudeKm, apogeeAltitudeKm } = o.derived;
  // An altitude range selects objects whose orbit *intersects* the band, not only those
  // entirely inside it — an elliptical orbit passing through 400-600 km is in that band for
  // part of every revolution, and a conjunction screen that missed it would be wrong.
  if (f.altMinKm !== null && apogeeAltitudeKm < f.altMinKm) return false;
  if (f.altMaxKm !== null && perigeeAltitudeKm > f.altMaxKm) return false;

  if (f.incMinDeg !== null && o.elements.inclinationDeg < f.incMinDeg) return false;
  if (f.incMaxDeg !== null && o.elements.inclinationDeg > f.incMaxDeg) return false;

  if (f.query !== null) {
    const q = f.query;
    const hit =
      o.elements.name.toLowerCase().includes(q) ||
      String(o.elements.noradId).includes(q) ||
      o.elements.intlDesignator.toLowerCase().includes(q);
    if (!hit) return false;
  }

  if (f.missionRelevantOnly && relevance === 'NONE') return false;
  return true;
}

/** The full record, plus the relevance computed for this request. */
function detailOf(o: CatalogObject, relevance: MissionRelevance) {
  return {
    noradId: o.elements.noradId,
    name: o.elements.name,
    intlDesignator: o.elements.intlDesignator,
    epoch: o.elements.epoch,
    elements: {
      inclinationDeg: o.elements.inclinationDeg,
      raanDeg: o.elements.raanDeg,
      eccentricity: o.elements.eccentricity,
      argPerigeeDeg: o.elements.argPerigeeDeg,
      meanAnomalyDeg: o.elements.meanAnomalyDeg,
      meanMotionRevPerDay: o.elements.meanMotionRevPerDay,
      bstar: o.elements.bstar,
      revAtEpoch: o.elements.revAtEpoch,
    },
    derived: o.derived,
    metadata: o.metadata,
    catalogClass: o.catalogClass,
    classReason: o.classReason,
    missionRelevance: relevance,
    tle: { line1: o.elements.line1, line2: o.elements.line2 },
    source: o.source,
    fetchedAt: o.fetchedAt,
    // Stated on every record so a consumer never has to infer it: these are catalogue element
    // sets to be propagated, not a live downlink.
    provenance: 'PROPAGATED_CATALOG' as const,
  };
}

export function createCatalogRouter(store: CatalogStore): Router {
  const router = Router();

  router.get('/status', (_req: Request, res: Response) => {
    res.json(store.status());
  });

  /** The colour legend, generated from the same rule table that does the colouring. */
  router.get('/legend', (_req: Request, res: Response) => {
    res.json({
      legend: buildLegend(),
      rules: CLASSIFICATION_RULES.map((r) => ({
        id: r.id,
        result: r.result,
        label: r.label,
        reason: r.reason,
      })),
      regimeBoundaries: ORBIT_CONSTANTS,
    });
  });

  router.post('/sync', async (_req: Request, res: Response) => {
    const status = await store.sync();
    res.status(status.state === 'FAILED' ? 502 : 200).json(status);
  });

  /**
   * The compact projection the renderer consumes: identity, the TLE the propagator needs, and
   * the few fields the point cloud colours and filters by. Deliberately excludes owner, launch
   * site and the classification reason — a 16,000-row payload should not carry text that only
   * the metadata panel reads.
   */
  router.get('/elements', (req: Request, res: Response) => {
    const status = store.status();
    const filters = parseFilters(req.query);
    const corridor = parseCorridor(req.query);
    const limit = Math.min(numParam(req.query.limit) ?? DEFAULT_LIMIT, MAX_LIMIT);
    const offset = Math.max(numParam(req.query.offset) ?? 0, 0);

    const objects: unknown[] = [];
    let matched = 0;
    for (const o of store.all()) {
      const relevance = missionRelevance(o, corridor);
      if (!matches(o, filters, relevance)) continue;
      matched++;
      if (matched <= offset) continue;
      if (objects.length >= limit) continue;
      objects.push({
        id: o.elements.noradId,
        name: o.elements.name,
        l1: o.elements.line1,
        l2: o.elements.line2,
        cls: o.catalogClass,
        rel: relevance,
        reg: o.derived.regime,
        inc: o.elements.inclinationDeg,
        ecc: o.elements.eccentricity,
        per: o.derived.perigeeAltitudeKm,
        apo: o.derived.apogeeAltitudeKm,
        pmin: o.derived.periodMinutes,
      });
    }

    res.json({
      status,
      matched,
      returned: objects.length,
      offset,
      limit,
      provenance: 'PROPAGATED_CATALOG',
      objects,
    });
  });

  /** Paginated full records, for list views and search results. */
  router.get('/objects', (req: Request, res: Response) => {
    const filters = parseFilters(req.query);
    const corridor = parseCorridor(req.query);
    const limit = Math.min(numParam(req.query.limit) ?? 100, 1000);
    const offset = Math.max(numParam(req.query.offset) ?? 0, 0);

    const results: unknown[] = [];
    let matched = 0;
    for (const o of store.all()) {
      const relevance = missionRelevance(o, corridor);
      if (!matches(o, filters, relevance)) continue;
      matched++;
      if (matched <= offset) continue;
      if (results.length >= limit) continue;
      results.push(detailOf(o, relevance));
    }
    res.json({ status: store.status(), matched, returned: results.length, offset, limit, objects: results });
  });

  /** Aggregate counts, so a dashboard summary does not require downloading the catalog. */
  router.get('/summary', (req: Request, res: Response) => {
    const corridor = parseCorridor(req.query);
    const byClass: Record<string, number> = {};
    const byRegime: Record<string, number> = {};
    const byType: Record<string, number> = {};
    const byRelevance: Record<string, number> = {};
    for (const o of store.all()) {
      byClass[o.catalogClass] = (byClass[o.catalogClass] ?? 0) + 1;
      byRegime[o.derived.regime] = (byRegime[o.derived.regime] ?? 0) + 1;
      const t = o.metadata.objectType ?? 'UNKNOWN';
      byType[t] = (byType[t] ?? 0) + 1;
      const r = missionRelevance(o, corridor);
      byRelevance[r] = (byRelevance[r] ?? 0) + 1;
    }
    res.json({ status: store.status(), total: store.count, byClass, byRegime, byType, byRelevance });
  });

  router.get('/objects/:noradId', (req: Request, res: Response) => {
    const id = Number(req.params.noradId);
    if (!Number.isFinite(id)) {
      res.status(400).json({ error: 'noradId must be a number' });
      return;
    }
    const object = store.get(id);
    if (!object) {
      const status = store.status();
      // A miss means one of two very different things, and the client needs to tell them
      // apart: no catalog has been fetched, or this object is not in the catalog that was.
      res.status(404).json({
        error:
          status.objectCount === 0
            ? 'No catalog has been synchronised, so no object can be looked up.'
            : `NORAD ${id} is not present in the synchronised catalog.`,
        status,
      });
      return;
    }
    res.json(detailOf(object, missionRelevance(object, parseCorridor(req.query))));
  });

  return router;
}
