/**
 * The metadata panel for a selected catalogue object.
 *
 * Two rules govern everything here. First, a field the provider did not supply reads
 * "Not available" — never a plausible substitute, never a blank that could be mistaken for
 * zero. Second, every value states where it came from: the element set is catalogue data at a
 * stated epoch, and the position is PROPAGATED from it, not observed. A panel that showed
 * latitude and longitude without saying which of those two things it was would be the exact
 * misrepresentation this project's architecture rules exist to prevent.
 */

import { useEffect, useMemo, useState } from 'react';

import { compileElements, propagateState, type SatelliteState } from '../../catalog/propagation';
import type { CatalogObjectDetail } from '../../catalog/types';
import { useSimClock, formatSimTime } from '../../state/SimClockContext';

const NOT_AVAILABLE = 'Not available';

function text(value: string | null | undefined): string {
  return value === null || value === undefined || value === '' ? NOT_AVAILABLE : value;
}

function num(value: number | null | undefined, unit: string, digits = 2): string {
  if (value === null || value === undefined || !Number.isFinite(value)) return NOT_AVAILABLE;
  return `${value.toFixed(digits)} ${unit}`;
}

/** Expands the catalog's status vocabulary into something an operator reads without a key. */
function statusLabel(status: string | null): string {
  if (status === null) return NOT_AVAILABLE;
  return status.replace(/_/g, ' ').toLowerCase().replace(/^./, (c) => c.toUpperCase());
}

export interface SatelliteDetailProps {
  readonly detail: CatalogObjectDetail | null;
  readonly loading: boolean;
  readonly error: string | null;
}

export function SatelliteDetail({ detail, loading, error }: SatelliteDetailProps) {
  const { handle, displayEpochMs } = useSimClock();
  const [state, setState] = useState<SatelliteState | null>(null);

  const satrec = useMemo(() => {
    if (!detail) return null;
    return (
      compileElements([
        {
          id: detail.noradId,
          name: detail.name,
          l1: detail.tle.line1,
          l2: detail.tle.line2,
          cls: detail.catalogClass,
          rel: detail.missionRelevance,
          reg: detail.derived.regime,
          inc: detail.elements.inclinationDeg,
          ecc: detail.elements.eccentricity,
          per: detail.derived.perigeeAltitudeKm,
          apo: detail.derived.apogeeAltitudeKm,
          pmin: detail.derived.periodMinutes,
        },
      ])[0]?.satrec ?? null
    );
  }, [detail]);

  // Recomputed from the published display timestamp rather than per frame: a human cannot read
  // a latitude that changes sixty times a second, and this panel is not on the render path.
  useEffect(() => {
    if (!satrec) {
      setState(null);
      return;
    }
    setState(propagateState(satrec, new Date(handle.current.epochMs)));
  }, [satrec, displayEpochMs, handle]);

  if (loading) {
    return (
      <div className="catalog-detail" aria-busy="true">
        <p className="muted">Loading object record…</p>
      </div>
    );
  }

  if (error) {
    return (
      <div className="catalog-detail">
        <h2>Object record unavailable</h2>
        <p className="catalog-detail-error" role="alert">
          {error}
        </p>
      </div>
    );
  }

  if (!detail) {
    return (
      <div className="catalog-detail">
        <h2>No object selected</h2>
        <p className="muted">
          Select a point in the orbital view, or search by name or NORAD number, to inspect an
          object&apos;s element set and propagated state.
        </p>
      </div>
    );
  }

  const epochAgeHours = (Date.now() - Date.parse(detail.epoch)) / 3_600_000;

  return (
    <div className="catalog-detail">
      <div className="catalog-detail-head">
        <span className={`catalog-swatch catalog-swatch-${detail.catalogClass.toLowerCase()}`} aria-hidden="true" />
        <div>
          <h2>{detail.name}</h2>
          <p className="catalog-detail-sub">
            NORAD {detail.noradId} · {text(detail.intlDesignator)}
          </p>
        </div>
      </div>

      <p className="catalog-class-reason">{detail.classReason}</p>

      <section>
        <h3>Identity</h3>
        <dl className="catalog-dl">
          <div><dt>Object type</dt><dd>{text(detail.metadata.objectType)}</dd></div>
          <div><dt>Operator / owner</dt><dd>{text(detail.metadata.owner)}</dd></div>
          <div><dt>Operational status</dt><dd>{statusLabel(detail.metadata.opsStatus)}</dd></div>
          <div><dt>Launch date</dt><dd>{text(detail.metadata.launchDate)}</dd></div>
          <div><dt>Launch site</dt><dd>{text(detail.metadata.launchSite)}</dd></div>
          <div><dt>Decay date</dt><dd>{text(detail.metadata.decayDate)}</dd></div>
          <div>
            <dt>Radar cross-section</dt>
            <dd>{detail.metadata.rcsM2 === null ? NOT_AVAILABLE : num(detail.metadata.rcsM2, 'm²', 4)}</dd>
          </div>
          <div><dt>Mission relevance</dt><dd>{detail.missionRelevance.replace(/_/g, ' ')}</dd></div>
        </dl>
      </section>

      <section>
        <h3>Orbit</h3>
        <dl className="catalog-dl">
          <div><dt>Regime</dt><dd>{detail.derived.regime}</dd></div>
          <div><dt>Inclination</dt><dd>{num(detail.elements.inclinationDeg, '°', 4)}</dd></div>
          <div><dt>Eccentricity</dt><dd>{detail.elements.eccentricity.toFixed(7)}</dd></div>
          <div><dt>RAAN</dt><dd>{num(detail.elements.raanDeg, '°', 4)}</dd></div>
          <div><dt>Argument of perigee</dt><dd>{num(detail.elements.argPerigeeDeg, '°', 4)}</dd></div>
          <div><dt>Mean motion</dt><dd>{num(detail.elements.meanMotionRevPerDay, 'rev/day', 8)}</dd></div>
          <div><dt>Semi-major axis</dt><dd>{num(detail.derived.semiMajorAxisKm, 'km', 1)}</dd></div>
          <div><dt>Perigee altitude</dt><dd>{num(detail.derived.perigeeAltitudeKm, 'km', 1)}</dd></div>
          <div><dt>Apogee altitude</dt><dd>{num(detail.derived.apogeeAltitudeKm, 'km', 1)}</dd></div>
          <div><dt>Period</dt><dd>{num(detail.derived.periodMinutes, 'min', 2)}</dd></div>
          <div><dt>B*</dt><dd>{detail.elements.bstar.toExponential(4)}</dd></div>
        </dl>
      </section>

      <section>
        <h3>
          Propagated state
          <span className="catalog-provenance">SGP4 from catalogue elements — not telemetry</span>
        </h3>
        {state === null ? (
          <p className="muted">
            SGP4 cannot place this object at the selected time. Objects that have decayed, or
            whose element set is far from its epoch, fall outside the model&apos;s validity.
          </p>
        ) : (
          <dl className="catalog-dl">
            <div><dt>At</dt><dd className="top-bar-mono">{formatSimTime(displayEpochMs)}</dd></div>
            <div><dt>Latitude</dt><dd>{num(state.latitudeDeg, '°', 4)}</dd></div>
            <div><dt>Longitude</dt><dd>{num(state.longitudeDeg, '°', 4)}</dd></div>
            <div><dt>Altitude</dt><dd>{num(state.altitudeKm, 'km', 1)}</dd></div>
            <div><dt>Speed</dt><dd>{num(state.velocityKmS, 'km/s', 3)}</dd></div>
          </dl>
        )}
      </section>

      <section>
        <h3>Provenance</h3>
        <dl className="catalog-dl">
          <div><dt>Data source</dt><dd>{detail.source}</dd></div>
          <div><dt>Element set epoch</dt><dd className="top-bar-mono">{detail.epoch}</dd></div>
          <div>
            <dt>Epoch age</dt>
            <dd>
              {Number.isFinite(epochAgeHours) ? `${epochAgeHours.toFixed(1)} h` : NOT_AVAILABLE}
              {epochAgeHours > 72 && (
                // SGP4 error grows with time from epoch; past a few days it is worth saying so
                // rather than presenting a position with unstated uncertainty.
                <span className="catalog-warn"> · accuracy degrades far from epoch</span>
              )}
            </dd>
          </div>
          <div><dt>Retrieved</dt><dd className="top-bar-mono">{detail.fetchedAt}</dd></div>
        </dl>
        <details className="catalog-tle">
          <summary>Two-line element set</summary>
          <pre>{`${detail.tle.line1}\n${detail.tle.line2}`}</pre>
        </details>
      </section>
    </div>
  );
}
