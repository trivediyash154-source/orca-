#pragma once

/// @file SimulationView.hpp
/// External view/projection types — Phase 9 brief §7-9. Flattened, semantic, JSON-shaped
/// structs, deliberately narrower than the internal domain types they are built from
/// (`ViewProjection.cpp`): no `unordered_map`, no internal IDs beyond their string form, no
/// algorithm bookkeeping. This is the boundary `ARCHITECTURE.md` §6.1's rule
/// ("the frontend never computes a value the engine could compute") depends on — every
/// field here is already-computed engine output, never a value a consumer would need to
/// derive.
///
/// Every field is real, present-day engine state. Nothing here is invented to fill out a
/// richer-looking contract than the engine actually supports (brief §10's explicit
/// instruction) — `SIMULATION_ENGINE.md` and `DATA_MODEL.md` are the source of truth this
/// file was built from.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace orca::io {

/// ECI position in kilometres — the same frame and units `Vector3`/`Orbit::positionAtKm()`
/// already use internally (`ARCHITECTURE.md`'s "the frontend never derives" rule extends to
/// units and frame: this is a direct passthrough, not a conversion the engine owes a second
/// opinion on).
struct PositionView {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

/// Orbital elements plus the one time-dependent quantity (`phaseDeg`) a consumer needs to
/// plot a marker without re-deriving it. Angles cross this boundary in degrees
/// (`API_CONTRACT.md` §1.2); radians never leave the engine.
struct OrbitView {
    double altitudeKm = 0.0;
    double inclinationDeg = 0.0;
    double raanDeg = 0.0;
    double phaseDeg = 0.0;  ///< at the snapshot's own `simulationTimeSec`, not epoch
    std::string band;       ///< "LEO" | "MEO" | "GEO"
};

struct HealthView {
    double power = 1.0;
    double propulsion = 1.0;
    double comms = 1.0;
    double thermal = 1.0;
};

struct SatelliteView {
    std::string id;
    std::string name;
    std::string status;
    PositionView position;
    OrbitView orbit;
    double fuelFraction = 1.0;
    HealthView health;
    std::optional<std::string> activeEmergencyId;
};

struct VehicleView {
    std::string id;
    std::string name;
    std::string status;
    PositionView position;
    OrbitView orbit;
    double deltaVBudgetKms = 0.0;
    double deltaVRemainingKms = 0.0;
    double fuelFraction = 1.0;
    HealthView health;
    std::optional<std::string> activeMissionId;
};

struct DebrisView {
    std::string id;
    PositionView position;
    double hazardRadiusKm = 0.0;
    double densityFactor = 0.0;
    bool detected = false;
};

struct EmergencyView {
    std::string id;
    std::string subjectId;
    std::string kind;
    std::string severity;
    std::string state;
    double detectedAtSec = 0.0;
    double deadlineSec = 0.0;
    double remainingWindowSec = 0.0;  ///< engine-computed at snapshot time -- never
                                      ///< `deadlineSec - simulationTimeSec` client-side
                                      ///< (`API_CONTRACT.md` §4.2's own stated reason)
    double urgency = 0.0;             ///< [0,1], `Emergency::urgencyAt()`
};

/// One leg of a mission's route, endpoint-only (brief §13: "must not create Three.js
/// geometry in the C++ serializer," and exact mid-leg position is not modelled at all —
/// `DYNAMIC_REPLANNING.md` §13.2, restated in `SIMULATION_ENGINE.md` §17 — so a leg's
/// *interior* is honestly left to the frontend to render as a simple connector between two
/// real points, never as fabricated intermediate physics).
struct MissionLegView {
    std::string fromEntityId;
    std::string toEntityId;
    PositionView fromPosition;
    PositionView toPosition;
    double departureTimeSec = 0.0;
    double arrivalTimeSec = 0.0;
    double deltaVKms = 0.0;
    std::string status;  ///< "completed" | "current" | "upcoming"
};

/// Direct mirror of `ObjectiveVector` (`core/ObjectiveVector.hpp`) — the same four numbers
/// every plan/edge cost is expressed in throughout the engine, unchanged at this boundary.
struct ObjectiveVectorView {
    double time = 0.0;      // seconds
    double fuel = 0.0;      // fraction of the vehicle's own delta-v budget, [0,1]
    double risk = 0.0;      // [0,1]
    double distance = 0.0;  // km
};

/// One point in a mission's Pareto front — Phase 10's Optimization Lab view
/// (`SimulationState::missionParetoHistory`'s own comment has the capture/limitation
/// details this type surfaces to the frontend as-is).
struct ParetoPointView {
    std::string planId;
    ObjectiveVectorView cost;
    bool dominated = false;
    bool recommended = false;
};

/// One retained planning pass -- Phase 11 brief §52-54's Pareto history. Pass 1 is the
/// mission's original dispatch-time front (multi-point, `points.size()` typically > 1);
/// pass 2+ comes from a later local replan and is honestly narrower (`points.size() == 1`,
/// the single plan actually adopted -- `SimulationState::ParetoFrontSnapshot`'s own comment
/// has the full reasoning).
struct ParetoHistoryEntryView {
    int planningPass = 1;
    double timestampSec = 0.0;
    std::vector<ParetoPointView> points;
};

struct MissionView {
    std::string id;
    std::string vehicleId;
    std::string emergencyId;
    std::string state;
    std::uint32_t currentLeg = 0;
    double progressOnLeg = 0.0;
    double deltaVSpentKms = 0.0;
    int replanCount = 0;
    double startedAtSec = 0.0;
    std::optional<double> completedAtSec;
    std::vector<MissionLegView> legs;

    /// The CURRENT (most recently captured) planning pass's front -- empty only if none was
    /// ever captured (should not happen for a dispatched mission; never fabricated if
    /// absent). Kept for backward compatibility with Phase 10 consumers; `paretoHistory`
    /// below is the complete record across every planning pass.
    std::vector<ParetoPointView> paretoFront;

    /// Every retained planning pass, oldest first -- Phase 11 brief §52-54: "keep history...
    /// do not silently overwrite the original."
    std::vector<ParetoHistoryEntryView> paretoHistory;

    /// The profile name of the most recent live re-scoring request for this mission
    /// (Phase 11 brief §50), if any -- `std::nullopt` when `paretoFront`'s `recommended`
    /// point still reflects the mission's actual adopted route rather than a what-if.
    std::optional<std::string> activeRescoreProfile;

    /// The deterministic explanation of the most recent real replan/abort decision, if any
    /// (`SimulationState::missionExplanations`). `std::nullopt`, not an empty string, for a
    /// mission that has never been replanned -- brief §26/§62: absence is represented
    /// honestly, never as fabricated text.
    std::optional<std::string> explanation;
};

/// One already-fired event from `SimulationState::recentEvents` (Phase 9 addition to the
/// Phase 8 engine, `SIMULATION_ENGINE.md`'s own event-queue design extended with a bounded
/// history log for exactly this consumer).
struct EventView {
    std::string id;
    std::string kind;
    double timestampSec = 0.0;
    std::optional<std::string> satelliteId;
    std::optional<std::string> vehicleId;
    std::optional<std::string> emergencyId;
    std::optional<std::string> missionId;
    std::optional<std::string> debrisId;
    std::string detail;
};

/// Direct mirror of `SimulationMetrics` (`SimulationState.hpp`) — no invented dashboard
/// statistics (brief §15).
struct MetricsView {
    long ticks = 0;
    long eventsProcessed = 0;
    long missionsUpdated = 0;
    long replansTriggered = 0;
    long replansCompleted = 0;
    long missionSuccesses = 0;
    long missionFailures = 0;
    long missionAborts = 0;
};

/// One node of the mission graph -- Phase 10's Orbital Network view. The graph is built
/// once, at scenario load, from `Scenario`'s static satellites/vehicles
/// (`SIMULATION_ENGINE.md` §5: `SimulationEngine::graph_` is never rebuilt), so this is the
/// same graph for every snapshot of a run -- included in full each time for simplicity
/// (`INTEGRATION_ARCHITECTURE.md`'s "narrow, real, no premature optimization" pattern),
/// not because it changes.
struct GraphNodeView {
    std::string id;
    std::string kind;      ///< "SATELLITE" | "RESCUE_VEHICLE"
    std::string entityId;  ///< the underlying SatelliteId/VehicleId, e.g. "SAT-101"
    OrbitView orbit;
};

/// One edge's cost is its BUILD-TIME reference sample (`GraphEdge::referenceResult()`),
/// evaluated once when the graph was constructed at a fixed reference departure time --
/// explicitly NOT what any live search decision used (`GraphEdge.hpp`'s own file comment:
/// the search re-evaluates every edge live, at the actual candidate departure time, and
/// never reads this cached sample). Presented here as an illustrative, typical cost for
/// the Orbital Network view, not as the number that drove any specific mission's plan --
/// `VISUAL_DESIGN.md` states this distinction explicitly for the UI to carry forward.
struct GraphEdgeView {
    std::string id;
    std::string from;
    std::string to;
    ObjectiveVectorView cost;
    bool feasible = false;
};

struct GraphView {
    std::vector<GraphNodeView> nodes;
    std::vector<GraphEdgeView> edges;
};

/// Phase 11's Algorithm Lab telemetry — a flattened projection of whichever
/// `SearchMetrics`/`ParetoSearchMetrics`/`AssignmentMetrics` the run actually produced
/// (`cpp/analytics/AlgorithmRun.hpp`). Every field a `std::nullopt` (serialized `null`) is
/// genuinely not applicable to that algorithm, never a zero standing in for "unknown"
/// (brief §15's "only include metrics that actually exist for that algorithm").
struct AlgorithmRunView {
    std::string algorithm;  ///< "DIJKSTRA" | "ASTAR" | "PARETO" | "HUNGARIAN" | "GREEDY"
    double timestampSec = 0.0;
    bool found = true;
    std::optional<double> arrivalTimeSec;
    double executionTimeMs = 0.0;

    // Dijkstra / A*
    std::optional<int> nodesExpanded;
    std::optional<int> edgesEvaluated;
    std::optional<int> relaxationsSucceeded;
    std::optional<int> relaxationsRejected;
    std::optional<int> priorityQueuePushes;
    std::optional<int> priorityQueuePops;
    std::optional<int> staleEntriesDiscarded;
    std::optional<int> heuristicEvaluations;  ///< present (possibly 0) for A*, absent for Dijkstra

    // Pareto
    std::optional<int> labelsGenerated;
    std::optional<int> labelsInserted;
    std::optional<int> labelsDominated;
    std::optional<int> labelsPruned;
    std::optional<int> labelsExpanded;
    std::optional<int> dominanceComparisons;
    std::optional<int> queuePushes;
    std::optional<int> queuePops;
    std::optional<int> maximumLabelsAtNode;
    std::optional<bool> truncated;

    // Hungarian / Greedy
    std::optional<int> vehicleCount;
    std::optional<int> emergencyCount;
    std::optional<int> candidatePairsEvaluated;
    std::optional<int> feasibleCandidatePairs;
    std::optional<int> assignedPairs;
    std::optional<int> solverMatrixDimension;
    std::optional<long> solverOperationCount;
};

/// The complete external projection of one `SimulationEngine`'s state at one instant.
struct SimulationSnapshot {
    int schemaVersion = 1;
    std::string scenarioName;
    double simulationTimeSec = 0.0;
    std::string simulationStatus;  ///< "RUNNING" | "PAUSED" -- directly
                                   ///< `SimulationClock::isRunning()`; the engine has no
                                   ///< richer run-state machine than this to report
                                   ///< honestly (`INTEGRATION_ARCHITECTURE.md` notes this).
    double speedMultiplier = 1.0;
    std::vector<SatelliteView> satellites;
    std::vector<VehicleView> rescueVehicles;
    std::vector<DebrisView> debris;
    std::vector<EmergencyView> emergencies;
    std::vector<MissionView> missions;
    std::vector<EventView> recentEvents;
    MetricsView metrics;
    GraphView graph;
    std::vector<AlgorithmRunView> algorithmRuns;
};

}  // namespace orca::io
