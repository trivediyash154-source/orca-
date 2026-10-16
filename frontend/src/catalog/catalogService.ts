/**
 * HTTP client for the bridge's `/catalog/*` routes.
 *
 * Mirrors the conventions of the existing `services/engineService.ts`: plain `fetch`, no client
 * library, errors surfaced as typed results rather than thrown strings, and no caching layer of
 * its own — the bridge already caches, and a second cache here would only let the two disagree
 * about how old the data is.
 *
 * Every filter is forwarded as a query parameter and applied server-side. A browser showing a
 * 400-600 km slice asks for that slice; it does not download sixteen thousand objects and throw
 * most of them away.
 */

import type {
  CatalogElementsResponse,
  CatalogLegendResponse,
  CatalogObjectDetail,
  CatalogQuery,
  CatalogStatus,
  CatalogSummary,
} from './types';

export interface CatalogFetchError {
  readonly kind: 'network' | 'http' | 'parse';
  readonly message: string;
  readonly status?: number;
}

export type CatalogResult<T> = { ok: true; value: T } | { ok: false; error: CatalogFetchError };

function toQueryString(query: CatalogQuery): string {
  const p = new URLSearchParams();
  if (query.regime?.length) p.set('regime', query.regime.join(','));
  if (query.klass?.length) p.set('class', query.klass.join(','));
  if (query.objectType?.length) p.set('objectType', query.objectType.join(','));
  if (query.owner) p.set('owner', query.owner);
  if (query.altMin !== undefined) p.set('altMin', String(query.altMin));
  if (query.altMax !== undefined) p.set('altMax', String(query.altMax));
  if (query.incMin !== undefined) p.set('incMin', String(query.incMin));
  if (query.incMax !== undefined) p.set('incMax', String(query.incMax));
  if (query.q) p.set('q', query.q);
  if (query.limit !== undefined) p.set('limit', String(query.limit));
  if (query.missionRelevantOnly) p.set('missionRelevantOnly', 'true');
  if (query.corridorAltMin !== undefined) p.set('corridorAltMin', String(query.corridorAltMin));
  if (query.corridorAltMax !== undefined) p.set('corridorAltMax', String(query.corridorAltMax));
  if (query.corridorIncMin !== undefined) p.set('corridorIncMin', String(query.corridorIncMin));
  if (query.corridorIncMax !== undefined) p.set('corridorIncMax', String(query.corridorIncMax));
  if (query.targetNorad !== undefined) p.set('targetNorad', String(query.targetNorad));
  if (query.rescueNorad !== undefined) p.set('rescueNorad', String(query.rescueNorad));
  const s = p.toString();
  return s === '' ? '' : `?${s}`;
}

async function getJson<T>(url: string, signal?: AbortSignal): Promise<CatalogResult<T>> {
  let res: Response;
  try {
    res = await fetch(url, { signal });
  } catch (err) {
    // An aborted request is a normal consequence of the user changing a filter, not a failure
    // worth showing them.
    if (err instanceof DOMException && err.name === 'AbortError') {
      return { ok: false, error: { kind: 'network', message: 'aborted' } };
    }
    return {
      ok: false,
      error: {
        kind: 'network',
        message:
          err instanceof Error
            ? `Cannot reach the ORCA bridge: ${err.message}`
            : 'Cannot reach the ORCA bridge.',
      },
    };
  }
  if (!res.ok) {
    return {
      ok: false,
      error: { kind: 'http', status: res.status, message: `Bridge responded HTTP ${res.status}` },
    };
  }
  try {
    return { ok: true, value: (await res.json()) as T };
  } catch (err) {
    return {
      ok: false,
      error: { kind: 'parse', message: err instanceof Error ? err.message : 'malformed response' },
    };
  }
}

export function createCatalogService(baseUrl: string) {
  return {
    status: (signal?: AbortSignal) => getJson<CatalogStatus>(`${baseUrl}/catalog/status`, signal),

    legend: (signal?: AbortSignal) =>
      getJson<CatalogLegendResponse>(`${baseUrl}/catalog/legend`, signal),

    summary: (query: CatalogQuery = {}, signal?: AbortSignal) =>
      getJson<CatalogSummary>(`${baseUrl}/catalog/summary${toQueryString(query)}`, signal),

    /** The compact projection the point field renders and the propagator consumes. */
    elements: (query: CatalogQuery = {}, signal?: AbortSignal) =>
      getJson<CatalogElementsResponse>(`${baseUrl}/catalog/elements${toQueryString(query)}`, signal),

    object: (noradId: number, query: CatalogQuery = {}, signal?: AbortSignal) =>
      getJson<CatalogObjectDetail>(
        `${baseUrl}/catalog/objects/${noradId}${toQueryString(query)}`,
        signal,
      ),

    /**
     * Asks the bridge to refresh from the provider. Slow by nature (two multi-megabyte feeds),
     * and a no-op inside the provider's refresh window — the bridge resolves that from its own
     * cache and says so in the returned status.
     */
    sync: async (signal?: AbortSignal): Promise<CatalogResult<CatalogStatus>> => {
      try {
        const res = await fetch(`${baseUrl}/catalog/sync`, { method: 'POST', signal });
        const body = (await res.json()) as CatalogStatus;
        // A 502 still carries a real status object describing the failure, which is more useful
        // to the UI than a generic error.
        return { ok: true, value: body };
      } catch (err) {
        return {
          ok: false,
          error: {
            kind: 'network',
            message: err instanceof Error ? err.message : 'sync request failed',
          },
        };
      }
    },
  };
}

export type CatalogService = ReturnType<typeof createCatalogService>;

/**
 * Human-readable, honest phrasing for each synchronisation state. Centralised so every surface
 * that reports catalog health uses the same words — a status chip and a metadata panel must not
 * describe the same condition differently.
 */
export function describeSyncState(status: CatalogStatus): { label: string; detail: string } {
  switch (status.state) {
    case 'NOT_CONFIGURED':
      return {
        label: 'Catalog source not configured',
        detail: 'No orbital data provider is configured for this deployment.',
      };
    case 'NEVER_SYNCHRONISED':
      return {
        label: 'Catalog not synchronised',
        detail:
          'The bridge has not yet fetched an orbital catalog. No satellite positions can be ' +
          'propagated until it does.',
      };
    case 'SYNCHRONISING':
      return { label: 'Synchronising catalog', detail: 'Fetching element sets from the provider.' };
    case 'FAILED':
      return {
        label: 'Catalog synchronisation failed',
        detail: status.lastError ?? 'The provider could not be reached.',
      };
    case 'STALE':
      return {
        label: 'Catalog stale',
        detail:
          `Element sets are ${status.ageHours === null ? 'over' : Math.round(status.ageHours)} ` +
          `hours old (stale after ${status.staleAfterHours}h). SGP4 accuracy degrades with age ` +
          `from epoch; refresh before relying on these positions.`,
      };
    case 'SYNCHRONISED':
      return {
        label: 'Catalog synchronised',
        detail:
          `${status.objectCount.toLocaleString()} objects from group "${status.group}", ` +
          `${status.ageHours === null ? 'age unknown' : `${status.ageHours.toFixed(1)}h old`}.`,
      };
  }
}
