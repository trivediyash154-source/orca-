/**
 * The entry surface's rescue scenario, expressed as real orbital ELEMENTS rather than as
 * hand-drawn shapes.
 *
 * Both orbits below are `OrbitView` records — the same shape the engine emits in a snapshot
 * (`types/engine.ts`) — and every position on them is evaluated with the formula
 * `Orbit::positionAtKm()` uses in `cpp/domain/Orbit.hpp`, the same expression
 * `visualization/orbit/orbitGeometry.ts::orbitRingPoints()` traces to draw a ring. The
 * landing page therefore renders geometrically real orbits generated from altitude,
 * inclination and RAAN; nothing here is drawn by eye.
 *
 * `orbitRingPoints()` evaluates that expression at N evenly spaced phases to trace a whole
 * ring, so it cannot answer "where is this vehicle right now". `positionOnOrbit()` below is
 * its single-phase counterpart and repeats the expression deliberately: `orbitGeometry.ts`
 * belongs to the workspace and is not this file's to refactor, so the duplication is stated
 * rather than hidden. Any change to one must be mirrored in the other.
 *
 * ## What is real and what is presentation
 *
 * - The elements, the position formula and the ECI -> scene axis mapping are real.
 * - `applyAltitudeExaggeration()` is the project's documented VISUALISATION transform
 *   (`visualization/coordinates.ts`) and is applied to radius only. At true scale a 520 km
 *   and a 786 km shell differ by 4% of Earth's radius and collapse into one line on screen.
 *   No number this transform touches is ever reported as a measurement.
 * - Angular rates come from the real two-body mean motion sqrt(mu / a^3), so the RATIO of
 *   the two spacecraft's rates is physical. Only the global time scale is compressed, by one
 *   documented constant, so a revolution takes seconds instead of an hour and a half. The
 *   workspace propagates real state from the engine; this file does not, and does not claim
 *   to.
 */

import { Vector3 } from 'three';

import { applyAltitudeExaggeration, SCENE_SCALE_KM } from '../../visualization/coordinates';
import type { OrbitView } from '../../types/engine';
import { SCENE_COLORS, spectralAt } from './spectral';

/** `kMuEarth` (`cpp/core/Constants.hpp`), km^3/s^2 — mirrored by hand exactly as
 * `components/ConstellationObjectPanel.tsx` already mirrors it. */
const MU_EARTH_KM3_S2 = 398600.4418;

const DEG_TO_RAD = Math.PI / 180;

/**
 * How hard the shells are pushed apart for the entry surface. The workspace defaults far
 * lower because an operator reads distances off it; nobody reads a distance off a hero
 * animation, and the story only works if "the vehicle is below the casualty" is legible in
 * one glance at 360 px wide.
 */
export const HERO_EXAGGERATION = 3.4;

/**
 * Seconds of scenario time per second of wall clock. The rescue orbit's true period is
 * 5,701 s (95.0 min); at this compression a revolution reads in ~47 s, slow enough to look
 * like orbital mechanics rather than a spinner.
 */
export const HERO_TIME_COMPRESSION = 120;

/**
 * The rescue vehicle: 520 km, 51.6 deg — the ISS inclination, which is where a real
 * servicing asset would be parked because so much traffic shares that plane.
 */
export const RESCUE_ORBIT: OrbitView = {
  altitudeKm: 520,
  inclinationDeg: 51.6,
  raanDeg: 14,
  phaseDeg: 208,
  band: 'LEO',
};

/**
 * The disabled satellite: 786 km, and — this is the point of the scenario — 63.4 deg at a
 * RAAN 58 deg away from the vehicle's. The two planes visibly do not coincide, so the
 * transfer has to buy a plane change as well as an altitude change. That is the conflict
 * README describes: the synodic period between a 500 km and an 800 km orbit is 25.4 hours,
 * so the fuel-optimal departure phase recurs about once a day while a survival window runs
 * tens of minutes.
 */
export const TARGET_ORBIT: OrbitView = {
  altitudeKm: 786,
  inclinationDeg: 63.4,
  raanDeg: 72,
  phaseDeg: 96,
  band: 'LEO',
};

/**
 * Context traffic. Real elements, no entity attached — these are scenery that establishes
 * the shell structure the transfer crosses, not objects the page claims to be tracking.
 */
export const BACKDROP_ORBITS: readonly OrbitView[] = [
  { altitudeKm: 340, inclinationDeg: 28.5, raanDeg: 300, phaseDeg: 0, band: 'LEO' },
  { altitudeKm: 640, inclinationDeg: 97.8, raanDeg: 208, phaseDeg: 0, band: 'LEO' },
  { altitudeKm: 1180, inclinationDeg: 82.1, raanDeg: 130, phaseDeg: 0, band: 'LEO' },
];

/** Backdrop set for the narrower authentication panel — one ring, so the plate stays quiet
 * behind a form. */
export const AUTH_BACKDROP_ORBITS: readonly OrbitView[] = BACKDROP_ORBITS.slice(1, 2);

/* --- Palette access ------------------------------------------------------- */

/**
 * `spectral.ts` publishes `SCENE_COLORS` under a string index signature, so with
 * `noUncheckedIndexedAccess` every lookup is `string | undefined`. Widening it once here
 * keeps that narrowing in a single place, and a key this layer asks for but the palette does
 * not define degrades to a point on the ramp instead of handing `undefined` to a material.
 */
const PALETTE: Readonly<Record<string, string>> = SCENE_COLORS;

export function sceneColor(name: string, fallbackT = 0.5): string {
  return PALETTE[name] ?? spectralAt(fallbackT).getStyle();
}

/* --- Orbital evaluation --------------------------------------------------- */

/** Orbit radius in scene units (Earth = 1), after the documented altitude exaggeration.
 * `SCENE_SCALE_KM` is Earth's own radius, so it serves as both the scale and the surface. */
function sceneRadius(orbit: OrbitView, exaggeration: number): number {
  const trueRadii = (SCENE_SCALE_KM + orbit.altitudeKm) / SCENE_SCALE_KM;
  return applyAltitudeExaggeration(trueRadii, exaggeration);
}

/**
 * Writes the scene-space position at in-plane argument `phaseRad` into `out`.
 *
 * This is `Orbit::positionAtKm()` with the radial term already in scene units, followed by
 * the one documented ECI -> scene axis mapping (`coordinates.ts`): sceneX = eciX / R,
 * sceneY = eciZ / R (the polar axis becomes "up"), sceneZ = eciY / R.
 *
 * Allocation-free on purpose — it is called a few hundred times per frame.
 */
export function writePositionOnOrbit(
  orbit: OrbitView,
  phaseRad: number,
  exaggeration: number,
  out: Vector3,
): Vector3 {
  const r = sceneRadius(orbit, exaggeration);
  const inc = orbit.inclinationDeg * DEG_TO_RAD;
  const raan = orbit.raanDeg * DEG_TO_RAD;
  const cosRaan = Math.cos(raan);
  const sinRaan = Math.sin(raan);
  const cosInc = Math.cos(inc);
  const sinInc = Math.sin(inc);
  const cosT = Math.cos(phaseRad);
  const sinT = Math.sin(phaseRad);
  const eciX = r * (cosRaan * cosT - sinRaan * sinT * cosInc);
  const eciY = r * (sinRaan * cosT + cosRaan * sinT * cosInc);
  const eciZ = r * (sinT * sinInc);
  return out.set(eciX, eciZ, eciY);
}

const scratchPosition = new Vector3();

/** Tuple form of {@link writePositionOnOrbit}, for setup code outside the frame loop. */
export function positionOnOrbit(
  orbit: OrbitView,
  phaseRad: number,
  exaggeration: number,
): [number, number, number] {
  const p = writePositionOnOrbit(orbit, phaseRad, exaggeration, scratchPosition);
  return [p.x, p.y, p.z];
}

/** Two-body mean motion, rad/s: n = sqrt(mu / a^3) with a in real kilometres. Unaffected by
 * the visual exaggeration — that transform touches radius for display only. Exported because
 * a section that wants to state the scenario's real period should derive it from here
 * (T = 2 pi / n; 5,701 s for the rescue orbit, 6,035 s for the casualty's) rather than
 * transcribing a number that could drift out of agreement with these elements. */
export function meanMotionRadPerSec(orbit: OrbitView): number {
  const a = SCENE_SCALE_KM + orbit.altitudeKm;
  return Math.sqrt(MU_EARTH_KM3_S2 / (a * a * a));
}

const RESCUE_RATE = meanMotionRadPerSec(RESCUE_ORBIT) * HERO_TIME_COMPRESSION;
const TARGET_RATE = meanMotionRadPerSec(TARGET_ORBIT) * HERO_TIME_COMPRESSION;

/* --- Shared per-frame state ----------------------------------------------- */

/**
 * The live scenario pose, owned by `MissionLayer` and read by every child in its own
 * `useFrame`. It exists so the transfer arc and the sensor beam terminate at where the
 * spacecraft actually are rather than at constants that drift out of agreement with them.
 *
 * Three fields are deliberately mutable: this object is written once per frame and read by
 * four components, and making it immutable would mean a new object — and a React render —
 * sixty times a second, which is exactly what the entry surface must not do. The `Vector3`
 * references are stable and `readonly`; their contents are mutated in place.
 */
export interface MissionFrame {
  readonly rescue: Vector3;
  readonly target: Vector3;
  /** In-plane argument of each craft, radians from its own ascending node. */
  rescueTheta: number;
  targetTheta: number;
  /** Scenario seconds since mount, before compression. Frozen under reduced motion. */
  elapsed: number;
}

export function createMissionFrame(exaggeration: number): MissionFrame {
  const frame: MissionFrame = {
    rescue: new Vector3(),
    target: new Vector3(),
    rescueTheta: RESCUE_ORBIT.phaseDeg * DEG_TO_RAD,
    targetTheta: TARGET_ORBIT.phaseDeg * DEG_TO_RAD,
    elapsed: 0,
  };
  writePositionOnOrbit(RESCUE_ORBIT, frame.rescueTheta, exaggeration, frame.rescue);
  writePositionOnOrbit(TARGET_ORBIT, frame.targetTheta, exaggeration, frame.target);
  return frame;
}

/**
 * Advances the pose. Phase is re-derived from total elapsed time rather than accumulated per
 * frame, so a dropped frame or a backgrounded tab cannot make the two craft drift out of
 * their real rate ratio.
 */
export function advanceMissionFrame(
  frame: MissionFrame,
  deltaSec: number,
  exaggeration: number,
): void {
  frame.elapsed += deltaSec;
  frame.rescueTheta = RESCUE_ORBIT.phaseDeg * DEG_TO_RAD + RESCUE_RATE * frame.elapsed;
  frame.targetTheta = TARGET_ORBIT.phaseDeg * DEG_TO_RAD + TARGET_RATE * frame.elapsed;
  writePositionOnOrbit(RESCUE_ORBIT, frame.rescueTheta, exaggeration, frame.rescue);
  writePositionOnOrbit(TARGET_ORBIT, frame.targetTheta, exaggeration, frame.target);
}

/** Velocity direction at the given phase, normalised — the analytic derivative of the
 * position expression, so a craft can be pointed along its own track. */
export function writeVelocityDirection(
  orbit: OrbitView,
  phaseRad: number,
  exaggeration: number,
  out: Vector3,
): Vector3 {
  const r = sceneRadius(orbit, exaggeration);
  const inc = orbit.inclinationDeg * DEG_TO_RAD;
  const raan = orbit.raanDeg * DEG_TO_RAD;
  const cosRaan = Math.cos(raan);
  const sinRaan = Math.sin(raan);
  const cosInc = Math.cos(inc);
  const sinInc = Math.sin(inc);
  const cosT = Math.cos(phaseRad);
  const sinT = Math.sin(phaseRad);
  // d/dtheta of the position expression above.
  const eciX = r * (-cosRaan * sinT - sinRaan * cosT * cosInc);
  const eciY = r * (-sinRaan * sinT + cosRaan * cosT * cosInc);
  const eciZ = r * (cosT * sinInc);
  return out.set(eciX, eciZ, eciY).normalize();
}

/* --- Transfer geometry ---------------------------------------------------- */

/**
 * One candidate transfer's shape. A "solve" picks one of these; re-solving picks the next,
 * which is what makes the arc change geometry on screen.
 *
 * These are PRESENTATION parameters for an illustrative arc. The engine's own transfer model
 * (`docs/TRANSFER_MODEL.md`) is not run in the browser, and the landing page makes no engine
 * call at all — so no delta-v, duration or risk figure is derived from anything here.
 */
export interface TransferShape {
  readonly id: string;
  /** Radial overshoot at mid-arc, in kilometres of apparent altitude — the "raise, coast,
   * circularise" silhouette a two-burn transfer has. */
  readonly bulgeKm: number;
  /** Smallest angular sweep the arc is allowed, in radians. A transfer that swept 5 degrees
   * would render as a radial ladder rather than as a trajectory. */
  readonly minSweepRad: number;
  /** How early the plane change is bought, in [0, 1]. 0.5 is "at the crossing"; lower spends
   * it near departure. */
  readonly planeChangeBias: number;
}

export const TRANSFER_SHAPES: readonly TransferShape[] = [
  { id: 'TRJ-A', bulgeKm: 210, minSweepRad: Math.PI * 0.62, planeChangeBias: 0.5 },
  { id: 'TRJ-B', bulgeKm: 470, minSweepRad: Math.PI * 0.9, planeChangeBias: 0.34 },
  { id: 'TRJ-C', bulgeKm: 120, minSweepRad: Math.PI * 1.18, planeChangeBias: 0.62 },
  { id: 'TRJ-D', bulgeKm: 330, minSweepRad: Math.PI * 0.74, planeChangeBias: 0.44 },
];

/** `TRANSFER_SHAPES` is a non-empty module constant, but `noUncheckedIndexedAccess` cannot
 * see that, so the wrapped index still needs a stated fallback. */
const DEFAULT_TRANSFER_SHAPE: TransferShape = {
  id: 'TRJ-A',
  bulgeKm: 210,
  minSweepRad: Math.PI * 0.62,
  planeChangeBias: 0.5,
};

export function transferShapeAt(index: number): TransferShape {
  const count = TRANSFER_SHAPES.length;
  const wrapped = ((index % count) + count) % count;
  return TRANSFER_SHAPES[wrapped] ?? DEFAULT_TRANSFER_SHAPE;
}

/**
 * The dominated alternatives drawn behind the chosen arc. They exist so the optimal path is
 * seen WINNING against something, which is the whole claim of a Pareto search — but they are
 * illustrative candidate geometries, not points from an engine-computed front. The page says
 * so in visible copy; this comment is the code-side half of the same statement.
 */
export const DOMINATED_SHAPES: readonly TransferShape[] = [
  { id: 'ALT-1', bulgeKm: 780, minSweepRad: Math.PI * 1.42, planeChangeBias: 0.72 },
  { id: 'ALT-2', bulgeKm: -140, minSweepRad: Math.PI * 0.42, planeChangeBias: 0.22 },
];

function smoothstep(s: number): number {
  return s * s * (3 - 2 * s);
}

/* --- The solve cycle ------------------------------------------------------ */

/** Seconds the arc takes to draw itself forward. Long enough to be watched, short enough
 * that it has finished before a reader who scrolled straight past would have noticed. */
const SOLVE_SWEEP_SEC = 1.9;
/** Seconds the settled arc holds before the scenario re-solves. */
const SOLVE_HOLD_SEC = 5.2;
/** Seconds spent dimming out before the geometry changes, so a re-solve reads as a
 * recomputation rather than as a jump cut. */
const SOLVE_FADE_SEC = 0.45;
const SOLVE_CYCLE_SEC = SOLVE_SWEEP_SEC + SOLVE_HOLD_SEC + SOLVE_FADE_SEC;

/**
 * The replanning loop, shared by the arc, its travelling pulses and the waypoint markers so
 * all three agree on which candidate is currently adopted and how much of it is drawn.
 *
 * Mutable for the same reason `MissionFrame` is: it is written once per frame and read by
 * several components, and it must never cause a React render.
 */
export interface SolveState {
  index: number;
  timer: number;
  /** Fraction of the arc currently drawn, 0..1. */
  progress: number;
  /** Intensity multiplier, dipped between solves. */
  alpha: number;
  shape: TransferShape;
}

export function createSolveState(): SolveState {
  return { index: 0, timer: 0, progress: 1, alpha: 1, shape: transferShapeAt(0) };
}

function easeOutCubic(t: number): number {
  const u = 1 - t;
  return 1 - u * u * u;
}

export function advanceSolveState(
  solve: SolveState,
  deltaSec: number,
  reducedMotion: boolean,
): void {
  if (reducedMotion) {
    // The settled result, drawn in full. Reduced motion removes the animation, never the
    // content — a reader who cannot watch the search still sees what it found.
    solve.progress = 1;
    solve.alpha = 1;
    solve.shape = transferShapeAt(0);
    return;
  }
  solve.timer += deltaSec;
  if (solve.timer >= SOLVE_CYCLE_SEC) {
    solve.timer -= SOLVE_CYCLE_SEC;
    solve.index += 1;
  }
  solve.shape = transferShapeAt(solve.index);
  const t = solve.timer;
  solve.progress = t < SOLVE_SWEEP_SEC ? easeOutCubic(t / SOLVE_SWEEP_SEC) : 1;
  const fadeStart = SOLVE_SWEEP_SEC + SOLVE_HOLD_SEC;
  solve.alpha = t > fadeStart ? 1 - 0.88 * ((t - fadeStart) / SOLVE_FADE_SEC) : 1;
}

/** Angular sweep from departure to arrival, lifted into `[minSweep, minSweep + 2 pi)`.
 * Arrival phase is preserved modulo a full revolution, so the arc still ends exactly on the
 * casualty however far the two have drifted apart. */
function sweepAngle(frame: MissionFrame, shape: TransferShape): number {
  const twoPi = Math.PI * 2;
  let delta = frame.targetTheta - frame.rescueTheta;
  delta -= Math.floor((delta - shape.minSweepRad) / twoPi) * twoPi;
  return delta;
}

/**
 * A point on the transfer, `s` in [0, 1], written into `out`.
 *
 * The arc is generated by interpolating ELEMENTS between the two orbits — altitude,
 * inclination, RAAN — and evaluating the same position expression the orbits themselves use.
 * That is why it lies between the two shells and tilts out of the departure plane as it
 * climbs, rather than being a bezier eyeballed into position. At s = 0 the element set is
 * exactly the rescue orbit at the vehicle's current phase, and at s = 1 exactly the target
 * orbit at the casualty's, so both endpoints land on the spacecraft by construction.
 *
 * It is still an illustration: no burn is solved, no delta-v is computed, and the engine is
 * never consulted.
 */
export function writeTransferPoint(
  frame: MissionFrame,
  shape: TransferShape,
  s: number,
  exaggeration: number,
  out: Vector3,
): Vector3 {
  const climb = smoothstep(s);
  // Plane change is bought over a narrower window than the climb, centred on the bias, so
  // the arc visibly rolls out of one plane and into the other instead of easing uniformly.
  // The half-width is clamped to keep that window inside [0, 1]: if it spilled past either
  // end the arc would leave the departure plane before s = 0 or arrive short of the target
  // plane at s = 1, and the endpoints would no longer sit exactly on the two spacecraft.
  const bias = Math.min(0.85, Math.max(0.15, shape.planeChangeBias));
  const halfWidth = Math.min(bias, 1 - bias, 0.45);
  const planeS = Math.min(1, Math.max(0, (s - (bias - halfWidth)) / (2 * halfWidth)));
  const plane = smoothstep(planeS);
  const altitudeKm =
    RESCUE_ORBIT.altitudeKm +
    (TARGET_ORBIT.altitudeKm - RESCUE_ORBIT.altitudeKm) * climb +
    shape.bulgeKm * Math.sin(Math.PI * s);
  const inclinationDeg =
    RESCUE_ORBIT.inclinationDeg +
    (TARGET_ORBIT.inclinationDeg - RESCUE_ORBIT.inclinationDeg) * plane;
  const raanDeg = RESCUE_ORBIT.raanDeg + (TARGET_ORBIT.raanDeg - RESCUE_ORBIT.raanDeg) * plane;
  const theta = frame.rescueTheta + sweepAngle(frame, shape) * s;
  return writePositionOnOrbit(
    { altitudeKm, inclinationDeg, raanDeg, phaseDeg: 0, band: RESCUE_ORBIT.band },
    theta,
    exaggeration,
    out,
  );
}
