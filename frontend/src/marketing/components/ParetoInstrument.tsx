/**
 * The trade-space instrument: a plot of the two conflicting cost objectives, the exact
 * nondominated set drawn over it, and an operator-preference control that selects *from* that
 * set.
 *
 * The ordering of those three things is the argument, not a layout choice. ORCA "computes the
 * Pareto-optimal set first, then applies operator preference" (README); this control makes the
 * two stages separately visible, so a reader can see that moving the preference never changes
 * which points are on the front — it only changes which front point is chosen.
 *
 * Three properties are load-bearing and must survive any edit here:
 *
 * 1. The frontier is computed by `paretoFrontier()`, never enumerated by hand. If the fixture
 *    changes, the drawn polyline changes with it.
 * 2. Every candidate is an operable control, not a decorated dot. A trade-space plot whose
 *    points cannot be inspected is a picture of an instrument rather than one.
 * 3. The fixture is labelled as a fixture in permanently visible text. The engine is not
 *    reachable from this page and nothing here may imply that it is (house rule 4).
 */

import { useCallback, useId, useMemo, useState } from 'react';
import type { CSSProperties, KeyboardEvent } from 'react';

import {
  DEMO_CANDIDATES,
  DEMO_DISCLOSURE,
  DEMO_PRIORITIES,
  paretoFrontier,
  preferredCandidate,
} from '../data/paretoDemo';
import type { DemoCandidate, DemoPriority } from '../data/paretoDemo';
import { TelemetryReadout } from './TelemetryReadout';

/* --- Geometry -------------------------------------------------------------
   User units, not pixels: the SVG scales with its container, so these numbers only fix the
   plot's proportions. The margins are sized for the widest tick label the y axis can produce
   plus a rotated axis title. */

const VIEW_WIDTH = 700;
const VIEW_HEIGHT = 470;
const MARGIN_LEFT = 76;
const MARGIN_RIGHT = 26;
const MARGIN_TOP = 34;
const MARGIN_BOTTOM = 64;

const PLOT_WIDTH = VIEW_WIDTH - MARGIN_LEFT - MARGIN_RIGHT;
const PLOT_HEIGHT = VIEW_HEIGHT - MARGIN_TOP - MARGIN_BOTTOM;
const PLOT_RIGHT = MARGIN_LEFT + PLOT_WIDTH;
const PLOT_BOTTOM = MARGIN_TOP + PLOT_HEIGHT;

/** Roughly six intervals reads as a graticule; fewer looks sparse, more turns into hatching. */
const TARGET_TICKS = 6;

/**
 * Presentation bands for the two objectives that are not on an axis. They belong to the
 * illustrative fixture, not to the engine: real feasibility is a hard constraint applied at
 * graph-build time, so an infeasible plan is never a candidate at all and never needs a
 * display threshold (docs/OPTIMIZATION_MODEL.md §1.1, reason 3).
 */
const RISK_BAND_LIMIT = 0.45;
const RECOVERY_BAND_FLOOR = 0.7;

interface AxisScale {
  readonly min: number;
  readonly max: number;
  readonly ticks: readonly number[];
  readonly decimals: number;
}

/** 1-2-5 rounding, the convention every plotting library converges on because those are the
 * intervals a reader can subdivide mentally. */
function niceStep(rawStep: number): number {
  if (!Number.isFinite(rawStep) || rawStep <= 0) return 1;
  const magnitude = Math.pow(10, Math.floor(Math.log10(rawStep)));
  const normalised = rawStep / magnitude;
  const stepped = normalised <= 1 ? 1 : normalised <= 2 ? 2 : normalised <= 5 ? 5 : 10;
  return stepped * magnitude;
}

/**
 * Domain snapped outward to whole steps, so the axis ends on a labelled tick rather than on
 * whatever the extreme datum happened to be. Ticks are generated from an index rather than by
 * accumulating the step, which would drift into values like 8.000000000000002.
 */
function buildScale(values: readonly number[], targetTicks: number): AxisScale {
  let min = Number.POSITIVE_INFINITY;
  let max = Number.NEGATIVE_INFINITY;
  for (const value of values) {
    if (value < min) min = value;
    if (value > max) max = value;
  }
  if (!Number.isFinite(min) || !Number.isFinite(max)) {
    return { min: 0, max: 1, ticks: [0, 1], decimals: 0 };
  }

  const step = niceStep(max > min ? (max - min) / targetTicks : 1);
  const low = Math.floor(min / step) * step;
  // A degenerate domain (every value identical) would collapse the axis and divide by zero in
  // the projection, so it is widened by one step instead.
  const high = Math.ceil(max / step) * step > low ? Math.ceil(max / step) * step : low + step;
  const decimals = Math.max(0, -Math.floor(Math.log10(step)));
  const count = Math.max(1, Math.round((high - low) / step));

  const ticks: number[] = [];
  for (let index = 0; index <= count; index += 1) {
    ticks.push(Number((low + index * step).toFixed(decimals)));
  }
  return { min: low, max: high, ticks, decimals };
}

function projectX(hours: number, scale: AxisScale): number {
  return MARGIN_LEFT + ((hours - scale.min) / (scale.max - scale.min)) * PLOT_WIDTH;
}

/** Delta-v increases upward, against the SVG y direction: a cost plot that ran downward would
 * read as "more is better" to anyone scanning it. */
function projectY(deltaV: number, scale: AxisScale): number {
  return MARGIN_TOP + (1 - (deltaV - scale.min) / (scale.max - scale.min)) * PLOT_HEIGHT;
}

/* --- Derived fixture state ------------------------------------------------
   Computed once at module scope. The frontier is a pure function of a frozen fixture, so
   recomputing it per render would be work with no possible different answer. */

const FRONTIER = paretoFrontier(DEMO_CANDIDATES);
const FRONTIER_IDS: ReadonlySet<string> = new Set(FRONTIER.map((candidate) => candidate.id));

/** Ascending delta-v, which is also ascending along the frontier — so tab order walks the
 * trade from the cheapest, slowest plan to the fastest, most expensive one. */
const ORDERED_CANDIDATES: readonly DemoCandidate[] = [...DEMO_CANDIDATES].sort(
  (a, b) => a.deltaVMs - b.deltaVMs || a.transferHours - b.transferHours,
);

const INITIAL_PRIORITY: DemoPriority = 'balanced';
const INITIAL_SELECTION =
  preferredCandidate(DEMO_CANDIDATES, INITIAL_PRIORITY)?.id ?? ORDERED_CANDIDATES[0]?.id ?? '';

const TIME_SCALE = buildScale(
  DEMO_CANDIDATES.map((candidate) => candidate.transferHours),
  TARGET_TICKS,
);
const DELTA_V_SCALE = buildScale(
  DEMO_CANDIDATES.map((candidate) => candidate.deltaVMs),
  TARGET_TICKS,
);

/** How the selection came about. Shown because "the preference moved it" and "the operator
 * overrode the preference" are different events and an instrument should not conflate them. */
type SelectionOrigin = 'preference' | 'manual';

function formatFixed(value: number, decimals: number): string {
  return value.toFixed(decimals);
}

function describeCandidate(candidate: DemoCandidate, optimal: boolean, selected: boolean): string {
  const status = optimal ? 'Pareto-optimal solution.' : 'Dominated candidate.';
  const selection = selected ? ' Currently selected.' : '';
  return (
    `${candidate.id}. Delta-v ${candidate.deltaVMs} metres per second, ` +
    `transfer time ${formatFixed(candidate.transferHours, 1)} hours, ` +
    `risk index ${formatFixed(candidate.riskIndex, 2)}, ` +
    `recovery index ${formatFixed(candidate.recoveryIndex, 2)}, ` +
    `propellant reserve ${formatFixed(candidate.fuelReservePct, 1)} percent. ` +
    `${status}${selection}`
  );
}

export interface ParetoInstrumentProps {
  /** id of the heading that names this instrument, for the plot's accessible name. */
  readonly labelledBy?: string;
}

export function ParetoInstrument({ labelledBy }: ParetoInstrumentProps) {
  // `useId()` emits colons, which are legal in an id attribute but hostile inside `url(#…)`
  // references, so they are stripped once here rather than at every use site.
  const uid = useId().replace(/:/g, '');
  const [priority, setPriority] = useState<DemoPriority>(INITIAL_PRIORITY);
  const [selectedId, setSelectedId] = useState<string>(INITIAL_SELECTION);
  const [origin, setOrigin] = useState<SelectionOrigin>('preference');

  const applyPriority = useCallback((next: DemoPriority) => {
    setPriority(next);
    const chosen = preferredCandidate(DEMO_CANDIDATES, next);
    if (chosen) {
      setSelectedId(chosen.id);
      setOrigin('preference');
    }
  }, []);

  const selectCandidate = useCallback((candidate: DemoCandidate) => {
    setSelectedId(candidate.id);
    setOrigin('manual');
  }, []);

  const handleNodeKeyDown = useCallback(
    (event: KeyboardEvent<SVGGElement>, candidate: DemoCandidate) => {
      if (event.key !== 'Enter' && event.key !== ' ' && event.key !== 'Spacebar') return;
      // Space scrolls the page by default; an activated control must not also move the view.
      event.preventDefault();
      selectCandidate(candidate);
    },
    [selectCandidate],
  );

  const selected = useMemo(
    () => DEMO_CANDIDATES.find((candidate) => candidate.id === selectedId) ?? null,
    [selectedId],
  );

  const activeOption = useMemo(
    () => DEMO_PRIORITIES.find((option) => option.value === priority) ?? null,
    [priority],
  );

  const frontierPath = useMemo(() => {
    const points = FRONTIER.map(
      (candidate) =>
        `${projectX(candidate.transferHours, TIME_SCALE).toFixed(2)} ` +
        `${projectY(candidate.deltaVMs, DELTA_V_SCALE).toFixed(2)}`,
    );
    return points.length > 0 ? `M ${points.join(' L ')}` : '';
  }, []);

  const lowDeltaV = FRONTIER[0] ?? null;
  const minimumTime = FRONTIER[FRONTIER.length - 1] ?? null;

  const gradientId = `${uid}-frontier-ramp`;
  const plotTitleId = `${uid}-plot-title`;

  const selectedX = selected ? projectX(selected.transferHours, TIME_SCALE) : 0;
  const selectedY = selected ? projectY(selected.deltaVMs, DELTA_V_SCALE) : 0;
  const selectedOptimal = selected !== null && FRONTIER_IDS.has(selected.id);

  const riskWithinBand = selected !== null && selected.riskIndex <= RISK_BAND_LIMIT;
  const recoveryMeetsFloor = selected !== null && selected.recoveryIndex >= RECOVERY_BAND_FLOOR;

  // Gradient endpoints follow the frontier itself rather than the bounding box, so the ramp
  // reads along the trade instead of across the plot.
  const rampStart = lowDeltaV
    ? {
        x: projectX(lowDeltaV.transferHours, TIME_SCALE),
        y: projectY(lowDeltaV.deltaVMs, DELTA_V_SCALE),
      }
    : { x: MARGIN_LEFT, y: PLOT_BOTTOM };
  const rampEnd = minimumTime
    ? {
        x: projectX(minimumTime.transferHours, TIME_SCALE),
        y: projectY(minimumTime.deltaVMs, DELTA_V_SCALE),
      }
    : { x: PLOT_RIGHT, y: MARGIN_TOP };

  return (
    <div className="mk-instrument">
      <div className="mk-instrument__header">
        <p className="mk-eyebrow">Trade space · {DEMO_CANDIDATES.length} candidate transfers</p>
        <p className="mk-instrument__meta mk-mono">
          {FRONTIER.length} nondominated · {DEMO_CANDIDATES.length - FRONTIER.length} dominated
        </p>
      </div>

      <fieldset className="mk-instrument__preference">
        <legend className="mk-instrument__preference-legend">
          Operator preference profile
        </legend>
        <div className="mk-instrument__segments">
          {DEMO_PRIORITIES.map((option) => {
            const inputId = `${uid}-priority-${option.value}`;
            return (
              <div className="mk-instrument__segment" key={option.value}>
                <input
                  className="mk-instrument__segment-input"
                  type="radio"
                  id={inputId}
                  name={`${uid}-priority`}
                  value={option.value}
                  checked={priority === option.value}
                  onChange={() => applyPriority(option.value)}
                />
                <label className="mk-instrument__segment-label" htmlFor={inputId}>
                  {option.label}
                </label>
              </div>
            );
          })}
        </div>
        <p className="mk-instrument__preference-note">
          {activeOption?.description ?? ''} Preference is applied after the front is computed and
          never changes which points are on it.
        </p>
      </fieldset>

      <div className="mk-instrument__plot-scroll">
        <svg
          className="mk-instrument__plot"
          viewBox={`0 0 ${VIEW_WIDTH} ${VIEW_HEIGHT}`}
          role="group"
          aria-labelledby={labelledBy ? `${labelledBy} ${plotTitleId}` : plotTitleId}
        >
          <title id={plotTitleId}>
            Transfer time against delta-v for {DEMO_CANDIDATES.length} illustrative candidate
            transfers, with the computed Pareto frontier. Both objectives are minimised, so the
            unattainable ideal sits at the lower left.
          </title>

          <defs>
            <linearGradient
              id={gradientId}
              gradientUnits="userSpaceOnUse"
              x1={rampStart.x}
              y1={rampStart.y}
              x2={rampEnd.x}
              y2={rampEnd.y}
            >
              <stop className="mk-instrument__stop mk-instrument__stop--1" offset="0" />
              <stop className="mk-instrument__stop mk-instrument__stop--2" offset="0.24" />
              <stop className="mk-instrument__stop mk-instrument__stop--3" offset="0.46" />
              <stop className="mk-instrument__stop mk-instrument__stop--4" offset="0.66" />
              <stop className="mk-instrument__stop mk-instrument__stop--5" offset="0.85" />
              <stop className="mk-instrument__stop mk-instrument__stop--6" offset="1" />
            </linearGradient>
          </defs>

          <g className="mk-instrument__graticule" aria-hidden="true">
            {TIME_SCALE.ticks.map((tick) => (
              <line
                key={`gx-${tick}`}
                x1={projectX(tick, TIME_SCALE)}
                y1={MARGIN_TOP}
                x2={projectX(tick, TIME_SCALE)}
                y2={PLOT_BOTTOM}
              />
            ))}
            {DELTA_V_SCALE.ticks.map((tick) => (
              <line
                key={`gy-${tick}`}
                x1={MARGIN_LEFT}
                y1={projectY(tick, DELTA_V_SCALE)}
                x2={PLOT_RIGHT}
                y2={projectY(tick, DELTA_V_SCALE)}
              />
            ))}
          </g>

          <g className="mk-instrument__axis" aria-hidden="true">
            <line
              className="mk-instrument__axis-line"
              x1={MARGIN_LEFT}
              y1={PLOT_BOTTOM}
              x2={PLOT_RIGHT}
              y2={PLOT_BOTTOM}
            />
            <line
              className="mk-instrument__axis-line"
              x1={MARGIN_LEFT}
              y1={MARGIN_TOP}
              x2={MARGIN_LEFT}
              y2={PLOT_BOTTOM}
            />
            {TIME_SCALE.ticks.map((tick) => (
              <g key={`tx-${tick}`}>
                <line
                  className="mk-instrument__tick"
                  x1={projectX(tick, TIME_SCALE)}
                  y1={PLOT_BOTTOM}
                  x2={projectX(tick, TIME_SCALE)}
                  y2={PLOT_BOTTOM + 6}
                />
                <text
                  className="mk-instrument__tick-label"
                  x={projectX(tick, TIME_SCALE)}
                  y={PLOT_BOTTOM + 22}
                  textAnchor="middle"
                >
                  {formatFixed(tick, TIME_SCALE.decimals)}
                </text>
              </g>
            ))}
            {DELTA_V_SCALE.ticks.map((tick) => (
              <g key={`ty-${tick}`}>
                <line
                  className="mk-instrument__tick"
                  x1={MARGIN_LEFT - 6}
                  y1={projectY(tick, DELTA_V_SCALE)}
                  x2={MARGIN_LEFT}
                  y2={projectY(tick, DELTA_V_SCALE)}
                />
                <text
                  className="mk-instrument__tick-label"
                  x={MARGIN_LEFT - 12}
                  y={projectY(tick, DELTA_V_SCALE) + 4}
                  textAnchor="end"
                >
                  {formatFixed(tick, DELTA_V_SCALE.decimals)}
                </text>
              </g>
            ))}
            <text
              className="mk-instrument__axis-title"
              x={MARGIN_LEFT + PLOT_WIDTH / 2}
              y={VIEW_HEIGHT - 16}
              textAnchor="middle"
            >
              TRANSFER TIME (H) — MINIMISE
            </text>
            <text
              className="mk-instrument__axis-title"
              transform={`translate(20 ${MARGIN_TOP + PLOT_HEIGHT / 2}) rotate(-90)`}
              textAnchor="middle"
            >
              DELTA-V (M/S) — MINIMISE
            </text>
          </g>

          {/* The ideal point: best on both axes at once, and empty for exactly the reason the
              section argues. Marking it is what turns a scatter into a trade-space plot. */}
          <g className="mk-instrument__utopia" aria-hidden="true">
            <line x1={MARGIN_LEFT + 8} y1={PLOT_BOTTOM - 8} x2={MARGIN_LEFT + 26} y2={PLOT_BOTTOM - 8} />
            <line x1={MARGIN_LEFT + 17} y1={PLOT_BOTTOM - 17} x2={MARGIN_LEFT + 17} y2={PLOT_BOTTOM + 1} />
            <text className="mk-instrument__utopia-label" x={MARGIN_LEFT + 32} y={PLOT_BOTTOM - 5}>
              IDEAL POINT — UNATTAINABLE
            </text>
          </g>

          {frontierPath ? (
            <path
              className="mk-instrument__frontier"
              d={frontierPath}
              stroke={`url(#${gradientId})`}
              aria-hidden="true"
            />
          ) : null}

          {selected ? (
            <g className="mk-instrument__crosshair" aria-hidden="true">
              {/* Full-extent guides rather than two stubs from the point to the axes. The
                  vertical one can then carry its value at the top of the frame, which is the
                  only band along it that the ideal-point marker never occupies -- at the foot
                  the two collide for any selection under about six hours. */}
              <line x1={MARGIN_LEFT} y1={selectedY} x2={PLOT_RIGHT} y2={selectedY} />
              <line x1={selectedX} y1={MARGIN_TOP} x2={selectedX} y2={PLOT_BOTTOM} />
              {/* The time reading flips to the left of its guide once the selection is far
                  enough right that the label would overrun the frame. */}
              <text
                className="mk-instrument__crosshair-label"
                x={MARGIN_LEFT + 6}
                y={selectedY < MARGIN_TOP + 22 ? selectedY + 18 : selectedY - 8}
              >
                {selected.deltaVMs} M/S
              </text>
              <text
                className="mk-instrument__crosshair-label"
                x={selectedX > MARGIN_LEFT + PLOT_WIDTH * 0.82 ? selectedX - 7 : selectedX + 7}
                y={MARGIN_TOP + 14}
                textAnchor={
                  selectedX > MARGIN_LEFT + PLOT_WIDTH * 0.82 ? 'end' : 'start'
                }
              >
                {formatFixed(selected.transferHours, 1)} H
              </text>
            </g>
          ) : null}

          {lowDeltaV ? (
            <g className="mk-instrument__annotation" aria-hidden="true">
              <line
                x1={projectX(lowDeltaV.transferHours, TIME_SCALE) - 12}
                y1={projectY(lowDeltaV.deltaVMs, DELTA_V_SCALE) - 14}
                x2={projectX(lowDeltaV.transferHours, TIME_SCALE) - 4}
                y2={projectY(lowDeltaV.deltaVMs, DELTA_V_SCALE) - 6}
              />
              <text
                x={projectX(lowDeltaV.transferHours, TIME_SCALE) - 16}
                y={projectY(lowDeltaV.deltaVMs, DELTA_V_SCALE) - 18}
                textAnchor="end"
              >
                LOW DELTA-V
              </text>
            </g>
          ) : null}

          {minimumTime ? (
            <g className="mk-instrument__annotation" aria-hidden="true">
              <line
                x1={projectX(minimumTime.transferHours, TIME_SCALE) + 6}
                y1={projectY(minimumTime.deltaVMs, DELTA_V_SCALE) + 6}
                x2={projectX(minimumTime.transferHours, TIME_SCALE) + 14}
                y2={projectY(minimumTime.deltaVMs, DELTA_V_SCALE) + 14}
              />
              <text
                x={projectX(minimumTime.transferHours, TIME_SCALE) + 18}
                y={projectY(minimumTime.deltaVMs, DELTA_V_SCALE) + 20}
                textAnchor="start"
              >
                MINIMUM TRANSFER TIME
              </text>
            </g>
          ) : null}

          <g>
            {ORDERED_CANDIDATES.map((candidate) => {
              const x = projectX(candidate.transferHours, TIME_SCALE);
              const y = projectY(candidate.deltaVMs, DELTA_V_SCALE);
              const optimal = FRONTIER_IDS.has(candidate.id);
              const isSelected = candidate.id === selectedId;
              const nodeClasses = [
                'mk-instrument__node',
                optimal ? 'mk-instrument__node--frontier' : 'mk-instrument__node--dominated',
                isSelected ? 'is-selected' : null,
              ]
                .filter((entry): entry is string => Boolean(entry))
                .join(' ');

              return (
                <g
                  key={candidate.id}
                  className={nodeClasses}
                  role="button"
                  tabIndex={0}
                  aria-current={isSelected ? 'true' : undefined}
                  aria-label={describeCandidate(candidate, optimal, isSelected)}
                  onClick={() => selectCandidate(candidate)}
                  onKeyDown={(event) => handleNodeKeyDown(event, candidate)}
                >
                  {/* Invisible target so a fingertip does not have to land on a 5-unit dot. */}
                  <circle className="mk-instrument__hit" cx={x} cy={y} r={15} />
                  {isSelected ? (
                    <circle className="mk-instrument__halo" cx={x} cy={y} r={10.5} />
                  ) : null}
                  <circle className="mk-instrument__point" cx={x} cy={y} r={optimal ? 4.6 : 3.4} />
                </g>
              );
            })}
          </g>
        </svg>
      </div>

      <ul className="mk-instrument__legend">
        <li className="mk-instrument__legend-item">
          <span className="mk-instrument__swatch mk-instrument__swatch--frontier" aria-hidden="true" />
          Pareto-optimal solution
        </li>
        <li className="mk-instrument__legend-item">
          <span
            className="mk-instrument__swatch mk-instrument__swatch--dominated"
            aria-hidden="true"
          />
          Dominated candidate
        </li>
        <li className="mk-instrument__legend-item">
          <span className="mk-instrument__swatch mk-instrument__swatch--selected" aria-hidden="true" />
          Selected plan
        </li>
      </ul>

      <div className="mk-instrument__panel" aria-live="polite">
        <p className="mk-instrument__origin mk-mono">
          {selected === null
            ? 'NO PLAN SELECTED'
            : `${selected.id} · ${
                selectedOptimal ? 'PARETO-OPTIMAL SOLUTION' : 'DOMINATED CANDIDATE'
              } · ${origin === 'preference' ? 'SELECTED BY PREFERENCE' : 'SELECTED BY OPERATOR'}`}
        </p>

        <div className="mk-instrument__readouts">
          <TelemetryReadout
            label="Delta-v"
            value={selected ? String(selected.deltaVMs) : '—'}
            unit="m/s"
          />
          <TelemetryReadout
            label="Transfer time"
            value={selected ? formatFixed(selected.transferHours, 1) : '—'}
            unit="h"
          />
          <TelemetryReadout
            label="Recovery index"
            value={selected ? formatFixed(selected.recoveryIndex, 2) : '—'}
            tone={recoveryMeetsFloor ? 'active' : 'neutral'}
          />
          <TelemetryReadout
            label="Propellant reserve"
            value={selected ? formatFixed(selected.fuelReservePct, 1) : '—'}
            unit="%"
          />
        </div>

        <div className="mk-instrument__bands">
          <div
            className={`mk-instrument__band${riskWithinBand ? '' : ' mk-instrument__band--breach'}`}
          >
            <p className="mk-instrument__band-head">
              <span className="mk-instrument__band-label">Acceptable risk</span>
              <span className="mk-instrument__band-state mk-mono">
                {selected === null
                  ? '—'
                  : `${formatFixed(selected.riskIndex, 2)} · ${
                      riskWithinBand ? 'WITHIN BAND' : 'ABOVE BAND'
                    }`}
              </span>
            </p>
            <div
              className="mk-instrument__band-track"
              style={
                {
                  '--mk-band-fill': `${(selected?.riskIndex ?? 0) * 100}%`,
                  '--mk-band-mark': `${RISK_BAND_LIMIT * 100}%`,
                } as CSSProperties
              }
            >
              <span className="mk-instrument__band-fill" />
              <span className="mk-instrument__band-mark" />
            </div>
          </div>

          <div
            className={`mk-instrument__band${
              recoveryMeetsFloor ? '' : ' mk-instrument__band--breach'
            }`}
          >
            <p className="mk-instrument__band-head">
              <span className="mk-instrument__band-label">High recovery probability</span>
              <span className="mk-instrument__band-state mk-mono">
                {selected === null
                  ? '—'
                  : `${formatFixed(selected.recoveryIndex, 2)} · ${
                      recoveryMeetsFloor ? 'MET' : 'BELOW BAND'
                    }`}
              </span>
            </p>
            <div
              className="mk-instrument__band-track"
              style={
                {
                  '--mk-band-fill': `${(selected?.recoveryIndex ?? 0) * 100}%`,
                  '--mk-band-mark': `${RECOVERY_BAND_FLOOR * 100}%`,
                } as CSSProperties
              }
            >
              <span className="mk-instrument__band-fill" />
              <span className="mk-instrument__band-mark" />
            </div>
          </div>
        </div>
      </div>

      <p className="mk-instrument__footnote">
        {DEMO_DISCLOSURE} The frontier drawn here is an exact nondominance test over the two
        plotted objectives only; risk, recovery and reserve are carried alongside but are not part
        of this dominance test, and the acceptance bands ({RISK_BAND_LIMIT.toFixed(2)} risk,{' '}
        {RECOVERY_BAND_FLOOR.toFixed(2)} recovery) are display thresholds belonging to the fixture.
        The real front is produced by the C++ engine&rsquo;s label-setting search over all four
        objectives and is reached only from the workspace.
      </p>
    </div>
  );
}
