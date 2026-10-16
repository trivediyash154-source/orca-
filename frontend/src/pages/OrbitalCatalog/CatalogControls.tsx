/**
 * Filter, legend and time-transport controls for the catalogue view.
 *
 * Every control here changes what the BRIDGE returns, not what the browser hides: an altitude
 * range is a query parameter, so narrowing it reduces what is transferred rather than dimming
 * points that were downloaded anyway. The one exception is the search box, which is debounced
 * before it becomes a request.
 */

import { useEffect, useId, useState } from 'react';

import type { CatalogClass, CatalogLegendResponse, OrbitRegime } from '../../catalog/types';
import { CLOCK_RATES, formatRate, formatSimTime, useSimClock } from '../../state/SimClockContext';

export interface CatalogFilterValues {
  readonly altMin: number;
  readonly altMax: number;
  readonly incMin: number;
  readonly incMax: number;
  readonly classes: readonly CatalogClass[];
  readonly regimes: readonly OrbitRegime[];
  readonly search: string;
  readonly limit: number;
}

export const DEFAULT_FILTERS: CatalogFilterValues = {
  altMin: 0,
  altMax: 2000,
  incMin: 0,
  incMax: 180,
  classes: ['NOMINAL', 'ATTENTION', 'UNKNOWN'],
  regimes: [],
  search: '',
  // Ten thousand is the brief's target and roughly what a mid-range GPU draws comfortably as
  // one point cloud; the bridge reports how many objects MATCHED so a truncated view says so.
  limit: 10000,
};

const ALL_CLASSES: readonly { value: CatalogClass; label: string }[] = [
  { value: 'NOMINAL', label: 'Operational' },
  { value: 'ATTENTION', label: 'Attention' },
  { value: 'UNKNOWN', label: 'Unknown' },
];

const ALL_REGIMES: readonly OrbitRegime[] = ['LEO', 'MEO', 'GEO', 'HEO'];

export interface CatalogControlsProps {
  readonly filters: CatalogFilterValues;
  readonly onChange: (next: CatalogFilterValues) => void;
  readonly legend: CatalogLegendResponse | null;
  readonly matched: number;
  readonly rendered: number;
}

export function CatalogControls({
  filters,
  onChange,
  legend,
  matched,
  rendered,
}: CatalogControlsProps) {
  const id = useId().replace(/:/g, '');
  const [searchDraft, setSearchDraft] = useState(filters.search);

  // Debounced: a request per keystroke would issue a dozen queries for one search term and
  // leave the bridge racing to answer terms the user has already replaced.
  useEffect(() => {
    const timer = window.setTimeout(() => {
      if (searchDraft !== filters.search) onChange({ ...filters, search: searchDraft });
    }, 300);
    return () => window.clearTimeout(timer);
  }, [searchDraft, filters, onChange]);

  const toggleClass = (value: CatalogClass) => {
    const has = filters.classes.includes(value);
    const next = has ? filters.classes.filter((c) => c !== value) : [...filters.classes, value];
    onChange({ ...filters, classes: next });
  };

  const toggleRegime = (value: OrbitRegime) => {
    const has = filters.regimes.includes(value);
    const next = has ? filters.regimes.filter((r) => r !== value) : [...filters.regimes, value];
    onChange({ ...filters, regimes: next });
  };

  return (
    <div className="catalog-controls">
      <section>
        <h3>Search</h3>
        <label className="field-label" htmlFor={`${id}-search`}>
          Name or NORAD number
        </label>
        <input
          id={`${id}-search`}
          type="search"
          className="catalog-input"
          placeholder="ISS, 25544, 1998-067A"
          value={searchDraft}
          onChange={(e) => setSearchDraft(e.target.value)}
        />
      </section>

      <section>
        <h3>Altitude</h3>
        <p className="catalog-range-value" aria-live="polite">
          {filters.altMin} – {filters.altMax} km
        </p>
        <label className="field-label" htmlFor={`${id}-altmin`}>
          Minimum altitude, km
        </label>
        <input
          id={`${id}-altmin`}
          type="range"
          min={0}
          max={40000}
          step={50}
          value={filters.altMin}
          onChange={(e) =>
            onChange({ ...filters, altMin: Math.min(Number(e.target.value), filters.altMax) })
          }
        />
        <label className="field-label" htmlFor={`${id}-altmax`}>
          Maximum altitude, km
        </label>
        <input
          id={`${id}-altmax`}
          type="range"
          min={0}
          max={40000}
          step={50}
          value={filters.altMax}
          onChange={(e) =>
            onChange({ ...filters, altMax: Math.max(Number(e.target.value), filters.altMin) })
          }
        />
      </section>

      <section>
        <h3>Inclination</h3>
        <p className="catalog-range-value" aria-live="polite">
          {filters.incMin}° – {filters.incMax}°
        </p>
        <label className="field-label" htmlFor={`${id}-incmin`}>
          Minimum inclination, degrees
        </label>
        <input
          id={`${id}-incmin`}
          type="range"
          min={0}
          max={180}
          step={1}
          value={filters.incMin}
          onChange={(e) =>
            onChange({ ...filters, incMin: Math.min(Number(e.target.value), filters.incMax) })
          }
        />
        <label className="field-label" htmlFor={`${id}-incmax`}>
          Maximum inclination, degrees
        </label>
        <input
          id={`${id}-incmax`}
          type="range"
          min={0}
          max={180}
          step={1}
          value={filters.incMax}
          onChange={(e) =>
            onChange({ ...filters, incMax: Math.max(Number(e.target.value), filters.incMin) })
          }
        />
      </section>

      <section>
        <h3>Classification</h3>
        <div className="catalog-chips" role="group" aria-label="Filter by classification">
          {ALL_CLASSES.map((c) => (
            <button
              key={c.value}
              type="button"
              className={filters.classes.includes(c.value) ? 'catalog-chip active' : 'catalog-chip'}
              aria-pressed={filters.classes.includes(c.value)}
              onClick={() => toggleClass(c.value)}
            >
              <span className={`catalog-swatch catalog-swatch-${c.value.toLowerCase()}`} aria-hidden="true" />
              {c.label}
            </button>
          ))}
        </div>
      </section>

      <section>
        <h3>Orbit regime</h3>
        <div className="catalog-chips" role="group" aria-label="Filter by orbit regime">
          {ALL_REGIMES.map((r) => (
            <button
              key={r}
              type="button"
              className={filters.regimes.includes(r) ? 'catalog-chip active' : 'catalog-chip'}
              aria-pressed={filters.regimes.includes(r)}
              onClick={() => toggleRegime(r)}
            >
              {r}
            </button>
          ))}
        </div>
        <p className="muted catalog-hint">No regime selected means all regimes.</p>
      </section>

      <section>
        <h3>Result</h3>
        <p className="catalog-count" aria-live="polite">
          <strong>{rendered.toLocaleString()}</strong> rendered
          {matched > rendered && (
            <>
              {' '}
              of <strong>{matched.toLocaleString()}</strong> matched
              {/* Saying so matters: a view showing 10,000 of 16,000 objects that claimed to
                  show "the catalog" would be quietly wrong. */}
              <span className="catalog-warn"> · limited to {filters.limit.toLocaleString()}</span>
            </>
          )}
        </p>
      </section>

      {legend && (
        <section>
          <h3>Legend</h3>
          <ul className="catalog-legend">
            {legend.rules.map((rule) => (
              <li key={rule.id}>
                <span
                  className={`catalog-swatch catalog-swatch-${rule.result.toLowerCase()}`}
                  aria-hidden="true"
                />
                <span>
                  <strong>{rule.label}</strong>
                  <span className="catalog-legend-reason">{rule.reason}</span>
                </span>
              </li>
            ))}
          </ul>
          <p className="muted catalog-hint">
            Served from the bridge&apos;s own classification rules, so a colour and its
            explanation cannot disagree.
          </p>
        </section>
      )}
    </div>
  );
}

/** Transport controls for the simulation clock. */
export function TimeControls() {
  const { playing, rate, play, pause, setRate, step, reset, displayEpochMs, isLive } = useSimClock();
  const id = useId().replace(/:/g, '');

  return (
    <div className="catalog-time">
      <div className="catalog-time-readout">
        <span className="top-bar-label">Simulation time (UTC)</span>
        <span className="top-bar-mono catalog-time-value">{formatSimTime(displayEpochMs)}</span>
        <span className="catalog-time-rate">
          {formatRate(rate, playing)}
          {isLive ? ' · at present' : ' · offset from present'}
        </span>
      </div>

      <div className="catalog-time-buttons" role="group" aria-label="Simulation time transport">
        <button type="button" onClick={() => step(-3600)} aria-label="Step back one hour">
          −1h
        </button>
        <button type="button" onClick={() => step(-60)} aria-label="Step back one minute">
          −1m
        </button>
        <button
          type="button"
          className="catalog-time-primary"
          onClick={playing ? pause : play}
          aria-pressed={playing}
        >
          {playing ? 'Pause' : 'Play'}
        </button>
        <button type="button" onClick={() => step(60)} aria-label="Step forward one minute">
          +1m
        </button>
        <button type="button" onClick={() => step(3600)} aria-label="Step forward one hour">
          +1h
        </button>
        <button type="button" onClick={reset} aria-label="Reset to the present instant">
          Now
        </button>
      </div>

      <div className="catalog-time-rate-select">
        <label className="field-label" htmlFor={`${id}-rate`}>
          Time acceleration
        </label>
        <select
          id={`${id}-rate`}
          value={rate}
          onChange={(e) => {
            setRate(Number(e.target.value));
            if (Number(e.target.value) !== 0) play();
          }}
        >
          {CLOCK_RATES.map((r) => (
            <option key={r} value={r}>
              {r === 0 ? 'Held' : r === 1 ? 'Real time (1x)' : `${r > 0 ? '' : '−'}${Math.abs(r)}x`}
            </option>
          ))}
        </select>
      </div>
    </div>
  );
}
