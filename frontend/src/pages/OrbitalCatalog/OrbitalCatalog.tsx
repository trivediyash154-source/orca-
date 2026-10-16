/**
 * Orbital Catalog — Earth at the centre, the real catalogue around it.
 *
 * The spatial hierarchy is deliberate and not a grid of cards: the globe and its satellite field
 * own the middle of the screen, filters sit to the left where they gate what is shown, the
 * selected object's record sits to the right where it answers "what is that", and the clock runs
 * along the bottom because it applies to everything above it.
 *
 * ## What this view claims, and what it does not
 *
 * It claims: these are real catalogued objects, with real identities, propagated by SGP4 from
 * real element sets to the instant on the clock. It does not claim live telemetry — the status
 * strip says so permanently, not as a disclaimer buried in a tooltip, because a moving satellite
 * field is exactly the thing a viewer would otherwise assume was a live feed.
 */

import { useCallback, useEffect, useMemo, useState } from 'react';

import { CatalogControls, DEFAULT_FILTERS, TimeControls, type CatalogFilterValues } from './CatalogControls';
import { SatelliteDetail } from './SatelliteDetail';
import { createCatalogService, describeSyncState } from '../../catalog/catalogService';
import { useCatalog } from '../../catalog/useCatalog';
import type { CatalogObjectDetail, CatalogQuery } from '../../catalog/types';
import { useEngine } from '../../state/EngineContext';
import { SimClockProvider, useSimClock } from '../../state/SimClockContext';
import { useUI } from '../../state/UIContext';
import { CatalogScene } from '../../visualization/catalog/CatalogScene';

function OrbitalCatalogInner() {
  const { baseUrl } = useEngine();
  const { reducedMotion, showStarfield } = useUI();
  const { handle } = useSimClock();

  const [filters, setFilters] = useState<CatalogFilterValues>(DEFAULT_FILTERS);
  const [selectedId, setSelectedId] = useState<number | null>(null);
  const [hoveredId, setHoveredId] = useState<number | null>(null);
  const [detail, setDetail] = useState<CatalogObjectDetail | null>(null);
  const [detailLoading, setDetailLoading] = useState(false);
  const [detailError, setDetailError] = useState<string | null>(null);
  const [propagation, setPropagation] = useState({ ok: 0, failed: 0 });

  const query = useMemo<CatalogQuery>(
    () => ({
      altMin: filters.altMin,
      altMax: filters.altMax,
      incMin: filters.incMin,
      incMax: filters.incMax,
      klass: filters.classes.length > 0 ? filters.classes : undefined,
      regime: filters.regimes.length > 0 ? filters.regimes : undefined,
      q: filters.search.trim() === '' ? undefined : filters.search.trim(),
      limit: filters.limit,
    }),
    [filters],
  );

  const catalog = useCatalog(baseUrl, query);
  const service = useMemo(() => createCatalogService(baseUrl), [baseUrl]);

  // The full record is fetched per selection rather than carried for every object — see
  // `catalog/routes.ts` on why the ten-thousand-row projection stays compact.
  useEffect(() => {
    if (selectedId === null) {
      setDetail(null);
      setDetailError(null);
      return;
    }
    const controller = new AbortController();
    setDetailLoading(true);
    void (async () => {
      const result = await service.object(selectedId, {}, controller.signal);
      if (controller.signal.aborted) return;
      if (result.ok) {
        setDetail(result.value);
        setDetailError(null);
      } else if (result.error.message !== 'aborted') {
        setDetail(null);
        setDetailError(result.error.message);
      }
      setDetailLoading(false);
    })();
    return () => controller.abort();
  }, [selectedId, service]);

  const handlePropagation = useCallback((ok: number, failed: number) => {
    // Only republish when the numbers actually change: this fires on every propagation tick,
    // and an unconditional setState would re-render the page ten times a second.
    setPropagation((prev) => (prev.ok === ok && prev.failed === failed ? prev : { ok, failed }));
  }, []);

  const sync = catalog.status ? describeSyncState(catalog.status) : null;
  const unusable =
    catalog.status !== null &&
    (catalog.status.state === 'NEVER_SYNCHRONISED' ||
      catalog.status.state === 'NOT_CONFIGURED' ||
      (catalog.status.state === 'FAILED' && catalog.elements.length === 0));

  return (
    <div className="catalog-page">
      <aside className="catalog-rail catalog-rail-left" aria-label="Catalogue filters">
        <CatalogControls
          filters={filters}
          onChange={setFilters}
          legend={catalog.legend}
          matched={catalog.matched}
          rendered={catalog.elements.length}
        />
      </aside>

      <div className="catalog-centre">
        <div className="catalog-status-strip">
          <span className={`catalog-status catalog-status-${catalog.status?.state.toLowerCase() ?? 'unknown'}`}>
            {sync?.label ?? 'Contacting bridge…'}
          </span>
          <span className="catalog-status-detail">{sync?.detail ?? ''}</span>
          {/* Permanent, not a dismissible notice: a field of moving satellites reads as live
              unless it says otherwise. */}
          <span className="catalog-provenance-tag">Propagated catalogue data · no live telemetry · no command uplink</span>
          <button
            type="button"
            className="catalog-sync-button"
            onClick={catalog.synchronise}
            disabled={catalog.synchronising}
          >
            {catalog.synchronising ? 'Synchronising…' : 'Synchronise'}
          </button>
        </div>

        <div className="catalog-scene">
          {unusable ? (
            <div className="catalog-empty" role="status">
              <h2>{sync?.label}</h2>
              <p>{sync?.detail}</p>
              <p className="muted">
                The bridge fetches element sets from a public provider. Nothing is rendered here
                until it holds a real catalog — an empty sky is not shown as if it were the sky.
              </p>
              <button type="button" className="catalog-sync-button" onClick={catalog.synchronise}>
                {catalog.synchronising ? 'Synchronising…' : 'Synchronise catalog'}
              </button>
            </div>
          ) : (
            <CatalogScene
              elements={catalog.elements}
              clock={handle}
              selectedId={selectedId}
              hoveredId={hoveredId}
              onSelect={setSelectedId}
              onHover={setHoveredId}
              reducedMotion={reducedMotion}
              showStarfield={showStarfield}
              onPropagationResult={handlePropagation}
            />
          )}

          {catalog.error !== null && (
            <p className="catalog-scene-error" role="alert">
              {catalog.error}
            </p>
          )}
        </div>

        <div className="catalog-bottom">
          <TimeControls />
          <div className="catalog-counts">
            <div>
              <span className="top-bar-label">Rendered</span>
              <span className="top-bar-mono">{catalog.elements.length.toLocaleString()}</span>
            </div>
            <div>
              <span className="top-bar-label">Placed by SGP4</span>
              <span className="top-bar-mono">{propagation.ok.toLocaleString()}</span>
            </div>
            <div>
              <span className="top-bar-label">Not placeable</span>
              {/* Reported rather than hidden: objects SGP4 refuses are dropped from the render,
                  and a viewer counting points deserves to know why the totals differ. */}
              <span className="top-bar-mono">{propagation.failed.toLocaleString()}</span>
            </div>
            <div>
              <span className="top-bar-label">Catalog age</span>
              <span className="top-bar-mono">
                {catalog.status?.ageHours === null || catalog.status === null
                  ? '—'
                  : `${catalog.status.ageHours.toFixed(1)} h`}
              </span>
            </div>
          </div>
        </div>
      </div>

      <aside className="catalog-rail catalog-rail-right" aria-label="Selected object">
        <SatelliteDetail detail={detail} loading={detailLoading} error={detailError} />
      </aside>
    </div>
  );
}

export function OrbitalCatalogPage() {
  // The clock is provided here rather than app-wide: it belongs to this view, and mounting it
  // globally would leave a timer running for pages that never read it.
  return (
    <SimClockProvider>
      <OrbitalCatalogInner />
    </SimClockProvider>
  );
}
