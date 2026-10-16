/**
 * A deterministic in-browser stand-in for the C++ engine's snapshot stream.
 *
 * ## Why this exists
 *
 * The workspace reads one thing: `SimulationSnapshot`, polled from the Node bridge, which
 * spawns the real engine. That is the correct architecture and it is unchanged. But the bridge
 * needs a native binary and a long-lived process, which a static host (Vercel, GitHub Pages, a
 * reviewer's laptop with no toolchain) does not have -- and `EngineOfflineOverlay` correctly
 * covers the entire workspace when the engine is unreachable. The result was a deployed site on
 * which none of the work could be seen at all.
 *
 * So this module produces a snapshot of the same shape from the same orbital relationships,
 * entirely in the browser, and `EngineContext` falls back to it when the bridge is absent.
 *
 * ## What it is honest about
 *
 * It is a *kinematic* model, not the engine. Positions come from closed-form circular orbital
 * motion; mission progress, fuel burn, emergency windows and metric counters advance on the
 * same clock. The Pareto fronts, algorithm telemetry and graph costs are generated from the
 * same objective weights the engine uses, so the shapes and the trade-off curves are
 * representative -- but no Dijkstra, no label-setting search and no Hungarian solve runs here.
 * Every consumer is told which source it is reading via `snapshot.scenarioName` and the
 * workspace's own `DemoModeBanner`, and nothing in this file is ever presented as engine output.
 *
 * Determinism is by seeded LCG, so the same simulation second always yields the same snapshot:
 * a reviewer can be pointed at a timestamp and see what the author saw.
 */

import type {
  AlgorithmRunView,
  DebrisView,
  EmergencyView,
  EventView,
  GraphEdgeView,
  GraphNodeView,
  MissionLegView,
  MissionView,
  ObjectiveVectorView,
  ParetoPointView,
  PositionView,
  SatelliteView,
  SimulationSnapshot,
  VehicleView,
} from '../types/engine';

const EARTH_RADIUS_KM = 6371;
const MU_KM3_S2 = 398_600.4418;
const DEG = Math.PI / 180;

/** Numerical Recipes' LCG constants. Chosen because the values are standard and the generator
 * is two operations -- this is for reproducible layout, never for anything security-adjacent. */
function lcg(seed: number): () => number {
  let state = seed >>> 0;
  return () => {
    state = (Math.imul(1_664_525, state) + 1_013_904_223) >>> 0;
    return state / 0x1_0000_0000;
  };
}

function orbitalPeriodSec(altitudeKm: number): number {
  const a = EARTH_RADIUS_KM + altitudeKm;
  return 2 * Math.PI * Math.sqrt((a * a * a) / MU_KM3_S2);
}

/** Circular orbit in ECI, built from the same four elements the engine reports (altitude,
 * inclination, RAAN, phase) so a position shown here is geometrically consistent with the
 * orbit block beside it. */
function positionFor(
  altitudeKm: number,
  inclinationDeg: number,
  raanDeg: number,
  phaseDeg: number,
  timeSec: number,
): PositionView {
  const radius = EARTH_RADIUS_KM + altitudeKm;
  const meanMotion = (2 * Math.PI) / orbitalPeriodSec(altitudeKm);
  const theta = phaseDeg * DEG + meanMotion * timeSec;
  const inc = inclinationDeg * DEG;
  const raan = raanDeg * DEG;

  const xOrbit = radius * Math.cos(theta);
  const yOrbit = radius * Math.sin(theta);

  return {
    x: xOrbit * Math.cos(raan) - yOrbit * Math.cos(inc) * Math.sin(raan),
    y: xOrbit * Math.sin(raan) + yOrbit * Math.cos(inc) * Math.cos(raan),
    z: yOrbit * Math.sin(inc),
  };
}

function round(value: number, places = 3): number {
  const factor = 10 ** places;
  return Math.round(value * factor) / factor;
}

/** Smooth, bounded, period-free wobble for health and fuel readings: three incommensurable
 * sines, so nothing in the UI ever looks like it is ticking to a metronome. */
function drift(seed: number, timeSec: number, amplitude: number): number {
  return (
    amplitude *
    (0.5 * Math.sin(timeSec / 311 + seed) +
      0.3 * Math.sin(timeSec / 97 + seed * 2.3) +
      0.2 * Math.sin(timeSec / 1_013 + seed * 0.7))
  );
}

function clamp01(value: number): number {
  return Math.min(1, Math.max(0, value));
}

const SAT_PREFIXES = [
  'SENTINEL',
  'ORBCOMM',
  'IRIDIUM',
  'STARLINK',
  'LANDSAT',
  'HELIOS',
  'TERRA',
  'AQUA',
  'KOMPSAT',
  'CARTOSAT',
  'RESOURCESAT',
  'OCEANSAT',
  'RISAT',
  'GSAT',
  'ASTROSAT',
  'CHANDRA',
] as const;

const BANDS = ['LEO-LOW', 'LEO-MID', 'LEO-HIGH'] as const;

function bandFor(altitudeKm: number): string {
  if (altitudeKm < 600) return BANDS[0];
  if (altitudeKm < 720) return BANDS[1];
  return BANDS[2];
}

interface SatelliteSeed {
  readonly id: string;
  readonly name: string;
  readonly altitudeKm: number;
  readonly inclinationDeg: number;
  readonly raanDeg: number;
  readonly phaseDeg: number;
  readonly seed: number;
  /** Simulation second at which this satellite's subsystem degrades into an emergency, or
   * `null` for one that stays nominal for the whole run. */
  readonly failAtSec: number | null;
  readonly failKind: string;
  /** Period between successive failures of this satellite, in simulation seconds. */
  readonly recurrenceSec: number;
}

interface VehicleSeed {
  readonly id: string;
  readonly name: string;
  readonly altitudeKm: number;
  readonly inclinationDeg: number;
  readonly raanDeg: number;
  readonly phaseDeg: number;
  readonly deltaVBudgetKms: number;
  readonly seed: number;
}

const SATELLITE_COUNT = 18;
const VEHICLE_COUNT = 5;
const DEBRIS_COUNT = 42;

const FAILURE_KINDS = [
  'POWER_DEGRADATION',
  'PROPULSION_LEAK',
  'COMMS_LOSS',
  'THERMAL_RUNAWAY',
  'ATTITUDE_LOSS',
] as const;

/** Built once per module load: the scenario is fixed for the deployment, exactly as a loaded
 * scenario file is fixed for an engine run. */
const SATELLITES: readonly SatelliteSeed[] = (() => {
  const random = lcg(0x0_7ca_51);
  const out: SatelliteSeed[] = [];
  for (let i = 0; i < SATELLITE_COUNT; i += 1) {
    const altitudeKm = 500 + Math.round(random() * 300);
    const prefix = SAT_PREFIXES[i % SAT_PREFIXES.length] ?? 'SENTINEL';
    // Roughly three satellites in five are failure-prone. Combined with the recurrence period
    // below this keeps a handful of emergencies live at any moment, which is the state the
    // scheduler is interesting in -- an empty queue demonstrates nothing.
    const fails = random() < 0.6;
    out.push({
      id: `SAT-${String(i + 1).padStart(3, '0')}`,
      name: `${prefix}-${String(i + 1).padStart(2, '0')}`,
      altitudeKm,
      inclinationDeg: round(45 + random() * 53, 2),
      raanDeg: round(random() * 360, 2),
      phaseDeg: round(random() * 360, 2),
      seed: random() * 100,
      failAtSec: fails ? Math.round(240 + random() * 9_000) : null,
      // Each failure-prone satellite degrades again on its own period, so the scenario has a
      // steady arrival process rather than one burst that drains and never refills.
      recurrenceSec: Math.round(5_400 + random() * 7_200),
      failKind: FAILURE_KINDS[Math.floor(random() * FAILURE_KINDS.length)] ?? FAILURE_KINDS[0],
    });
  }
  return out;
})();

const VEHICLES: readonly VehicleSeed[] = (() => {
  const random = lcg(0x0_ca_11ed);
  const out: VehicleSeed[] = [];
  const names = ['ORCA-PRIME', 'ORCA-SECOND', 'ORCA-THIRD', 'ORCA-FOURTH', 'ORCA-RESERVE'];
  for (let i = 0; i < VEHICLE_COUNT; i += 1) {
    out.push({
      id: `RV-${String(i + 1).padStart(2, '0')}`,
      name: names[i] ?? `ORCA-${i + 1}`,
      altitudeKm: 540 + Math.round(random() * 240),
      inclinationDeg: round(50 + random() * 46, 2),
      raanDeg: round(random() * 360, 2),
      phaseDeg: round(random() * 360, 2),
      deltaVBudgetKms: round(2.4 + random() * 1.8, 3),
      seed: random() * 100,
    });
  }
  return out;
})();

const DEBRIS: readonly {
  readonly id: string;
  readonly altitudeKm: number;
  readonly inclinationDeg: number;
  readonly raanDeg: number;
  readonly phaseDeg: number;
  readonly hazardRadiusKm: number;
  readonly densityFactor: number;
  readonly detectAtSec: number;
}[] = (() => {
  const random = lcg(0x0_deb_215);
  return Array.from({ length: DEBRIS_COUNT }, (_, i) => ({
    id: `DEB-${String(i + 1).padStart(3, '0')}`,
    altitudeKm: 480 + Math.round(random() * 340),
    inclinationDeg: round(40 + random() * 60, 2),
    raanDeg: round(random() * 360, 2),
    phaseDeg: round(random() * 360, 2),
    hazardRadiusKm: round(8 + random() * 34, 2),
    densityFactor: round(0.2 + random() * 0.8, 3),
    detectAtSec: Math.round(random() * 2_400),
  }));
})();

/** Emergency lifecycle, derived from the clock rather than stored, so every call for the same
 * second produces the same queue regardless of call order. */
interface EmergencyState {
  readonly emergency: EmergencyView;
  readonly satellite: SatelliteSeed;
}

const RESPONSE_WINDOW_SEC = 5_400;
const MISSION_DURATION_SEC = 3_000;

function severityFor(remainingSec: number): string {
  if (remainingSec < 900) return 'CRITICAL';
  if (remainingSec < 2_400) return 'HIGH';
  if (remainingSec < 4_000) return 'ELEVATED';
  return 'NOMINAL';
}

function emergenciesAt(timeSec: number): readonly EmergencyState[] {
  const out: EmergencyState[] = [];
  let index = 0;

  for (const satellite of SATELLITES) {
    if (satellite.failAtSec === null || timeSec < satellite.failAtSec) continue;
    index += 1;

    // The most recent occurrence of this satellite's recurring failure, which is the only one
    // that can still be open. Earlier occurrences are history and belong to the event feed.
    const occurrence = Math.floor((timeSec - satellite.failAtSec) / satellite.recurrenceSec);
    const detectedAtSec = satellite.failAtSec + occurrence * satellite.recurrenceSec;
    const deadlineSec = detectedAtSec + RESPONSE_WINDOW_SEC;
    const remainingWindowSec = Math.max(0, deadlineSec - timeSec);
    const elapsed = timeSec - detectedAtSec;

    // A mission is dispatched shortly after detection and resolves after its duration; past
    // that the emergency reads RESOLVED and drops out of the active queue's urgency ordering.
    const state =
      elapsed > MISSION_DURATION_SEC + 240
        ? 'RESOLVED'
        : elapsed > 180
          ? 'ASSIGNED'
          : 'DETECTED';

    out.push({
      satellite,
      emergency: {
        id: `EMG-${String(index).padStart(3, '0')}-${occurrence + 1}`,
        subjectId: satellite.id,
        kind: satellite.failKind,
        severity: state === 'RESOLVED' ? 'RESOLVED' : severityFor(remainingWindowSec),
        state,
        detectedAtSec,
        deadlineSec,
        remainingWindowSec: round(remainingWindowSec, 1),
        urgency: round(clamp01(1 - remainingWindowSec / RESPONSE_WINDOW_SEC), 4),
      },
    });
  }

  return out;
}

function objective(
  time: number,
  fuel: number,
  risk: number,
  distance: number,
): ObjectiveVectorView {
  return { time: round(time, 2), fuel: round(fuel, 4), risk: round(risk, 4), distance: round(distance, 2) };
}

/**
 * A Pareto front with the shape the real one has: a convex time/fuel trade-off, a handful of
 * dominated interior points so the Optimization Lab's dominance filter has something to filter,
 * and exactly one recommended point.
 */
function paretoFrontFor(missionSeed: number, pass: number, baseDeltaV: number): ParetoPointView[] {
  const random = lcg(Math.round(missionSeed * 7_919 + pass * 104_729));
  const points: ParetoPointView[] = [];
  const count = pass === 1 ? 6 : 1;

  for (let i = 0; i < count; i += 1) {
    const t = count === 1 ? 0.5 : i / (count - 1);
    // Fuel falls as time rises: the hyperbolic relationship a Hohmann-versus-fast-transfer
    // trade actually has, not a straight line.
    const timeSec = 900 + t * 3_600;
    const fuel = baseDeltaV * (1.45 - 0.55 * Math.sqrt(t));
    const risk = 0.08 + 0.26 * t + random() * 0.05;
    const distance = 1_200 + t * 5_400 + random() * 400;
    points.push({
      planId: `PLAN-${missionSeed}-${pass}-${i + 1}`,
      cost: objective(timeSec, fuel, risk, distance),
      dominated: false,
      recommended: false,
    });
  }

  if (count > 1) {
    // Two interior points nudged to be genuinely dominated, so `dominated` is computed from the
    // vectors rather than asserted.
    for (const i of [2, 4]) {
      const base = points[i];
      if (base === undefined) continue;
      points[i] = {
        ...base,
        cost: objective(
          base.cost.time * 1.18,
          base.cost.fuel * 1.14,
          base.cost.risk * 1.1,
          base.cost.distance * 1.06,
        ),
      };
    }
    for (let i = 0; i < points.length; i += 1) {
      const self = points[i];
      if (self === undefined) continue;
      const a = self.cost;
      const isDominated = points.some((other, j) => {
        if (i === j) return false;
        const b = other.cost;
        return (
          b.time <= a.time &&
          b.fuel <= a.fuel &&
          b.risk <= a.risk &&
          (b.time < a.time || b.fuel < a.fuel || b.risk < a.risk)
        );
      });
      points[i] = { ...self, dominated: isDominated };
    }
  }

  // The recommendation is the weighted pick the engine's default profile makes: time 0.45,
  // fuel 0.35, risk 0.20 over per-objective normalised costs.
  const live = points.filter((point) => !point.dominated);
  const pool = live.length > 0 ? live : points;
  const maxTime = Math.max(...pool.map((p) => p.cost.time));
  const maxFuel = Math.max(...pool.map((p) => p.cost.fuel));
  const maxRisk = Math.max(...pool.map((p) => p.cost.risk));
  let bestId: string | null = pool[0]?.planId ?? null;
  let bestScore = Number.POSITIVE_INFINITY;
  for (const point of pool) {
    const score =
      0.45 * (point.cost.time / maxTime) +
      0.35 * (point.cost.fuel / maxFuel) +
      0.2 * (point.cost.risk / maxRisk);
    if (score < bestScore) {
      bestScore = score;
      bestId = point.planId;
    }
  }

  return points.map((point) => ({ ...point, recommended: point.planId === bestId }));
}

function legsFor(
  vehicle: VehicleSeed,
  satellite: SatelliteSeed,
  startedAtSec: number,
  timeSec: number,
): MissionLegView[] {
  const legCount = 2;
  const legDuration = MISSION_DURATION_SEC / legCount;
  const out: MissionLegView[] = [];

  // Leg 1 is a phasing burn to an intermediate altitude; leg 2 is the rendezvous. Two legs is
  // what the engine's planner produces for a same-plane transfer with a phasing requirement.
  const midAltitude = (vehicle.altitudeKm + satellite.altitudeKm) / 2;

  for (let i = 0; i < legCount; i += 1) {
    const departureTimeSec = startedAtSec + i * legDuration;
    const arrivalTimeSec = departureTimeSec + legDuration;
    const fromAlt = i === 0 ? vehicle.altitudeKm : midAltitude;
    const toAlt = i === 0 ? midAltitude : satellite.altitudeKm;

    out.push({
      fromEntityId: i === 0 ? vehicle.id : `WAYPOINT-${vehicle.id}-1`,
      toEntityId: i === legCount - 1 ? satellite.id : `WAYPOINT-${vehicle.id}-1`,
      fromPosition: positionFor(
        fromAlt,
        vehicle.inclinationDeg,
        vehicle.raanDeg,
        vehicle.phaseDeg,
        departureTimeSec,
      ),
      toPosition: positionFor(
        toAlt,
        satellite.inclinationDeg,
        satellite.raanDeg,
        satellite.phaseDeg,
        arrivalTimeSec,
      ),
      departureTimeSec: round(departureTimeSec, 1),
      arrivalTimeSec: round(arrivalTimeSec, 1),
      deltaVKms: round(0.18 + Math.abs(toAlt - fromAlt) / 2_400, 4),
      status:
        timeSec >= arrivalTimeSec ? 'completed' : timeSec >= departureTimeSec ? 'current' : 'upcoming',
    });
  }

  return out;
}

function missionsAt(timeSec: number, states: readonly EmergencyState[]): MissionView[] {
  const out: MissionView[] = [];

  states.forEach((state, index) => {
    const { emergency, satellite } = state;
    const startedAtSec = emergency.detectedAtSec + 180;
    if (timeSec < startedAtSec) return;

    const vehicle = VEHICLES[index % VEHICLES.length];
    if (vehicle === undefined) return;
    const elapsed = timeSec - startedAtSec;
    const completedAtSec = startedAtSec + MISSION_DURATION_SEC;
    const complete = timeSec >= completedAtSec;
    const legs = legsFor(vehicle, satellite, startedAtSec, timeSec);
    const totalDeltaV = legs.reduce((sum, leg) => sum + leg.deltaVKms, 0);

    // Every third mission replans once, a third of the way in -- the case the Pareto history
    // and the explanation string exist to show.
    const replans = index % 3 === 2 && elapsed > MISSION_DURATION_SEC / 3 ? 1 : 0;
    const history: {
      readonly planningPass: number;
      readonly timestampSec: number;
      readonly points: readonly ParetoPointView[];
    }[] = [
      { planningPass: 1, timestampSec: round(startedAtSec, 1), points: paretoFrontFor(index + 1, 1, totalDeltaV) },
      ...(replans > 0
        ? [
            {
              planningPass: 2,
              timestampSec: round(startedAtSec + MISSION_DURATION_SEC / 3, 1),
              points: paretoFrontFor(index + 1, 2, totalDeltaV * 1.08),
            },
          ]
        : []),
    ];

    const currentLeg = Math.min(legs.length - 1, Math.floor(elapsed / (MISSION_DURATION_SEC / legs.length)));
    const legSpan = MISSION_DURATION_SEC / legs.length;

    out.push({
      id: `MSN-${String(index + 1).padStart(3, '0')}`,
      vehicleId: vehicle.id,
      emergencyId: emergency.id,
      state: complete ? 'COMPLETED' : replans > 0 && elapsed < MISSION_DURATION_SEC / 3 + 60 ? 'REPLANNING' : 'EN_ROUTE',
      currentLeg,
      progressOnLeg: complete ? 1 : round(clamp01((elapsed - currentLeg * legSpan) / legSpan), 4),
      deltaVSpentKms: round(totalDeltaV * clamp01(elapsed / MISSION_DURATION_SEC), 4),
      replanCount: replans,
      startedAtSec: round(startedAtSec, 1),
      completedAtSec: complete ? round(completedAtSec, 1) : null,
      legs,
      paretoFront: history[history.length - 1]?.points ?? [],
      paretoHistory: history,
      activeRescoreProfile: null,
      explanation:
        replans > 0
          ? `Replanned at T+${Math.round(startedAtSec + MISSION_DURATION_SEC / 3)}s: a newly ` +
            `detected debris conjunction raised the risk objective on leg 2 above the 0.35 ` +
            `abort threshold. The label-setting search returned a longer, lower-risk route ` +
            `costing ${round(totalDeltaV * 0.08, 3)} km/s more delta-V, which remained inside ` +
            `${vehicle.name}'s remaining budget.`
          : null,
    });
  });

  return out;
}

function eventsAt(
  timeSec: number,
  states: readonly EmergencyState[],
  missions: readonly MissionView[],
): EventView[] {
  const out: EventView[] = [];

  for (const { emergency, satellite } of states) {
    out.push({
      id: `EVT-DET-${emergency.id}`,
      kind: 'EMERGENCY_DETECTED',
      timestampSec: emergency.detectedAtSec,
      satelliteId: satellite.id,
      vehicleId: null,
      emergencyId: emergency.id,
      missionId: null,
      debrisId: null,
      detail: `${satellite.name} reported ${emergency.kind.replace(/_/g, ' ').toLowerCase()}; response window ${Math.round(RESPONSE_WINDOW_SEC / 60)} min.`,
    });
  }

  for (const mission of missions) {
    out.push({
      id: `EVT-DSP-${mission.id}`,
      kind: 'MISSION_DISPATCHED',
      timestampSec: mission.startedAtSec,
      satelliteId: null,
      vehicleId: mission.vehicleId,
      emergencyId: mission.emergencyId,
      missionId: mission.id,
      debrisId: null,
      detail: `${mission.vehicleId} assigned to ${mission.emergencyId} across ${mission.legs.length} legs.`,
    });

    if (mission.replanCount > 0) {
      out.push({
        id: `EVT-RPL-${mission.id}`,
        kind: 'MISSION_REPLANNED',
        timestampSec: round(mission.startedAtSec + MISSION_DURATION_SEC / 3, 1),
        satelliteId: null,
        vehicleId: mission.vehicleId,
        emergencyId: mission.emergencyId,
        missionId: mission.id,
        debrisId: null,
        detail: 'Debris conjunction raised leg-2 risk past threshold; lower-risk route adopted.',
      });
    }

    if (mission.completedAtSec !== null) {
      out.push({
        id: `EVT-CMP-${mission.id}`,
        kind: 'MISSION_COMPLETED',
        timestampSec: mission.completedAtSec,
        satelliteId: null,
        vehicleId: mission.vehicleId,
        emergencyId: mission.emergencyId,
        missionId: mission.id,
        debrisId: null,
        detail: `Rendezvous achieved; ${mission.deltaVSpentKms} km/s expended.`,
      });
    }
  }

  for (const debris of DEBRIS) {
    if (timeSec < debris.detectAtSec) continue;
    out.push({
      id: `EVT-DEB-${debris.id}`,
      kind: 'DEBRIS_DETECTED',
      timestampSec: debris.detectAtSec,
      satelliteId: null,
      vehicleId: null,
      emergencyId: null,
      missionId: null,
      debrisId: debris.id,
      detail: `Tracked object at ${debris.altitudeKm} km, hazard radius ${debris.hazardRadiusKm} km.`,
    });
  }

  // Newest first, capped: the timeline renders a window, and an unbounded list would grow for
  // as long as the tab is open.
  return out
    .filter((event) => event.timestampSec <= timeSec)
    .sort((a, b) => b.timestampSec - a.timestampSec)
    .slice(0, 80);
}

function algorithmRunsAt(timeSec: number, missions: readonly MissionView[]): AlgorithmRunView[] {
  const runs: AlgorithmRunView[] = [];
  const random = lcg(0x0_a16_0 + Math.floor(timeSec / 60));

  const nodeCount = SATELLITES.length + VEHICLES.length;
  const edgeCount = nodeCount * (nodeCount - 1);

  for (const mission of missions.slice(0, 6)) {
    const expanded = Math.round(nodeCount * (0.55 + random() * 0.4));
    const evaluated = Math.round(edgeCount * (0.3 + random() * 0.4));

    runs.push({
      algorithm: 'DIJKSTRA',
      timestampSec: mission.startedAtSec,
      found: true,
      arrivalTimeSec: mission.legs[mission.legs.length - 1]?.arrivalTimeSec ?? null,
      executionTimeMs: round(0.4 + random() * 1.6, 3),
      nodesExpanded: expanded,
      edgesEvaluated: evaluated,
      relaxationsSucceeded: Math.round(evaluated * 0.22),
      relaxationsRejected: Math.round(evaluated * 0.78),
      priorityQueuePushes: Math.round(expanded * 1.8),
      priorityQueuePops: expanded,
      staleEntriesDiscarded: Math.round(expanded * 0.3),
      heuristicEvaluations: null,
      labelsGenerated: null,
      labelsInserted: null,
      labelsDominated: null,
      labelsPruned: null,
      labelsExpanded: null,
      dominanceComparisons: null,
      queuePushes: null,
      queuePops: null,
      maximumLabelsAtNode: null,
      truncated: null,
      vehicleCount: null,
      emergencyCount: null,
      candidatePairsEvaluated: null,
      feasibleCandidatePairs: null,
      assignedPairs: null,
      solverMatrixDimension: null,
      solverOperationCount: null,
    });

    const dijkstra = runs[runs.length - 1];
    if (dijkstra === undefined) continue;

    // A* expands strictly fewer nodes than Dijkstra on the same query, which is the whole
    // point of the comparison the Algorithm Lab draws.
    runs.push({
      ...dijkstra,
      algorithm: 'ASTAR',
      executionTimeMs: round(0.25 + random() * 1.0, 3),
      nodesExpanded: Math.round(expanded * (0.45 + random() * 0.2)),
      edgesEvaluated: Math.round(evaluated * (0.5 + random() * 0.2)),
      heuristicEvaluations: Math.round(expanded * 1.3),
    });

    const labelsGenerated = Math.round(expanded * (6 + random() * 5));
    runs.push({
      algorithm: 'PARETO',
      timestampSec: mission.startedAtSec,
      found: true,
      arrivalTimeSec: mission.legs[mission.legs.length - 1]?.arrivalTimeSec ?? null,
      executionTimeMs: round(2.2 + random() * 5.5, 3),
      nodesExpanded: null,
      edgesEvaluated: null,
      relaxationsSucceeded: null,
      relaxationsRejected: null,
      priorityQueuePushes: null,
      priorityQueuePops: null,
      staleEntriesDiscarded: null,
      heuristicEvaluations: null,
      labelsGenerated,
      labelsInserted: Math.round(labelsGenerated * 0.42),
      labelsDominated: Math.round(labelsGenerated * 0.51),
      labelsPruned: Math.round(labelsGenerated * 0.07),
      labelsExpanded: Math.round(labelsGenerated * 0.38),
      dominanceComparisons: Math.round(labelsGenerated * 3.4),
      queuePushes: Math.round(labelsGenerated * 0.42),
      queuePops: Math.round(labelsGenerated * 0.38),
      maximumLabelsAtNode: Math.round(4 + random() * 9),
      truncated: false,
      vehicleCount: null,
      emergencyCount: null,
      candidatePairsEvaluated: null,
      feasibleCandidatePairs: null,
      assignedPairs: null,
      solverMatrixDimension: null,
      solverOperationCount: null,
    });
  }

  const pairs = VEHICLES.length * Math.max(1, missions.length);
  const dimension = Math.max(VEHICLES.length, missions.length, 1);
  runs.push({
    algorithm: 'HUNGARIAN',
    timestampSec: round(timeSec, 1),
    found: true,
    arrivalTimeSec: null,
    executionTimeMs: round(0.6 + Math.pow(dimension, 3) / 90_000, 3),
    nodesExpanded: null,
    edgesEvaluated: null,
    relaxationsSucceeded: null,
    relaxationsRejected: null,
    priorityQueuePushes: null,
    priorityQueuePops: null,
    staleEntriesDiscarded: null,
    heuristicEvaluations: null,
    labelsGenerated: null,
    labelsInserted: null,
    labelsDominated: null,
    labelsPruned: null,
    labelsExpanded: null,
    dominanceComparisons: null,
    queuePushes: null,
    queuePops: null,
    maximumLabelsAtNode: null,
    truncated: null,
    vehicleCount: VEHICLES.length,
    emergencyCount: missions.length,
    candidatePairsEvaluated: pairs,
    feasibleCandidatePairs: Math.round(pairs * 0.72),
    assignedPairs: Math.min(VEHICLES.length, missions.length),
    solverMatrixDimension: dimension,
    // O(n^3), which is what the cubic Hungarian implementation in `cpp/algorithms/` costs.
    solverOperationCount: Math.pow(dimension, 3),
  });

  return runs.sort((a, b) => b.timestampSec - a.timestampSec).slice(0, 40);
}

function graphAt(): { nodes: GraphNodeView[]; edges: GraphEdgeView[] } {
  const nodes: GraphNodeView[] = [
    ...SATELLITES.map((satellite) => ({
      id: `N-${satellite.id}`,
      kind: 'SATELLITE' as const,
      entityId: satellite.id,
      orbit: {
        altitudeKm: satellite.altitudeKm,
        inclinationDeg: satellite.inclinationDeg,
        raanDeg: satellite.raanDeg,
        phaseDeg: satellite.phaseDeg,
        band: bandFor(satellite.altitudeKm),
      },
    })),
    ...VEHICLES.map((vehicle) => ({
      id: `N-${vehicle.id}`,
      kind: 'RESCUE_VEHICLE' as const,
      entityId: vehicle.id,
      orbit: {
        altitudeKm: vehicle.altitudeKm,
        inclinationDeg: vehicle.inclinationDeg,
        raanDeg: vehicle.raanDeg,
        phaseDeg: vehicle.phaseDeg,
        band: bandFor(vehicle.altitudeKm),
      },
    })),
  ];

  const edges: GraphEdgeView[] = [];
  for (const from of nodes) {
    for (const to of nodes) {
      if (from.id === to.id) continue;
      const dAlt = Math.abs(from.orbit.altitudeKm - to.orbit.altitudeKm);
      const dInc = Math.abs(from.orbit.inclinationDeg - to.orbit.inclinationDeg);

      // A plane change is the expensive manoeuvre, by a wide margin: ~0.13 km/s per degree at
      // LEO velocity, against ~0.0004 km/s per km of altitude. Edges past the budget of any
      // vehicle are kept but marked infeasible, which is what the engine's graph does.
      const fuel = dAlt * 0.000_42 + dInc * 0.132;
      if (fuel > 3.6) continue;

      edges.push({
        id: `${from.id}->${to.id}`,
        from: from.id,
        to: to.id,
        cost: objective(900 + dAlt * 1.8 + dInc * 42, fuel, 0.05 + dInc / 240, dAlt * 3.1 + dInc * 86),
        feasible: fuel <= 2.4,
      });
    }
  }

  return { nodes, edges };
}

const GRAPH = graphAt();

export interface DemoSnapshotOptions {
  readonly timeSec: number;
  readonly status: 'RUNNING' | 'PAUSED';
  readonly speedMultiplier: number;
}

export function demoSnapshot({
  timeSec,
  status,
  speedMultiplier,
}: DemoSnapshotOptions): SimulationSnapshot {
  const states = emergenciesAt(timeSec);
  const missions = missionsAt(timeSec, states);
  const events = eventsAt(timeSec, states, missions);

  const satellites: SatelliteView[] = SATELLITES.map((seed) => {
    const failed = seed.failAtSec !== null && timeSec >= seed.failAtSec;
    const emergency = states.find((state) => state.satellite.id === seed.id);
    const resolved = emergency?.emergency.state === 'RESOLVED';

    // The failing subsystem degrades; the rest keep their nominal drift. A resolved emergency
    // recovers the subsystem to a serviced-but-not-new level rather than back to 1.0.
    const degradation = failed ? (resolved ? 0.26 : 0.62) : 0;
    const base = (offset: number) => clamp01(0.86 + drift(seed.seed + offset, timeSec, 0.12));

    return {
      id: seed.id,
      name: seed.name,
      status: resolved ? 'SERVICED' : failed ? 'EMERGENCY' : 'NOMINAL',
      position: positionFor(seed.altitudeKm, seed.inclinationDeg, seed.raanDeg, seed.phaseDeg, timeSec),
      orbit: {
        altitudeKm: seed.altitudeKm,
        inclinationDeg: seed.inclinationDeg,
        raanDeg: seed.raanDeg,
        phaseDeg: round((seed.phaseDeg + (timeSec / orbitalPeriodSec(seed.altitudeKm)) * 360) % 360, 2),
        band: bandFor(seed.altitudeKm),
      },
      fuelFraction: round(clamp01(0.42 + drift(seed.seed, timeSec, 0.1) + 0.3), 4),
      health: {
        power: round(clamp01(base(0) - (seed.failKind === 'POWER_DEGRADATION' ? degradation : 0)), 4),
        propulsion: round(clamp01(base(1) - (seed.failKind === 'PROPULSION_LEAK' ? degradation : 0)), 4),
        comms: round(clamp01(base(2) - (seed.failKind === 'COMMS_LOSS' ? degradation : 0)), 4),
        thermal: round(clamp01(base(3) - (seed.failKind === 'THERMAL_RUNAWAY' ? degradation : 0)), 4),
      },
      activeEmergencyId:
        emergency && emergency.emergency.state !== 'RESOLVED' ? emergency.emergency.id : null,
    };
  });

  const rescueVehicles: VehicleView[] = VEHICLES.map((seed) => {
    const mission = missions.find(
      (entry) => entry.vehicleId === seed.id && entry.completedAtSec === null,
    );
    const spent = missions
      .filter((entry) => entry.vehicleId === seed.id)
      .reduce((sum, entry) => sum + entry.deltaVSpentKms, 0);
    const remaining = Math.max(0, seed.deltaVBudgetKms - spent);

    return {
      id: seed.id,
      name: seed.name,
      status: mission ? (mission.state === 'REPLANNING' ? 'REPLANNING' : 'EN_ROUTE') : 'STANDBY',
      position: positionFor(seed.altitudeKm, seed.inclinationDeg, seed.raanDeg, seed.phaseDeg, timeSec),
      orbit: {
        altitudeKm: seed.altitudeKm,
        inclinationDeg: seed.inclinationDeg,
        raanDeg: seed.raanDeg,
        phaseDeg: round((seed.phaseDeg + (timeSec / orbitalPeriodSec(seed.altitudeKm)) * 360) % 360, 2),
        band: bandFor(seed.altitudeKm),
      },
      deltaVBudgetKms: seed.deltaVBudgetKms,
      deltaVRemainingKms: round(remaining, 4),
      fuelFraction: round(clamp01(remaining / seed.deltaVBudgetKms), 4),
      health: {
        power: round(clamp01(0.93 + drift(seed.seed, timeSec, 0.05)), 4),
        propulsion: round(clamp01(0.9 + drift(seed.seed + 1, timeSec, 0.06)), 4),
        comms: round(clamp01(0.95 + drift(seed.seed + 2, timeSec, 0.04)), 4),
        thermal: round(clamp01(0.91 + drift(seed.seed + 3, timeSec, 0.05)), 4),
      },
      activeMissionId: mission?.id ?? null,
    };
  });

  const debris: DebrisView[] = DEBRIS.map((seed) => ({
    id: seed.id,
    position: positionFor(seed.altitudeKm, seed.inclinationDeg, seed.raanDeg, seed.phaseDeg, timeSec),
    hazardRadiusKm: seed.hazardRadiusKm,
    densityFactor: seed.densityFactor,
    detected: timeSec >= seed.detectAtSec,
  }));

  const completed = missions.filter((mission) => mission.completedAtSec !== null).length;

  return {
    type: 'snapshot',
    schemaVersion: 1,
    scenarioName: 'ORCA browser demonstration scenario (kinematic model — not the C++ engine)',
    simulationTimeSec: round(timeSec, 2),
    simulationStatus: status,
    speedMultiplier,
    satellites,
    rescueVehicles,
    debris,
    emergencies: states.map((state) => state.emergency),
    missions,
    recentEvents: events,
    metrics: {
      ticks: Math.floor(timeSec * 2),
      eventsProcessed: events.length + Math.floor(timeSec / 30),
      missionsUpdated: missions.length * Math.max(1, Math.floor(timeSec / 120)),
      replansTriggered: missions.reduce((sum, mission) => sum + mission.replanCount, 0),
      replansCompleted: missions.reduce((sum, mission) => sum + mission.replanCount, 0),
      missionSuccesses: completed,
      missionFailures: 0,
      missionAborts: 0,
    },
    graph: GRAPH,
    algorithmRuns: algorithmRunsAt(timeSec, missions),
  };
}

/** Exposed for the dashboard's "fleet standby" tile and for tests, which need the scenario's
 * fixed sizes without building a whole snapshot. */
export const DEMO_SCENARIO_SIZE = {
  satellites: SATELLITE_COUNT,
  vehicles: VEHICLE_COUNT,
  debris: DEBRIS_COUNT,
} as const;
