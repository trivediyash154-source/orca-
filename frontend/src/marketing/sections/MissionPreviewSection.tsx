/**
 * Section 04 — a composed preview of the mission workspace.
 *
 * This surface is deliberately the most constrained thing on the page. It shows the *shape* of
 * the operator interface — a parameter rail, a spatial panel, an objectives column, a candidate
 * strip, a phase timeline — and nothing else. It is not connected to the engine, it holds no
 * telemetry, and it issues no command. That is stated in a header chip and again in the footer,
 * in visible text, because a control-room composition is exactly the kind of image a reader
 * would otherwise assume was live (house rule 4).
 *
 * Two implementation constraints follow from that framing:
 *
 * 1. No second WebGL canvas. The hero already owns one; a second `<Canvas>` costs a second
 *    renderer, a second RAF loop and a second GL context on a page whose job is to load fast.
 *    The orbital view here is inline SVG in the same visual language.
 * 2. Every control does something real. A scenario change reloads the parameters, the candidate
 *    set and the timeline; a constraint toggle actually filters the candidate strip and moves
 *    the revision line. Nothing on this panel is a painted affordance.
 */

import { useCallback, useId, useMemo, useState } from 'react';
import type { CSSProperties } from 'react';

import { ActionButton, Reveal, SectionHeading, StatusDot, TelemetryReadout } from '../components';
import { useRoute } from '../../routes/RouteContext';

const TITLE_ID = 'workspace-preview-title';

/* --- Illustrative fixtures ------------------------------------------------
   Hand-authored, internally consistent, and not produced by the engine. They exist so the
   composition has something coherent to react to; every surface that renders them carries the
   preview chip and the disconnection notice. */

interface PreviewParameter {
  readonly label: string;
  readonly value: string;
  readonly unit?: string;
  /** Present only where the figure is transcribed from a repository document. */
  readonly source?: string;
}

interface PreviewCandidate {
  readonly id: string;
  readonly deltaVMs: number;
  readonly transferHours: number;
  readonly reservePct: number;
  readonly contactPct: number;
  readonly recoveryIndex: number;
  readonly corridorClear: boolean;
}

interface PreviewPhase {
  readonly label: string;
  readonly share: number;
}

interface PreviewScenario {
  readonly id: string;
  readonly label: string;
  readonly summary: string;
  readonly departureShell: string;
  readonly targetShell: string;
  readonly parameters: readonly PreviewParameter[];
  readonly candidates: readonly PreviewCandidate[];
  readonly phases: readonly PreviewPhase[];
}

const SCENARIOS: readonly PreviewScenario[] = [
  {
    id: 'leo-depressurisation',
    label: 'LEO depressurisation · 500 km to 800 km',
    summary:
      'A pressure-loss event on a station in the 800 km shell, with rescue vehicles parked ' +
      'lower and faster.',
    departureShell: '500 KM',
    targetShell: '800 KM',
    parameters: [
      { label: 'Departure shell', value: '500', unit: 'km' },
      { label: 'Target shell', value: '800', unit: 'km' },
      { label: 'Synodic period', value: '25.4', unit: 'h', source: 'README' },
      { label: 'Survival window', value: '38', unit: 'min' },
      { label: 'Vehicles available', value: '4' },
      { label: 'Emergencies queued', value: '2' },
      { label: 'Tracked debris', value: '1,240' },
    ],
    candidates: [
      { id: 'PLN-01', deltaVMs: 168, transferHours: 11.4, reservePct: 58.2, contactPct: 62, recoveryIndex: 0.41, corridorClear: true },
      { id: 'PLN-02', deltaVMs: 194, transferHours: 9.1, reservePct: 54.6, contactPct: 47, recoveryIndex: 0.52, corridorClear: true },
      { id: 'PLN-03', deltaVMs: 213, transferHours: 8.4, reservePct: 51.8, contactPct: 31, recoveryIndex: 0.57, corridorClear: false },
      { id: 'PLN-04', deltaVMs: 246, transferHours: 6.7, reservePct: 47.3, contactPct: 55, recoveryIndex: 0.68, corridorClear: true },
      { id: 'PLN-05', deltaVMs: 281, transferHours: 5.2, reservePct: 42.9, contactPct: 38, recoveryIndex: 0.77, corridorClear: true },
      { id: 'PLN-06', deltaVMs: 334, transferHours: 4.1, reservePct: 33.6, contactPct: 44, recoveryIndex: 0.84, corridorClear: true },
      { id: 'PLN-07', deltaVMs: 391, transferHours: 3.3, reservePct: 26.4, contactPct: 29, recoveryIndex: 0.89, corridorClear: false },
    ],
    phases: [
      { label: 'Phasing', share: 18 },
      { label: 'Burn', share: 6 },
      { label: 'Coast', share: 42 },
      { label: 'Trim', share: 8 },
      { label: 'Rendezvous', share: 16 },
      { label: 'Safing', share: 10 },
    ],
  },
  {
    id: 'polar-power-loss',
    label: 'Polar power loss · 620 km to 1,140 km',
    summary:
      'A high-inclination target that forces a plane change, which is where the delta-v lower ' +
      'bound stops being a Hohmann estimate.',
    departureShell: '620 KM',
    targetShell: '1,140 KM',
    parameters: [
      { label: 'Departure shell', value: '620', unit: 'km' },
      { label: 'Target shell', value: '1,140', unit: 'km' },
      { label: 'Plane change', value: '8.4', unit: 'deg' },
      { label: 'Survival window', value: '51', unit: 'min' },
      { label: 'Vehicles available', value: '3' },
      { label: 'Emergencies queued', value: '1' },
      { label: 'Tracked debris', value: '2,105' },
    ],
    candidates: [
      { id: 'PLN-11', deltaVMs: 402, transferHours: 14.8, reservePct: 49.7, contactPct: 58, recoveryIndex: 0.36, corridorClear: true },
      { id: 'PLN-12', deltaVMs: 447, transferHours: 11.6, reservePct: 45.1, contactPct: 42, recoveryIndex: 0.48, corridorClear: true },
      { id: 'PLN-13', deltaVMs: 496, transferHours: 9.3, reservePct: 39.8, contactPct: 27, recoveryIndex: 0.59, corridorClear: true },
      { id: 'PLN-14', deltaVMs: 538, transferHours: 7.9, reservePct: 34.2, contactPct: 51, recoveryIndex: 0.66, corridorClear: true },
      { id: 'PLN-15', deltaVMs: 604, transferHours: 6.4, reservePct: 28.9, contactPct: 36, recoveryIndex: 0.74, corridorClear: false },
      { id: 'PLN-16', deltaVMs: 671, transferHours: 5.1, reservePct: 22.3, contactPct: 44, recoveryIndex: 0.81, corridorClear: true },
    ],
    phases: [
      { label: 'Phasing', share: 24 },
      { label: 'Burn', share: 7 },
      { label: 'Coast', share: 38 },
      { label: 'Plane change', share: 11 },
      { label: 'Rendezvous', share: 13 },
      { label: 'Safing', share: 7 },
    ],
  },
  {
    id: 'debris-conjunction',
    label: 'Debris conjunction · 480 km to 705 km',
    summary:
      'Three emergencies against six vehicles, which is the case the assignment stage exists ' +
      'for rather than a single best transfer.',
    departureShell: '480 KM',
    targetShell: '705 KM',
    parameters: [
      { label: 'Departure shell', value: '480', unit: 'km' },
      { label: 'Target shell', value: '705', unit: 'km' },
      { label: 'Conjunction lead', value: '22', unit: 'min' },
      { label: 'Survival window', value: '26', unit: 'min' },
      { label: 'Vehicles available', value: '6' },
      { label: 'Emergencies queued', value: '3' },
      { label: 'Tracked debris', value: '3,480' },
    ],
    candidates: [
      { id: 'PLN-21', deltaVMs: 122, transferHours: 9.8, reservePct: 64.1, contactPct: 53, recoveryIndex: 0.44, corridorClear: false },
      { id: 'PLN-22', deltaVMs: 147, transferHours: 8.2, reservePct: 60.5, contactPct: 39, recoveryIndex: 0.53, corridorClear: true },
      { id: 'PLN-23', deltaVMs: 179, transferHours: 6.6, reservePct: 56.2, contactPct: 61, recoveryIndex: 0.63, corridorClear: true },
      { id: 'PLN-24', deltaVMs: 208, transferHours: 5.4, reservePct: 51.7, contactPct: 34, recoveryIndex: 0.71, corridorClear: true },
      { id: 'PLN-25', deltaVMs: 259, transferHours: 4.2, reservePct: 44.8, contactPct: 48, recoveryIndex: 0.8, corridorClear: true },
      { id: 'PLN-26', deltaVMs: 318, transferHours: 3.1, reservePct: 36.3, contactPct: 22, recoveryIndex: 0.87, corridorClear: false },
    ],
    phases: [
      { label: 'Phasing', share: 12 },
      { label: 'Burn', share: 5 },
      { label: 'Coast', share: 46 },
      { label: 'Avoidance', share: 9 },
      { label: 'Rendezvous', share: 18 },
      { label: 'Safing', share: 10 },
    ],
  },
];

interface PreviewConstraint {
  readonly id: string;
  readonly label: string;
  readonly detail: string;
  readonly test: (candidate: PreviewCandidate) => boolean;
}

const PREVIEW_CONSTRAINTS: readonly PreviewConstraint[] = [
  {
    id: 'corridor',
    label: 'Debris corridor clearance',
    detail: 'Rejects any plan whose path crosses a tracked conjunction corridor.',
    test: (candidate) => candidate.corridorClear,
  },
  {
    id: 'reserve',
    label: 'Propellant reserve at least 35%',
    detail: 'The outbound leg is not the whole mission — the vehicle still has to come back.',
    test: (candidate) => candidate.reservePct >= 35,
  },
  {
    id: 'contact',
    label: 'Ground contact at least 40% of burn',
    detail: 'Intervention is only possible while the vehicle is in contact.',
    test: (candidate) => candidate.contactPct >= 40,
  },
];

/** Two of the three start engaged, so the strip arrives already showing that a constraint set
 * removes candidates rather than greying them. */
const INITIAL_CONSTRAINTS: readonly string[] = ['corridor', 'reserve'];

/* --- Orbital schematic geometry -------------------------------------------
   Earth sits below the frame so only the limb and the two shells above it are in view, which
   is the framing the workspace camera settles into. Shell separation is exaggerated for
   legibility for the same reason the workspace exaggerates altitude: at true scale a 500 km
   and an 800 km orbit differ by about 4% of Earth's radius and collapse into one line
   (src/visualization/coordinates.ts). */

const EARTH_X = 150;
const EARTH_Y = 470;
const EARTH_R = 232;
const DEPARTURE_R = 292;
const TARGET_R = 356;
const VEHICLE_ANGLE_DEG = 76;
const EMERGENCY_ANGLE_DEG = 38;

interface ScenePoint {
  readonly x: number;
  readonly y: number;
}

function polar(radius: number, degrees: number): ScenePoint {
  const radians = (degrees * Math.PI) / 180;
  return { x: EARTH_X + radius * Math.cos(radians), y: EARTH_Y - radius * Math.sin(radians) };
}

/**
 * A slower transfer bows further from Earth — closer to the tangential half-ellipse — while a
 * fast one cuts a straighter chord and pays for it in delta-v. The mapping is schematic; it is
 * not a propagated trajectory and the panel says so.
 */
function transferControl(from: ScenePoint, to: ScenePoint, hours: number): ScenePoint {
  const midX = (from.x + to.x) / 2;
  const midY = (from.y + to.y) / 2;
  const dx = midX - EARTH_X;
  const dy = midY - EARTH_Y;
  const length = Math.hypot(dx, dy) || 1;
  const fraction = Math.min(1, Math.max(0, (hours - 3) / 12));
  const bow = 8 + fraction * 88;
  return { x: midX + (dx / length) * bow, y: midY + (dy / length) * bow };
}

/**
 * Where a shell crosses a chosen vertical, so its label can sit against the left edge of the
 * frame instead of at the arc's apex. The apex is directly above Earth and directly beside the
 * parked vehicle, and two labels were landing on each other there.
 */
function shellLabelY(radius: number, atX: number): number {
  const dx = atX - EARTH_X;
  const chord = Math.max(0, radius * radius - dx * dx);
  return EARTH_Y - Math.sqrt(chord);
}

const SHELL_LABEL_X = 44;

function quadraticAt(from: ScenePoint, control: ScenePoint, to: ScenePoint, t: number): ScenePoint {
  const inv = 1 - t;
  return {
    x: inv * inv * from.x + 2 * inv * t * control.x + t * t * to.x,
    y: inv * inv * from.y + 2 * inv * t * control.y + t * t * to.y,
  };
}

/** Debris authored in Earth-centred polar coordinates so the field genuinely occupies the
 * orbital band rather than being scattered over the frame. */
const DEBRIS_POLAR: readonly (readonly [number, number])[] = [
  [22, 268],
  [29, 346],
  [36, 296],
  [44, 368],
  [51, 262],
  [58, 334],
  [66, 370],
  [72, 284],
  [79, 348],
  [86, 306],
  [93, 360],
  [100, 278],
  [107, 332],
  [114, 290],
];

function formatFixed(value: number, decimals: number): string {
  return value.toFixed(decimals);
}

export function MissionPreviewSection() {
  const uid = useId().replace(/:/g, '');
  const { navigate } = useRoute();

  const [scenarioId, setScenarioId] = useState<string>(SCENARIOS[0]?.id ?? '');
  const [constraintIds, setConstraintIds] = useState<readonly string[]>(INITIAL_CONSTRAINTS);
  const [selectedId, setSelectedId] = useState<string | null>(null);
  // A monotonic revision counter, the way a plan board numbers each reissue. It is the thing
  // that makes "the filter ran again and produced the same set" distinguishable from "nothing
  // happened", which a bare count cannot express.
  const [revision, setRevision] = useState<number>(1);

  const scenario = useMemo(
    () => SCENARIOS.find((entry) => entry.id === scenarioId) ?? SCENARIOS[0] ?? null,
    [scenarioId],
  );

  const retained = useMemo(() => {
    if (!scenario) return [] as readonly PreviewCandidate[];
    return scenario.candidates.filter((candidate) =>
      PREVIEW_CONSTRAINTS.every(
        (constraint) => !constraintIds.includes(constraint.id) || constraint.test(candidate),
      ),
    );
  }, [scenario, constraintIds]);

  // Derived rather than synchronised in an effect: when a constraint removes the selected
  // plan, the selection must already be valid in the same render that drops it, or the panel
  // would paint one frame describing a plan the strip no longer contains.
  const active = useMemo(
    () => retained.find((candidate) => candidate.id === selectedId) ?? retained[0] ?? null,
    [retained, selectedId],
  );

  const handleScenarioChange = useCallback((next: string) => {
    setScenarioId(next);
    setSelectedId(null);
    setRevision((current) => current + 1);
  }, []);

  const toggleConstraint = useCallback((id: string) => {
    setConstraintIds((current) =>
      current.includes(id) ? current.filter((entry) => entry !== id) : [...current, id],
    );
    setRevision((current) => current + 1);
  }, []);

  const vehicle = polar(DEPARTURE_R, VEHICLE_ANGLE_DEG);
  const emergency = polar(TARGET_R, EMERGENCY_ANGLE_DEG);
  const control = transferControl(vehicle, emergency, active?.transferHours ?? 8);
  const midCourse = quadraticAt(vehicle, control, emergency, 0.5);
  const transferPath =
    `M ${vehicle.x.toFixed(1)} ${vehicle.y.toFixed(1)} ` +
    `Q ${control.x.toFixed(1)} ${control.y.toFixed(1)} ` +
    `${emergency.x.toFixed(1)} ${emergency.y.toFixed(1)}`;

  const totalCandidates = scenario?.candidates.length ?? 0;
  const revisionLabel =
    `REV ${String(revision).padStart(3, '0')} · ${retained.length} OF ${totalCandidates} ` +
    `CANDIDATES RETAINED · ${constraintIds.length} OF ${PREVIEW_CONSTRAINTS.length} ` +
    'CONSTRAINTS ACTIVE';

  return (
    <section id="workspace-preview" className="mk-section" aria-labelledby={TITLE_ID}>
      <div className="mk-shell">
        <Reveal>
          <SectionHeading
            index="04"
            label="Workspace"
            title="The operator surface, laid out"
            lede={
              'Parameters on the left, the spatial view in the centre, objectives on the right, ' +
              'candidates along the bottom. The layout below is the real information ' +
              'architecture of the workspace; the values in it are not.'
            }
            id={TITLE_ID}
          />
        </Reveal>

        <Reveal className="mk-preview" delayStep={1}>
          <div className="mk-preview__bar">
            <p className="mk-preview__chip mk-mono">
              <StatusDot tone="neutral" />
              Interface preview · illustrative scenario
            </p>
            <div className="mk-preview__scenario">
              <label className="mk-preview__field-label" htmlFor={`${uid}-scenario`}>
                Scenario
              </label>
              <select
                className="mk-preview__select"
                id={`${uid}-scenario`}
                value={scenario?.id ?? ''}
                onChange={(event) => handleScenarioChange(event.target.value)}
              >
                {SCENARIOS.map((entry) => (
                  <option key={entry.id} value={entry.id}>
                    {entry.label}
                  </option>
                ))}
              </select>
            </div>
          </div>

          <div className="mk-preview__console">
            {/* --- Left rail: scenario parameters --- */}
            <div className="mk-preview__rail">
              <h3 className="mk-preview__panel-title">Parameters</h3>
              <p className="mk-preview__summary">{scenario?.summary ?? ''}</p>
              <dl className="mk-preview__params">
                {(scenario?.parameters ?? []).map((parameter) => (
                  <div className="mk-preview__param" key={parameter.label}>
                    <dt className="mk-preview__param-label">{parameter.label}</dt>
                    <dd className="mk-preview__param-value mk-mono">
                      {parameter.value}
                      {parameter.unit ? (
                        <span className="mk-preview__param-unit">{parameter.unit}</span>
                      ) : null}
                      {parameter.source ? (
                        <span className="mk-preview__param-source">{parameter.source}</span>
                      ) : null}
                    </dd>
                  </div>
                ))}
              </dl>

              <fieldset className="mk-preview__constraints">
                <legend className="mk-preview__panel-title">Safety constraints</legend>
                {PREVIEW_CONSTRAINTS.map((constraint) => {
                  const inputId = `${uid}-constraint-${constraint.id}`;
                  return (
                    <div className="mk-preview__constraint" key={constraint.id}>
                      <input
                        className="mk-preview__checkbox"
                        type="checkbox"
                        id={inputId}
                        checked={constraintIds.includes(constraint.id)}
                        onChange={() => toggleConstraint(constraint.id)}
                      />
                      <label className="mk-preview__constraint-label" htmlFor={inputId}>
                        <span className="mk-preview__constraint-name">{constraint.label}</span>
                        <span className="mk-preview__constraint-detail">{constraint.detail}</span>
                      </label>
                    </div>
                  );
                })}
              </fieldset>
            </div>

            {/* --- Centre: orbital schematic --- */}
            <div className="mk-preview__stage">
              <div className="mk-preview__stage-head">
                <h3 className="mk-preview__panel-title">Orbital view</h3>
                <p className="mk-preview__stage-note mk-mono">Schematic geometry · not propagated</p>
              </div>

              <svg
                className="mk-preview__scene"
                viewBox="0 0 520 400"
                role="img"
                aria-label={
                  `Schematic orbital view. A rescue vehicle in the ${
                    scenario?.departureShell ?? ''
                  } shell transfers to an emergency in the ${scenario?.targetShell ?? ''} shell, ` +
                  `along the path for ${active?.id ?? 'no selected plan'}.`
                }
              >
                <defs>
                  <linearGradient
                    id={`${uid}-transfer`}
                    gradientUnits="userSpaceOnUse"
                    x1={vehicle.x}
                    y1={vehicle.y}
                    x2={emergency.x}
                    y2={emergency.y}
                  >
                    <stop className="mk-preview__stop mk-preview__stop--1" offset="0" />
                    <stop className="mk-preview__stop mk-preview__stop--2" offset="0.34" />
                    <stop className="mk-preview__stop mk-preview__stop--3" offset="0.62" />
                    <stop className="mk-preview__stop mk-preview__stop--4" offset="0.84" />
                    <stop className="mk-preview__stop mk-preview__stop--5" offset="1" />
                  </linearGradient>
                </defs>

                <circle className="mk-preview__earth" cx={EARTH_X} cy={EARTH_Y} r={EARTH_R} />
                <circle className="mk-preview__limb" cx={EARTH_X} cy={EARTH_Y} r={EARTH_R} />

                <circle className="mk-preview__shell" cx={EARTH_X} cy={EARTH_Y} r={DEPARTURE_R} />
                <circle className="mk-preview__shell" cx={EARTH_X} cy={EARTH_Y} r={TARGET_R} />

                <g className="mk-preview__debris">
                  {DEBRIS_POLAR.map(([degrees, radius]) => {
                    const point = polar(radius, degrees);
                    return (
                      <circle
                        key={`${degrees}-${radius}`}
                        cx={point.x.toFixed(1)}
                        cy={point.y.toFixed(1)}
                        r={1.7}
                      />
                    );
                  })}
                </g>

                <g className="mk-preview__shell-label">
                  <text x={SHELL_LABEL_X} y={shellLabelY(TARGET_R, SHELL_LABEL_X) - 7}>
                    TARGET SHELL {scenario?.targetShell ?? ''}
                  </text>
                  <text x={SHELL_LABEL_X} y={shellLabelY(DEPARTURE_R, SHELL_LABEL_X) - 7}>
                    DEPARTURE SHELL {scenario?.departureShell ?? ''}
                  </text>
                </g>

                {/* Keyed on the plan so a new selection re-runs the trace: the stroke drawing
                    itself in is the panel's only animation, and it stands for the search that
                    produced the path. */}
                <path
                  key={`${scenario?.id ?? 'none'}-${active?.id ?? 'none'}`}
                  className="mk-preview__transfer"
                  d={transferPath}
                  pathLength={100}
                  stroke={`url(#${uid}-transfer)`}
                />

                <g className="mk-preview__waypoint">
                  <circle
                    className="mk-preview__waypoint-vehicle"
                    cx={vehicle.x}
                    cy={vehicle.y}
                    r={5.5}
                  />
                  <circle
                    className="mk-preview__waypoint-mid"
                    cx={midCourse.x}
                    cy={midCourse.y}
                    r={3.4}
                  />
                  <circle
                    className="mk-preview__waypoint-target"
                    cx={emergency.x}
                    cy={emergency.y}
                    r={6.5}
                  />
                  <circle
                    className="mk-preview__waypoint-target-ring"
                    cx={emergency.x}
                    cy={emergency.y}
                    r={12}
                  />
                </g>

                <g className="mk-preview__annotation">
                  <text x={vehicle.x} y={vehicle.y - 20} textAnchor="middle">
                    RESCUE VEHICLE
                  </text>
                  <text x={midCourse.x} y={midCourse.y - 14} textAnchor="middle">
                    MID-COURSE
                  </text>
                  {/* Below the marker, clear of its ping ring and of the departure shell,
                      which passes just under the emergency at this geometry. */}
                  <text x={emergency.x} y={emergency.y + 32} textAnchor="middle">
                    EMERGENCY
                  </text>
                  <text
                    className="mk-preview__annotation-value mk-mono"
                    x={midCourse.x}
                    y={midCourse.y + 22}
                    textAnchor="middle"
                  >
                    {active
                      ? `${active.deltaVMs} M/S · ${formatFixed(active.transferHours, 1)} H`
                      : 'NO PLAN RETAINED'}
                  </text>
                </g>
              </svg>

              <div className="mk-preview__timeline">
                <p className="mk-preview__timeline-title mk-mono">Conceptual mission timeline</p>
                <ol className="mk-preview__phases">
                  {(scenario?.phases ?? []).map((phase, index) => (
                    // `flexGrow` carries the share in the row layout; `--mk-phase-share` carries
                    // the same number as a bar width once the timeline stacks, where a grow
                    // factor no longer expresses anything.
                    <li
                      className="mk-preview__phase"
                      key={phase.label}
                      style={
                        {
                          flexGrow: phase.share,
                          '--mk-phase-share': `${phase.share}%`,
                        } as CSSProperties
                      }
                    >
                      <span className="mk-preview__phase-index mk-mono">
                        {String(index + 1).padStart(2, '0')}
                      </span>
                      <span className="mk-preview__phase-label">{phase.label}</span>
                    </li>
                  ))}
                </ol>
              </div>
            </div>

            {/* --- Right column: objectives and link status --- */}
            <div className="mk-preview__column">
              <h3 className="mk-preview__panel-title">Selected plan</h3>
              <p className="mk-preview__plan-id mk-mono">{active?.id ?? 'NONE RETAINED'}</p>
              <div className="mk-preview__objectives">
                <TelemetryReadout
                  label="Delta-v"
                  value={active ? String(active.deltaVMs) : '—'}
                  unit="m/s"
                />
                <TelemetryReadout
                  label="Transfer time"
                  value={active ? formatFixed(active.transferHours, 1) : '—'}
                  unit="h"
                />
                <TelemetryReadout
                  label="Propellant reserve"
                  value={active ? formatFixed(active.reservePct, 1) : '—'}
                  unit="%"
                />
                <TelemetryReadout
                  label="Contact window"
                  value={active ? String(active.contactPct) : '—'}
                  unit="%"
                />
                <TelemetryReadout
                  label="Recovery index"
                  value={active ? formatFixed(active.recoveryIndex, 2) : '—'}
                  tone={active && active.recoveryIndex >= 0.7 ? 'active' : 'neutral'}
                />
                <TelemetryReadout
                  label="Corridor"
                  value={active ? (active.corridorClear ? 'CLEAR' : 'CROSSED') : '—'}
                  tone={active && !active.corridorClear ? 'warm' : 'neutral'}
                />
              </div>

              <div className="mk-preview__link">
                <p className="mk-preview__link-row mk-mono">
                  <StatusDot tone="neutral" />
                  Engine link — not connected
                </p>
                <p className="mk-preview__link-row mk-mono">
                  <StatusDot tone="neutral" />
                  Bridge — not connected
                </p>
                <p className="mk-preview__link-note">
                  Both channels are dormant on this page by design. They are established from the
                  workspace, where the C++ engine owns the state this panel only draws.
                </p>
              </div>
            </div>
          </div>

          {/* --- Candidate strip --- */}
          <div className="mk-preview__strip">
            <div className="mk-preview__strip-head">
              <h3 className="mk-preview__panel-title">Candidate plans</h3>
              <p className="mk-preview__revision mk-mono" aria-live="polite">
                {revisionLabel}
              </p>
            </div>

            <div className="mk-preview__strip-scroll">
              {retained.length === 0 ? (
                <p className="mk-preview__strip-empty">
                  No candidate satisfies every active constraint. Release one to see the set
                  return.
                </p>
              ) : (
                <ul className="mk-preview__candidates">
                  {retained.map((candidate) => {
                    const isActive = active?.id === candidate.id;
                    return (
                      <li key={candidate.id}>
                        <button
                          type="button"
                          className={`mk-preview__candidate${isActive ? ' is-active' : ''}`}
                          aria-current={isActive ? 'true' : undefined}
                          onClick={() => setSelectedId(candidate.id)}
                        >
                          <span className="mk-preview__candidate-id mk-mono">{candidate.id}</span>
                          <span className="mk-preview__candidate-metric mk-mono">
                            {candidate.deltaVMs} m/s
                          </span>
                          <span className="mk-preview__candidate-metric mk-mono">
                            {formatFixed(candidate.transferHours, 1)} h
                          </span>
                          <span className="mk-preview__candidate-metric mk-mono">
                            {formatFixed(candidate.reservePct, 1)}% reserve
                          </span>
                        </button>
                      </li>
                    );
                  })}
                </ul>
              )}
            </div>
          </div>

          <div className="mk-preview__foot">
            <p className="mk-preview__disclosure">
              This panel is not connected to the engine. It shows no live telemetry and issues no
              commands, and every value in it is an illustrative fixture. The real workspace runs
              the C++20 engine over the Node bridge and is one click away.
            </p>
            <ActionButton variant="primary" onClick={() => navigate('workspace')}>
              Open the live workspace
            </ActionButton>
          </div>
        </Reveal>
      </div>
    </section>
  );
}
