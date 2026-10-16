#include "ViewProjection.hpp"

#include <format>
#include <variant>

#include "../simulation/MissionExecutor.hpp"

namespace orca::io {

using namespace orca;

namespace {

PositionView projectPosition(const Vector3& p) { return PositionView{.x = p.x, .y = p.y, .z = p.z}; }

OrbitView projectOrbit(const Orbit& orbit, Seconds nowSec) {
    return OrbitView{
        .altitudeKm = orbit.altitudeKm(),
        .inclinationDeg = radToDeg(orbit.inclinationRad()),
        .raanDeg = radToDeg(orbit.raanRad()),
        .phaseDeg = radToDeg(orbit.phaseAtRad(nowSec)),
        .band = std::string(toString(orbit.band())),
    };
}

HealthView projectHealth(const Subsystems& health) {
    return HealthView{
        .power = health.get(Subsystem::Power),
        .propulsion = health.get(Subsystem::Propulsion),
        .comms = health.get(Subsystem::Comms),
        .thermal = health.get(Subsystem::Thermal),
    };
}

SatelliteView projectSatellite(const Satellite& sat, Seconds nowSec) {
    return SatelliteView{
        .id = std::string(toString(sat.id())),
        .name = sat.name(),
        .status = std::string(toString(sat.status())),
        .position = projectPosition(sat.positionAtKm(nowSec)),
        .orbit = projectOrbit(sat.orbit(), nowSec),
        .fuelFraction = sat.fuelFraction(),
        .health = projectHealth(sat.health()),
        .activeEmergencyId = sat.activeEmergency()
                                 ? std::optional(std::string(toString(*sat.activeEmergency())))
                                 : std::nullopt,
    };
}

VehicleView projectVehicle(const RescueVehicle& v, Seconds nowSec) {
    return VehicleView{
        .id = std::string(toString(v.id())),
        .name = v.name(),
        .status = std::string(toString(v.status())),
        .position = projectPosition(v.positionAtKm(nowSec)),
        .orbit = projectOrbit(v.orbit(), nowSec),
        .deltaVBudgetKms = v.deltaVBudgetKms(),
        .deltaVRemainingKms = v.deltaVRemainingKms(),
        .fuelFraction = v.fuelFraction(),
        .health = projectHealth(v.health()),
        .activeMissionId = v.activeMission()
                               ? std::optional(std::string(toString(*v.activeMission())))
                               : std::nullopt,
    };
}

DebrisView projectDebris(const DebrisObject& d, Seconds nowSec) {
    return DebrisView{
        .id = std::string(toString(d.id())),
        .position = projectPosition(d.positionAtKm(nowSec)),
        .hazardRadiusKm = d.hazardRadiusKm(),
        .densityFactor = d.densityFactor(),
        .detected = d.isDetected(),
    };
}

EmergencyView projectEmergency(const Emergency& e, Seconds nowSec) {
    return EmergencyView{
        .id = std::string(toString(e.id())),
        .subjectId = std::string(toString(e.subject())),
        .kind = std::string(toString(e.kind())),
        .severity = std::string(toString(e.severity())),
        .state = std::string(toString(e.state())),
        .detectedAtSec = e.detectedAtSec(),
        .deadlineSec = e.deadlineSec(),
        .remainingWindowSec = e.remainingWindowSec(nowSec),
        .urgency = e.urgencyAt(nowSec),
    };
}

std::string entityIdString(const GraphNode& node) {
    return std::visit([](const auto& id) { return std::string(toString(id)); }, node.entity());
}

ObjectiveVectorView projectObjectiveVector(const ObjectiveVector& v) {
    return ObjectiveVectorView{.time = v.time, .fuel = v.fuel, .risk = v.risk, .distance = v.distance};
}

/// `recommended` marks whichever point Phase 11's live re-scoring last selected
/// (`rescoreOverride`, brief §50-51 -- `MissionScorer::selectRecommendedPlan()` applied to
/// this SAME front under a different profile, no new search), if any request has been made
/// for this mission; otherwise falls back to Phase 10's original behaviour -- the point
/// matching the mission's actual adopted route, since "which solution was actually adopted"
/// is a question about the MISSION, not the front in isolation.
std::vector<ParetoPointView> projectParetoFront(const ParetoFront& front,
                                                const std::vector<EdgeId>& adoptedRoute,
                                                const std::optional<PlanId>& rescoreOverride) {
    std::vector<ParetoPointView> points;
    points.reserve(front.solutions.size() + front.dominated.size());
    for (const MissionPlan& plan : front.solutions) {
        const bool recommended =
            rescoreOverride ? plan.id() == *rescoreOverride : plan.route() == adoptedRoute;
        points.push_back(ParetoPointView{
            .planId = std::string(toString(plan.id())),
            .cost = projectObjectiveVector(plan.totalCost()),
            .dominated = false,
            .recommended = recommended,
        });
    }
    for (const MissionPlan& plan : front.dominated) {
        points.push_back(ParetoPointView{
            .planId = std::string(toString(plan.id())),
            .cost = projectObjectiveVector(plan.totalCost()),
            .dominated = true,
            .recommended = false,
        });
    }
    return points;
}

std::vector<MissionLegView> projectLegs(const Mission& mission, const MissionPlan& plan,
                                        const MissionGraph& graph, const TransferModel& model) {
    const std::vector<LegTiming> timings =
        computeLegTimings(graph, model, plan.route(), plan.departureTimeSec());

    std::vector<MissionLegView> legs;
    legs.reserve(plan.route().size());
    for (std::size_t i = 0; i < plan.route().size(); ++i) {
        const GraphEdge& edge = graph.edge(plan.route()[i]);
        const LegTiming& t = timings[i];
        std::string status = "upcoming";
        if (i < mission.currentLeg()) {
            status = "completed";
        } else if (i == mission.currentLeg()) {
            status = "current";
        }
        legs.push_back(MissionLegView{
            .fromEntityId = entityIdString(graph.node(edge.from())),
            .toEntityId = entityIdString(graph.node(edge.to())),
            .fromPosition = projectPosition(graph.node(edge.from()).orbit().positionAtKm(t.departureSec)),
            .toPosition = projectPosition(graph.node(edge.to()).orbit().positionAtKm(t.arrivalSec)),
            .departureTimeSec = t.departureSec,
            .arrivalTimeSec = t.arrivalSec,
            .deltaVKms = t.deltaVKms,
            .status = status,
        });
    }
    return legs;
}

MissionView projectMission(const Mission& mission, const MissionPlan& plan,
                           const MissionGraph& graph, const TransferModel& model,
                           const std::vector<SimulationState::ParetoFrontSnapshot>* history,
                           const std::string* explanation,
                           const SimulationState::ParetoRescore* rescore) {
    const std::optional<PlanId> rescoreOverride =
        rescore != nullptr ? std::optional(rescore->recommendedPlanId) : std::nullopt;

    std::vector<ParetoHistoryEntryView> paretoHistory;
    std::vector<ParetoPointView> currentFront;
    if (history != nullptr && !history->empty()) {
        paretoHistory.reserve(history->size());
        for (const SimulationState::ParetoFrontSnapshot& snapshot : *history) {
            paretoHistory.push_back(ParetoHistoryEntryView{
                .planningPass = snapshot.planningPass,
                .timestampSec = snapshot.timestampSec,
                .points = projectParetoFront(snapshot.front, mission.currentRoute(), rescoreOverride),
            });
        }
        currentFront = paretoHistory.back().points;
    }

    return MissionView{
        .id = std::string(toString(mission.id())),
        .vehicleId = std::string(toString(mission.vehicle())),
        .emergencyId = std::string(toString(mission.emergency())),
        .state = std::string(toString(mission.state())),
        .currentLeg = static_cast<std::uint32_t>(mission.currentLeg()),
        .progressOnLeg = mission.progressOnLeg(),
        .deltaVSpentKms = mission.deltaVSpentKms(),
        .replanCount = mission.replanCount(),
        .startedAtSec = mission.startedAtSec(),
        .completedAtSec = mission.completedAtSec(),
        .legs = projectLegs(mission, plan, graph, model),
        .paretoFront = std::move(currentFront),
        .paretoHistory = std::move(paretoHistory),
        .explanation = explanation != nullptr ? std::optional(*explanation) : std::nullopt,
        .activeRescoreProfile = rescore != nullptr ? std::optional(rescore->profileName) : std::nullopt,
    };
}

GraphView projectGraph(const MissionGraph& graph, Seconds now) {
    GraphView view;
    view.nodes.reserve(graph.nodeCount());
    view.edges.reserve(graph.edgeCount());
    for (const GraphNode& node : graph.nodes()) {
        view.nodes.push_back(GraphNodeView{
            .id = std::string(toString(node.id())),
            .kind = node.kind() == NodeKind::Satellite ? "SATELLITE" : "RESCUE_VEHICLE",
            .entityId = entityIdString(node),
            .orbit = projectOrbit(node.orbit(), now),
        });
        for (const EdgeId edgeId : graph.outgoingEdges(node.id())) {
            const GraphEdge& edge = graph.edge(edgeId);
            view.edges.push_back(GraphEdgeView{
                .id = std::string(toString(edge.id())),
                .from = std::string(toString(edge.from())),
                .to = std::string(toString(edge.to())),
                .cost = projectObjectiveVector(edge.referenceResult().objectives()),
                .feasible = edge.referenceResult().feasible,
            });
        }
    }
    return view;
}

EventView projectEvent(const Event& e) {
    auto optId = [](const auto& opt) -> std::optional<std::string> {
        if (!opt) return std::nullopt;
        return std::string(toString(*opt));
    };
    return EventView{
        .id = std::string(toString(e.id)),
        .kind = std::string(toString(e.kind)),
        .timestampSec = e.timestampSec,
        .satelliteId = optId(e.satellite),
        .vehicleId = optId(e.vehicle),
        .emergencyId = optId(e.emergency),
        .missionId = optId(e.mission),
        .debrisId = optId(e.debris),
        .detail = e.detail,
    };
}

AlgorithmRunView projectAlgorithmRun(const AlgorithmRunRecord& r) {
    AlgorithmRunView view;
    view.algorithm = std::string(toString(r.algorithm));
    view.timestampSec = r.timestampSec;
    view.found = r.found;
    view.arrivalTimeSec = r.arrivalTimeSec;

    if (r.search) {
        view.executionTimeMs = r.search->executionTimeMs;
        view.nodesExpanded = r.search->nodesExpanded;
        view.edgesEvaluated = r.search->edgesEvaluated;
        view.relaxationsSucceeded = r.search->relaxationsSucceeded;
        view.relaxationsRejected = r.search->relaxationsRejected;
        view.priorityQueuePushes = r.search->priorityQueuePushes;
        view.priorityQueuePops = r.search->priorityQueuePops;
        view.staleEntriesDiscarded = r.search->staleEntriesDiscarded;
        if (r.algorithm == AlgorithmKind::AStar) {
            view.heuristicEvaluations = r.search->heuristicEvaluations;
        }
    }
    if (r.pareto) {
        view.executionTimeMs = r.pareto->executionTimeMs;
        view.labelsGenerated = r.pareto->labelsGenerated;
        view.labelsInserted = r.pareto->labelsInserted;
        view.labelsDominated = r.pareto->labelsDominated;
        view.labelsPruned = r.pareto->labelsPruned;
        view.labelsExpanded = r.pareto->labelsExpanded;
        view.dominanceComparisons = r.pareto->dominanceComparisons;
        view.queuePushes = r.pareto->queuePushes;
        view.queuePops = r.pareto->queuePops;
        view.maximumLabelsAtNode = r.pareto->maximumLabelsAtNode;
        view.truncated = r.pareto->truncated;
    }
    if (r.assignment) {
        view.executionTimeMs = r.assignment->executionTimeMs;
        view.vehicleCount = r.assignment->vehicleCount;
        view.emergencyCount = r.assignment->emergencyCount;
        view.candidatePairsEvaluated = r.assignment->candidatePairsEvaluated;
        view.feasibleCandidatePairs = r.assignment->feasibleCandidatePairs;
        view.assignedPairs = r.assignment->assignedPairs;
        view.solverMatrixDimension = static_cast<int>(r.assignment->solverMatrixDimension);
        view.solverOperationCount = r.assignment->solverOperationCount;
    }
    return view;
}

MetricsView projectMetrics(const SimulationMetrics& m) {
    return MetricsView{
        .ticks = m.ticks,
        .eventsProcessed = m.eventsProcessed,
        .missionsUpdated = m.missionsUpdated,
        .replansTriggered = m.replansTriggered,
        .replansCompleted = m.replansCompleted,
        .missionSuccesses = m.missionSuccesses,
        .missionFailures = m.missionFailures,
        .missionAborts = m.missionAborts,
    };
}

}  // namespace

SimulationSnapshot buildSnapshot(const SimulationEngine& engine) {
    const SimulationState& state = engine.state();
    const Seconds now = engine.clock().currentTimeSec();

    SimulationSnapshot snapshot;
    snapshot.scenarioName = engine.scenario().name();
    snapshot.simulationTimeSec = now;
    snapshot.simulationStatus = engine.clock().isRunning() ? "RUNNING" : "PAUSED";
    snapshot.speedMultiplier = engine.clock().speedMultiplier();

    for (const auto& [rawId, sat] : state.satellites) {
        snapshot.satellites.push_back(projectSatellite(sat, now));
    }
    for (const auto& [rawId, v] : state.vehicles) {
        snapshot.rescueVehicles.push_back(projectVehicle(v, now));
    }
    for (const auto& [rawId, d] : state.debris) {
        snapshot.debris.push_back(projectDebris(d, now));
    }
    for (const auto& [rawId, e] : state.emergencies) {
        snapshot.emergencies.push_back(projectEmergency(e, now));
    }
    for (const auto& [rawId, mission] : state.missions) {
        const MissionPlan* plan = state.findMissionPlan(mission.id());
        if (plan == nullptr) {
            continue;  // a terminal mission whose plan was never recorded is not expected,
                      // but skipping rather than crashing keeps a snapshot always producible
        }
        const auto historyIt = state.missionParetoHistory.find(mission.id().value());
        const std::vector<SimulationState::ParetoFrontSnapshot>* history =
            historyIt != state.missionParetoHistory.end() ? &historyIt->second : nullptr;
        const auto explIt = state.missionExplanations.find(mission.id().value());
        const std::string* explanation = explIt != state.missionExplanations.end() ? &explIt->second : nullptr;
        const auto rescoreIt = state.missionRescoring.find(mission.id().value());
        const SimulationState::ParetoRescore* rescore =
            rescoreIt != state.missionRescoring.end() ? &rescoreIt->second : nullptr;
        snapshot.missions.push_back(projectMission(mission, *plan, engine.graph(),
                                                    engine.modelForDispatch(), history, explanation, rescore));
    }
    for (const Event& e : state.recentEvents) {
        snapshot.recentEvents.push_back(projectEvent(e));
    }
    snapshot.metrics = projectMetrics(state.metrics);
    snapshot.graph = projectGraph(engine.graph(), now);
    for (const AlgorithmRunRecord& r : state.algorithmRuns) {
        snapshot.algorithmRuns.push_back(projectAlgorithmRun(r));
    }

    return snapshot;
}

}  // namespace orca::io
