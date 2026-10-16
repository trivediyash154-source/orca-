/**
 * Loads the catalogue for a set of filters, and keeps the honest status alongside it.
 *
 * The filters go to the bridge, not to a client-side `.filter()`: the point of server-side
 * filtering is that a browser showing a 400-600 km slice never downloads the objects outside it.
 * Every filter change therefore issues a new request and aborts the one in flight, so a user
 * dragging an altitude slider does not queue a dozen responses that arrive out of order.
 */

import { useCallback, useEffect, useMemo, useRef, useState } from 'react';

import { createCatalogService } from './catalogService';
import type {
  CatalogElement,
  CatalogLegendResponse,
  CatalogQuery,
  CatalogStatus,
  CatalogSummary,
} from './types';

export interface CatalogState {
  readonly elements: readonly CatalogElement[];
  readonly status: CatalogStatus | null;
  readonly summary: CatalogSummary | null;
  readonly legend: CatalogLegendResponse | null;
  /** How many objects matched the filters server-side, which may exceed what was returned. */
  readonly matched: number;
  readonly loading: boolean;
  /** Non-null when the bridge could not be reached or answered an error. */
  readonly error: string | null;
  readonly refresh: () => void;
  /** Asks the bridge to re-fetch from the provider. */
  readonly synchronise: () => void;
  readonly synchronising: boolean;
}

const EMPTY: readonly CatalogElement[] = [];

export function useCatalog(baseUrl: string, query: CatalogQuery): CatalogState {
  const service = useMemo(() => createCatalogService(baseUrl), [baseUrl]);

  const [elements, setElements] = useState<readonly CatalogElement[]>(EMPTY);
  const [status, setStatus] = useState<CatalogStatus | null>(null);
  const [summary, setSummary] = useState<CatalogSummary | null>(null);
  const [legend, setLegend] = useState<CatalogLegendResponse | null>(null);
  const [matched, setMatched] = useState(0);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [synchronising, setSynchronising] = useState(false);
  const [reloadNonce, setReloadNonce] = useState(0);

  // Serialised so the effect re-runs on a genuine filter change rather than on every render
  // that happens to construct an equivalent object literal.
  const queryKey = JSON.stringify(query);

  const mounted = useRef(true);
  useEffect(() => {
    mounted.current = true;
    return () => {
      mounted.current = false;
    };
  }, []);

  useEffect(() => {
    const controller = new AbortController();
    setLoading(true);
    void (async () => {
      const parsed = JSON.parse(queryKey) as CatalogQuery;
      const [elementsResult, summaryResult] = await Promise.all([
        service.elements(parsed, controller.signal),
        service.summary(parsed, controller.signal),
      ]);
      if (!mounted.current || controller.signal.aborted) return;

      if (elementsResult.ok) {
        setElements(elementsResult.value.objects);
        setStatus(elementsResult.value.status);
        setMatched(elementsResult.value.matched);
        setError(null);
      } else if (elementsResult.error.message !== 'aborted') {
        // The catalog is deliberately NOT cleared on a failed refresh: showing an empty sky
        // would read as "there are no satellites" when the truth is "we could not ask".
        setError(elementsResult.error.message);
      }

      if (summaryResult.ok) setSummary(summaryResult.value);
      setLoading(false);
    })();
    return () => controller.abort();
  }, [service, queryKey, reloadNonce]);

  // The legend describes the classification rules, which do not depend on the filters.
  useEffect(() => {
    const controller = new AbortController();
    void (async () => {
      const result = await service.legend(controller.signal);
      if (!mounted.current || controller.signal.aborted) return;
      if (result.ok) setLegend(result.value);
    })();
    return () => controller.abort();
  }, [service]);

  const refresh = useCallback(() => setReloadNonce((n) => n + 1), []);

  const synchronise = useCallback(() => {
    setSynchronising(true);
    void (async () => {
      const result = await service.sync();
      if (!mounted.current) return;
      if (result.ok) setStatus(result.value);
      else setError(result.error.message);
      setSynchronising(false);
      setReloadNonce((n) => n + 1);
    })();
  }, [service]);

  return {
    elements,
    status,
    summary,
    legend,
    matched,
    loading,
    error,
    refresh,
    synchronise,
    synchronising,
  };
}
