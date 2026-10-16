/**
 * TypeScript mirror of the C++ engine's NDJSON wire contract — must be kept in exact
 * agreement with `cpp/io/JsonSerializer.cpp`'s field names by hand (no schema-generation
 * tooling exists in this project). `docs/API_CONTRACT.md`'s Phase 9 section documents this
 * same shape with worked examples.
 *
 * IDs cross the wire as strings (e.g. "SAT-006"), never raw numbers — `toString(Id<Tag>)`
 * on the C++ side already produces the prefixed form, and using it directly sidesteps any
 * question of JavaScript's 2^53 safe-integer ceiling entirely (the ceiling is not close
 * for this project's `uint32_t` IDs either way, but the string form needs no argument about
 * it at all — see `INTEGRATION_ARCHITECTURE.md`'s ID serialization decision).
 */

export interface PositionView {
  readonly x: number;
  readonly y: number;
  readonly z: number;
}

export interface OrbitView {
  readonly altitudeKm: number;
  readonly inclinationDeg: number;
  readonly raanDeg: number;
  readonly phaseDeg: number;
  readonly band: string;
}

export interface HealthView {
  readonly power: number;
  readonly propulsion: number;
  readonly comms: number;
  readonly thermal: number;
}

export interface SatelliteView {
  readonly id: string;
  readonly name: string;
  readonly status: string;
  readonly position: PositionView;
  readonly orbit: OrbitView;
  readonly fuelFraction: number;
  readonly health: HealthView;
  readonly activeEmergencyId: string | null;
}

export interface VehicleView {
  readonly id: string;
  readonly name: string;
  readonly status: string;
  readonly position: PositionView;
  readonly orbit: OrbitView;
  readonly deltaVBudgetKms: number;
  readonly deltaVRemainingKms: number;
  readonly fuelFraction: number;
  readonly health: HealthView;
  readonly activeMissionId: string | null;
}

export interface DebrisView {
  readonly id: string;
  readonly position: PositionView;
  readonly hazardRadiusKm: number;
  readonly densityFactor: number;
  readonly detected: boolean;
}

export interface EmergencyView {
  readonly id: string;
  readonly subjectId: string;
  readonly kind: string;
  readonly severity: string;
  readonly state: string;
  readonly detectedAtSec: number;
  readonly deadlineSec: number;
  readonly remainingWindowSec: number;
  readonly urgency: number;
}

export interface MissionLegView {
  readonly fromEntityId: string;
  readonly toEntityId: string;
  readonly fromPosition: PositionView;
  readonly toPosition: PositionView;
  readonly departureTimeSec: number;
  readonly arrivalTimeSec: number;
  readonly deltaVKms: number;
  readonly status: 'completed' | 'current' | 'upcoming';
}

/** Direct mirror of `ObjectiveVector` (`core/ObjectiveVector.hpp`). */
export interface ObjectiveVectorView {
  readonly time: number;
  readonly fuel: number;
  readonly risk: number;
  readonly distance: number;
}

/** One point in a mission's Pareto front -- Phase 10, extended by Phase 11's Pareto
 * history (`cpp/simulation/SimulationState.hpp`'s `missionParetoHistory`). */
export interface ParetoPointView {
  readonly planId: string;
  readonly cost: ObjectiveVectorView;
  readonly dominated: boolean;
  readonly recommended: boolean;
}

/** One retained planning pass -- Phase 11 brief #52-54. Pass 1 is the mission's original
 * dispatch-time front (multi-point); pass 2+ comes from a later local replan and is
 * honestly narrower (single point: the plan actually adopted). */
export interface ParetoHistoryEntryView {
  readonly planningPass: number;
  readonly timestampSec: number;
  readonly points: readonly ParetoPointView[];
}

export interface MissionView {
  readonly id: string;
  readonly vehicleId: string;
  readonly emergencyId: string;
  readonly state: string;
  readonly currentLeg: number;
  readonly progressOnLeg: number;
  readonly deltaVSpentKms: number;
  readonly replanCount: number;
  readonly startedAtSec: number;
  readonly completedAtSec: number | null;
  readonly legs: readonly MissionLegView[];
  /** The CURRENT (most recently captured) planning pass's front. */
  readonly paretoFront: readonly ParetoPointView[];
  /** Every retained planning pass, oldest first (Phase 11 brief #52-54). */
  readonly paretoHistory: readonly ParetoHistoryEntryView[];
  /** The profile name of the most recent live re-scoring request for this mission
   * (Phase 11 brief #50), if any -- `null` when `paretoFront`'s `recommended` point still
   * reflects the mission's actual adopted route rather than a what-if. */
  readonly activeRescoreProfile: string | null;
  /** Deterministic explanation of the most recent real replan/abort decision
   * (`planning/ReplanDecision.hpp::explain()`). `null` for a mission never replanned --
   * never fabricated (brief #26/#62). */
  readonly explanation: string | null;
}

export interface EventView {
  readonly id: string;
  readonly kind: string;
  readonly timestampSec: number;
  readonly satelliteId: string | null;
  readonly vehicleId: string | null;
  readonly emergencyId: string | null;
  readonly missionId: string | null;
  readonly debrisId: string | null;
  readonly detail: string;
}

export interface MetricsView {
  readonly ticks: number;
  readonly eventsProcessed: number;
  readonly missionsUpdated: number;
  readonly replansTriggered: number;
  readonly replansCompleted: number;
  readonly missionSuccesses: number;
  readonly missionFailures: number;
  readonly missionAborts: number;
}

/** One node of the mission graph -- Phase 10's Orbital Network view. Static per run (the
 * graph is built once at scenario load and never rebuilt), included in full on every
 * snapshot for simplicity. */
export interface GraphNodeView {
  readonly id: string;
  readonly kind: 'SATELLITE' | 'RESCUE_VEHICLE';
  readonly entityId: string;
  readonly orbit: OrbitView;
}

/** `cost` is the edge's BUILD-TIME reference sample, not what any live search decision
 * used (`cpp/graph/GraphEdge.hpp`'s own file comment) -- illustrative only. */
export interface GraphEdgeView {
  readonly id: string;
  readonly from: string;
  readonly to: string;
  readonly cost: ObjectiveVectorView;
  readonly feasible: boolean;
}

export interface GraphView {
  readonly nodes: readonly GraphNodeView[];
  readonly edges: readonly GraphEdgeView[];
}

/** Phase 11 Algorithm Lab telemetry -- a flattened projection of whichever metrics struct
 * the run actually produced; every field not applicable to that algorithm is `null`, never
 * a fabricated zero. */
export interface AlgorithmRunView {
  readonly algorithm: 'DIJKSTRA' | 'ASTAR' | 'PARETO' | 'HUNGARIAN' | 'GREEDY';
  readonly timestampSec: number;
  readonly found: boolean;
  readonly arrivalTimeSec: number | null;
  readonly executionTimeMs: number;
  readonly nodesExpanded: number | null;
  readonly edgesEvaluated: number | null;
  readonly relaxationsSucceeded: number | null;
  readonly relaxationsRejected: number | null;
  readonly priorityQueuePushes: number | null;
  readonly priorityQueuePops: number | null;
  readonly staleEntriesDiscarded: number | null;
  readonly heuristicEvaluations: number | null;
  readonly labelsGenerated: number | null;
  readonly labelsInserted: number | null;
  readonly labelsDominated: number | null;
  readonly labelsPruned: number | null;
  readonly labelsExpanded: number | null;
  readonly dominanceComparisons: number | null;
  readonly queuePushes: number | null;
  readonly queuePops: number | null;
  readonly maximumLabelsAtNode: number | null;
  readonly truncated: boolean | null;
  readonly vehicleCount: number | null;
  readonly emergencyCount: number | null;
  readonly candidatePairsEvaluated: number | null;
  readonly feasibleCandidatePairs: number | null;
  readonly assignedPairs: number | null;
  readonly solverMatrixDimension: number | null;
  readonly solverOperationCount: number | null;
}

export interface SnapshotMessage {
  readonly type: 'snapshot';
  readonly schemaVersion: number;
  readonly scenarioName: string;
  readonly simulationTimeSec: number;
  readonly simulationStatus: 'RUNNING' | 'PAUSED';
  readonly speedMultiplier: number;
  readonly satellites: readonly SatelliteView[];
  readonly rescueVehicles: readonly VehicleView[];
  readonly debris: readonly DebrisView[];
  readonly emergencies: readonly EmergencyView[];
  readonly missions: readonly MissionView[];
  readonly recentEvents: readonly EventView[];
  readonly metrics: MetricsView;
  readonly graph: GraphView;
  readonly algorithmRuns: readonly AlgorithmRunView[];
  readonly reqId?: number;
}

export interface ReadyMessage {
  readonly type: 'ready';
  readonly schemaVersion: number;
  readonly engineVersion: string;
  readonly capabilities: readonly string[];
}

export interface ErrorMessage {
  readonly type: 'error';
  readonly code: string;
  readonly message: string;
  readonly reqId?: number;
}

/** A discriminated union on `type` -- an unhandled case in a `switch` becomes a build
 * failure, the same reason `API_CONTRACT.md` §8 gives for choosing TypeScript at all. */
export type EngineMessage = SnapshotMessage | ReadyMessage | ErrorMessage;

export function isSnapshotMessage(m: EngineMessage): m is SnapshotMessage {
  return m.type === 'snapshot';
}
export function isReadyMessage(m: EngineMessage): m is ReadyMessage {
  return m.type === 'ready';
}
export function isErrorMessage(m: EngineMessage): m is ErrorMessage {
  return m.type === 'error';
}

/** The exact, narrow command set `cpp/io/CommandProtocol.cpp` recognizes -- nothing else
 * is a valid command because the engine has no handler for anything else (brief §20/§58:
 * Node must not invent controls for functionality the engine does not expose). */
export type EngineCommandName =
  | 'start'
  | 'pause'
  | 'resume'
  | 'reset'
  | 'step'
  | 'advance'
  | 'setSpeed'
  | 'getSnapshot'
  | 'shutdown'
  | 'rescoreParetoFront'
  | 'runAlgorithm';

export interface EngineCommand {
  readonly cmd: EngineCommandName;
  readonly reqId?: number;
  readonly seconds?: number;
  readonly multiplier?: number;
  /** `rescoreParetoFront` only -- Phase 11 brief #50. */
  readonly missionId?: string;
  readonly profile?: string;
  /** `runAlgorithm` only -- Phase 12 brief #16: one of "DIJKSTRA" | "ASTAR" | "PARETO" |
   * "HUNGARIAN". `vehicleId`/`emergencyId` are optional (DIJKSTRA/ASTAR/PARETO only) --
   * the engine picks a real fallback vehicle/target when omitted. */
  readonly algorithm?: string;
  readonly vehicleId?: string;
  readonly emergencyId?: string;
}
