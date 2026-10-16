/**
 * Illustrative trade-space fixtures for the marketing surface.
 *
 * THESE NUMBERS ARE NOT ENGINE OUTPUT. They are hand-authored values chosen to be physically
 * coherent — lower delta-v buys longer transfers, faster arrivals raise closing-velocity risk,
 * propellant reserve falls as the burn grows — so that the landing page can demonstrate what a
 * Pareto frontier *is* without a backend. The real frontier comes from the C++ engine's
 * label-setting search and reaches the workspace over `/snapshot`; nothing here talks to it.
 *
 * Any surface rendering this data must say so in visible text. `DEMO_DISCLOSURE` exists so
 * that label cannot drift away from the fixture it describes.
 *
 * What is NOT illustrative is `paretoFrontier()`. It is an exact nondominance computation over
 * the two plotted, minimised axes — the same test the engine applies, on a smaller input. The
 * frontier the page draws is therefore genuinely derived from the points it shows, not a
 * hand-picked subset that merely looks like one.
 */

export interface DemoCandidate {
  readonly id: string;
  readonly deltaVMs: number;
  readonly transferHours: number;
  readonly riskIndex: number;
  readonly recoveryIndex: number;
  readonly fuelReservePct: number;
}

export type DemoPriority = 'balanced' | 'fuel' | 'time' | 'risk';

export const DEMO_DISCLOSURE =
  'Illustrative fixture data, not engine output. The dominance test and the selected plan are ' +
  'computed from these points in the browser; the engine is reached only from the workspace.';

/**
 * Fourteen candidate transfers. Eight are nondominated and six sit in the interior — the
 * interior points matter, because a frontier drawn without anything behind it does not
 * demonstrate that dominance is doing any work.
 *
 * `riskIndex` is deliberately not monotonic in transfer time: a long, slow transfer accumulates
 * debris exposure while a very fast one arrives with high closing velocity and a narrow abort
 * envelope. The minimum sits in the middle, which is what makes a third objective worth
 * carrying separately rather than folding into the other two.
 */
export const DEMO_CANDIDATES: readonly DemoCandidate[] = [
  { id: 'TRJ-01', deltaVMs: 138, transferHours: 13.9, riskIndex: 0.71, recoveryIndex: 0.34, fuelReservePct: 62.4 },
  { id: 'TRJ-02', deltaVMs: 166, transferHours: 12.7, riskIndex: 0.66, recoveryIndex: 0.39, fuelReservePct: 57.1 },
  { id: 'TRJ-03', deltaVMs: 152, transferHours: 11.2, riskIndex: 0.58, recoveryIndex: 0.46, fuelReservePct: 59.8 },
  { id: 'TRJ-04', deltaVMs: 205, transferHours: 10.5, riskIndex: 0.54, recoveryIndex: 0.49, fuelReservePct: 51.2 },
  { id: 'TRJ-05', deltaVMs: 171, transferHours: 9.4, riskIndex: 0.47, recoveryIndex: 0.55, fuelReservePct: 56.3 },
  { id: 'TRJ-06', deltaVMs: 196, transferHours: 7.6, riskIndex: 0.39, recoveryIndex: 0.63, fuelReservePct: 52.7 },
  { id: 'TRJ-07', deltaVMs: 243, transferHours: 8.2, riskIndex: 0.44, recoveryIndex: 0.58, fuelReservePct: 46.0 },
  { id: 'TRJ-08', deltaVMs: 224, transferHours: 6.1, riskIndex: 0.33, recoveryIndex: 0.71, fuelReservePct: 48.9 },
  { id: 'TRJ-09', deltaVMs: 296, transferHours: 6.9, riskIndex: 0.41, recoveryIndex: 0.66, fuelReservePct: 40.3 },
  { id: 'TRJ-10', deltaVMs: 268, transferHours: 4.8, riskIndex: 0.36, recoveryIndex: 0.79, fuelReservePct: 43.6 },
  { id: 'TRJ-11', deltaVMs: 352, transferHours: 5.2, riskIndex: 0.48, recoveryIndex: 0.75, fuelReservePct: 34.1 },
  { id: 'TRJ-12', deltaVMs: 317, transferHours: 3.6, riskIndex: 0.52, recoveryIndex: 0.86, fuelReservePct: 37.5 },
  { id: 'TRJ-13', deltaVMs: 384, transferHours: 2.9, riskIndex: 0.68, recoveryIndex: 0.91, fuelReservePct: 30.2 },
  { id: 'TRJ-14', deltaVMs: 412, transferHours: 3.4, riskIndex: 0.74, recoveryIndex: 0.88, fuelReservePct: 27.4 },
];

/**
 * `a` dominates `b` when it is no worse on both minimised axes and strictly better on at least
 * one. Two identical points do not dominate each other, so both survive — dropping one would
 * be a presentation choice smuggled into a mathematical definition.
 */
function dominates(a: DemoCandidate, b: DemoCandidate): boolean {
  const noWorse = a.deltaVMs <= b.deltaVMs && a.transferHours <= b.transferHours;
  const strictlyBetter = a.deltaVMs < b.deltaVMs || a.transferHours < b.transferHours;
  return noWorse && strictlyBetter;
}

/**
 * Exact nondominance over (deltaVMs, transferHours), both minimised. O(n²), which is the right
 * complexity for fourteen points and keeps the definition readable — this function is on the
 * page precisely so a reader can check that the drawn frontier is the computed one.
 *
 * Returned sorted by ascending delta-v so a consumer can connect the points into a monotone
 * frontier without re-sorting. Sorting is presentation only; membership is decided before it.
 */
export function paretoFrontier(cs: readonly DemoCandidate[]): readonly DemoCandidate[] {
  const nondominated = cs.filter((candidate) => !cs.some((other) => dominates(other, candidate)));
  return [...nondominated].sort(
    (a, b) => a.deltaVMs - b.deltaVMs || a.transferHours - b.transferHours,
  );
}

interface PriorityWeights {
  readonly deltaV: number;
  readonly time: number;
  readonly risk: number;
  readonly recovery: number;
}

/**
 * The scalarisation is stated openly rather than tuned until it looks good, because the whole
 * argument of the section around it is that collapsing a frontier to one point is a *choice*.
 * Weights sum to 1 in every row; 'balanced' is the only row that does not privilege an axis.
 *
 * Note what this is not: it is not how the engine selects a plan, and it never influences which
 * points are on the frontier. Selection happens strictly after the dominance test.
 */
const PRIORITY_WEIGHTS: Readonly<Record<DemoPriority, PriorityWeights>> = {
  balanced: { deltaV: 0.3, time: 0.3, risk: 0.2, recovery: 0.2 },
  fuel: { deltaV: 0.62, time: 0.14, risk: 0.12, recovery: 0.12 },
  time: { deltaV: 0.14, time: 0.62, risk: 0.1, recovery: 0.14 },
  risk: { deltaV: 0.14, time: 0.14, risk: 0.58, recovery: 0.14 },
};

export interface DemoPriorityOption {
  readonly value: DemoPriority;
  readonly label: string;
  readonly description: string;
}

export const DEMO_PRIORITIES: readonly DemoPriorityOption[] = [
  {
    value: 'balanced',
    label: 'Balanced',
    description: 'No axis privileged. The compromise a planner defaults to when nothing forces one.',
  },
  {
    value: 'fuel',
    label: 'Propellant',
    description: 'Reserve is the binding constraint — the vehicle still has to come back.',
  },
  {
    value: 'time',
    label: 'Time',
    description: 'The survival window dominates. Delta-v is spent to arrive inside it.',
  },
  {
    value: 'risk',
    label: 'Risk',
    description: 'Debris exposure and approach geometry outweigh both cost terms.',
  },
];

interface AxisRange {
  readonly min: number;
  readonly max: number;
}

function axisRange(cs: readonly DemoCandidate[], read: (c: DemoCandidate) => number): AxisRange {
  let min = Number.POSITIVE_INFINITY;
  let max = Number.NEGATIVE_INFINITY;
  for (const candidate of cs) {
    const value = read(candidate);
    if (value < min) min = value;
    if (value > max) max = value;
  }
  return { min, max };
}

/** Min-max to [0, 1] so axes in different units (m/s, hours, unitless indices) are comparable.
 * A zero-width range collapses to 0 rather than dividing by it — every candidate is then equal
 * on that axis, which is the correct reading. */
function normalise(value: number, range: AxisRange): number {
  const span = range.max - range.min;
  return span <= 0 ? 0 : (value - range.min) / span;
}

/**
 * Lowest scalarised cost wins; ties keep the first candidate in input order, so the result is
 * deterministic. Returns null for an empty set rather than a sentinel candidate — there is no
 * honest "preferred plan" when there are no plans.
 */
export function preferredCandidate(
  cs: readonly DemoCandidate[],
  priority: DemoPriority,
): DemoCandidate | null {
  if (cs.length === 0) return null;

  const weights = PRIORITY_WEIGHTS[priority];
  const deltaVRange = axisRange(cs, (c) => c.deltaVMs);
  const timeRange = axisRange(cs, (c) => c.transferHours);
  const riskRange = axisRange(cs, (c) => c.riskIndex);
  const recoveryRange = axisRange(cs, (c) => c.recoveryIndex);

  let best: DemoCandidate | null = null;
  let bestScore = Number.POSITIVE_INFINITY;

  for (const candidate of cs) {
    // Three minimised terms plus one maximised term, inverted so that every contribution
    // points the same way: smaller total is better.
    const score =
      weights.deltaV * normalise(candidate.deltaVMs, deltaVRange) +
      weights.time * normalise(candidate.transferHours, timeRange) +
      weights.risk * normalise(candidate.riskIndex, riskRange) +
      weights.recovery * (1 - normalise(candidate.recoveryIndex, recoveryRange));

    if (score < bestScore) {
      bestScore = score;
      best = candidate;
    }
  }

  return best;
}
