/**
 * TypeScript mirror of the engine's snapshot contract — kept in agreement with
 * `backend/src/ipc/protocol.ts` (and, transitively, `cpp/io/JsonSerializer.cpp`) by hand,
 * the same "no schema-generation tooling" situation those two already document. Every field
 * is `readonly`: the store never mutates engine data (brief §35 — this is exactly why
 * TypeScript was chosen for this project at all, `API_CONTRACT.md` §8).
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

/** One point in a mission's Pareto front -- Phase 10's Optimization Lab, extended by Phase
 * 11's Pareto history (`paretoHistory` below covers every retained planning pass). */
export interface ParetoPointView {
  readonly planId: string;
  readonly cost: ObjectiveVectorView;
  readonly dominated: boolean;
  readonly recommended: boolean;
}

/** One retained planning pass -- Phase 11 brief #52-54. Pass 1 is the mission's original
 * dispatch-time front (multi-point); pass 2+ comes from a later local replan and is
 * honestly narrower (single point: the plan actually adopted, not a full re-derived
 * multi-point front -- see `VISUAL_DESIGN.md`). */
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
  /** Deterministic explanation of the most recent real replan/abort decision. `null` for a
   * mission that has never been replanned -- never fabricated (brief #26/#62: show N/A,
   * don't invent an explanation). */
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

/** One node of the mission graph -- Phase 10's Orbital Network view. Static per run
 * (the graph is built once at scenario load and never rebuilt), included in full on every
 * snapshot for simplicity. */
export interface GraphNodeView {
  readonly id: string;
  readonly kind: 'SATELLITE' | 'RESCUE_VEHICLE';
  readonly entityId: string;
  readonly orbit: OrbitView;
}

/** `cost` is the edge's BUILD-TIME reference sample, not what any live search decision
 * used -- illustrative only (`VISUAL_DESIGN.md` states this for the UI to carry forward). */
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

export interface SimulationSnapshot {
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
}

/** Every entity a scene/inspector might select, tagged by kind so a single selection slot
 * in application state (brief §33) can hold any of them without ambiguity. */
export type SelectableEntity =
  | { readonly kind: 'satellite'; readonly id: string }
  | { readonly kind: 'vehicle'; readonly id: string }
  | { readonly kind: 'mission'; readonly id: string }
  | { readonly kind: 'graphNode'; readonly id: string };

/** One object in a large VISUAL constellation (`GET /visual/constellation`) -- Phase 12
 * brief §5/§19. Deliberately a NARROWER shape than `SatelliteView`/`VehicleView`: no mission
 * graph, no health subsystems, no fuel -- real positions from `cpp/io/VisualConstellation.hpp`,
 * but never run through `SimulationEngine`, so there is no mission/health state to report
 * honestly. */
export interface VisualObjectView {
  readonly id: string;
  readonly name?: string;
  readonly status?: string;
  readonly position: PositionView;
  readonly altitudeKm?: number;
  readonly inclinationDeg?: number;
  /** Real orbital elements -- needed to draw per-plane orbital ribbons
   * (`OrbitalRibbons.tsx`). Without RAAN every ribbon would land in the same plane. */
  readonly raanDeg?: number;
  readonly phaseDeg?: number;
  readonly band?: string;
  readonly hazardRadiusKm?: number;
}

export interface VisualConstellationSnapshot {
  readonly type: 'visualConstellation';
  readonly scenarioName: string;
  readonly timeSec: number;
  readonly generationMs: number;
  readonly satelliteCount: number;
  readonly vehicleCount: number;
  readonly debrisCount: number;
  readonly satellites: readonly VisualObjectView[];
  readonly vehicles: readonly VisualObjectView[];
  readonly debris: readonly VisualObjectView[];
}
