#include "EventDispatcher.hpp"

#include <cmath>
#include <format>

#include "../algorithms/AStar.hpp"
#include "../algorithms/Dijkstra.hpp"
#include "../algorithms/ParetoSearch.hpp"
#include "../analytics/AlgorithmRun.hpp"
#include "../core/Validation.hpp"
#include "../optimization/CandidateGeneration.hpp"
#include "../planning/GlobalReplanner.hpp"
#include "../planning/MissionValidity.hpp"
#include "../planning/ReplanDecision.hpp"

namespace orca {

namespace {

/// Walks a satellite's status one valid transition at a time until it reaches `Failed` --
/// `Satellite::setStatus()` only permits single-step transitions (`Satellite.hpp`'s own
/// state machine), so a `SatelliteFailure` event drives it through whichever intermediate
/// steps are needed from its CURRENT status, not assumed to start `Nominal`.
void forceSatelliteFailed(Satellite& satellite) {
    static constexpr SatelliteStatus kPath[] = {SatelliteStatus::Warning,
                                                SatelliteStatus::Critical,
                                                SatelliteStatus::Failed};
    for (SatelliteStatus next : kPath) {
        if (satellite.canTransitionTo(next)) {
            satellite.setStatus(next);
        }
    }
}

/// Applies a `ReplanDecision` to the mission/vehicle/emergency it concerns -- the ONLY
/// place any event handler mutates `Mission`/`RescueVehicle`/`Emergency` state in response
/// to a replan outcome, so the policy (brief §39: "use mission/domain APIs... do not
/// mutate arbitrary fields directly") lives in exactly one function.
void applyReplanDecision(SimulationEngine& engine, Mission& mission, RescueVehicle& vehicle,
                         Emergency& emergency, const ReplanDecision& decision) {
    SimulationState& state = engine.stateMutable();
    switch (decision.decision) {
        case ReplanDecisionType::Continue:
            break;
        case ReplanDecisionType::Replan:
            // `Mission`'s own state table (Mission.hpp) defines InTransit -> Replanning ->
            // InTransit for exactly this moment, but nothing previously drove it -- adopting
            // a route left `state()` reading InTransit throughout, so the Replanning value
            // was declared and reachable-by-table yet never actually entered. Walking
            // through it here (both transitions legal per `isValidTransition`, verified
            // against Mission.cpp) costs nothing and means `state()` genuinely reflects
            // Mission.hpp's documented machine rather than only approximating it.
            mission.setState(MissionState::Replanning);
            mission.adoptRoute(*decision.newPlan);
            mission.setState(MissionState::InTransit);
            state.missionPlans.insert_or_assign(mission.id().value(), *decision.newPlan);
            ++state.metrics.replansCompleted;
            // Phase 10's "why ORCA chose it" panel -- the deterministic, table-driven
            // explanation of this real decision, not generated prose (`ReplanDecision.hpp`'s
            // own file comment).
            state.missionExplanations.insert_or_assign(mission.id().value(), explain(decision));
            // Phase 11 brief §52-54's Pareto history: append a new planning pass rather
            // than leaving the mission's history stuck at pass 1 -- honestly a single-plan
            // front (`SimulationState.hpp`'s own comment on `missionParetoHistory` explains
            // why a full multi-point front is not re-derived here).
            {
                std::vector<SimulationState::ParetoFrontSnapshot>& history =
                    state.missionParetoHistory[mission.id().value()];
                ParetoFront singlePlanFront;
                singlePlanFront.solutions.push_back(*decision.newPlan);
                singlePlanFront.ideal = decision.newPlan->totalCost();
                singlePlanFront.nadir = decision.newPlan->totalCost();
                history.push_back(SimulationState::ParetoFrontSnapshot{
                    .planningPass = static_cast<int>(history.size()) + 1,
                    .timestampSec = engine.currentTimeSec(),
                    .front = std::move(singlePlanFront),
                });
            }
            engine.eventQueueMutable().schedule(Event{
                .id = EventId(0),
                .timestampSec = engine.currentTimeSec(),
                .kind = EventKind::MissionReplan,
                .mission = mission.id(),
                .detail = std::format("replanned: {}", toString(decision.reason)),
            });
            break;
        case ReplanDecisionType::Abort:
            mission.setState(MissionState::Aborted);
            ++state.metrics.missionAborts;
            state.missionExplanations.insert_or_assign(mission.id().value(), explain(decision));
            vehicle.clearMission();
            if (vehicle.canTransitionTo(VehicleStatus::Returning)) {
                vehicle.setStatus(VehicleStatus::Returning);
            }
            if (vehicle.canTransitionTo(VehicleStatus::Idle)) {
                vehicle.setStatus(VehicleStatus::Idle);
            }
            // Reassignment returns an emergency to Pending (Emergency.hpp's own state
            // table) so a later global replan can pick it back up with a different
            // vehicle -- an aborted mission does not mean the casualty is abandoned.
            if (emergency.canTransitionTo(EmergencyState::Pending)) {
                emergency.setState(EmergencyState::Pending);
            }
            engine.eventQueueMutable().schedule(Event{
                .id = EventId(0),
                .timestampSec = engine.currentTimeSec(),
                .kind = EventKind::MissionFailure,
                .mission = mission.id(),
                .detail = std::format("aborted: {}", toString(decision.abortReason)),
            });
            break;
        case ReplanDecisionType::Complete:
            // The emergency was already Resolved by the time evaluate() ran (a rare race
            // -- e.g. two events resolving the same casualty in one tick); nothing further
            // to do here, Rendezvous/RescueComplete handling already covers the normal
            // path.
            break;
    }
}

/// Runs Dijkstra, A*, and Pareto search on the SAME vehicle/emergency pair a real
/// assignment just used, purely for Algorithm Lab telemetry (brief §14-19) -- none of the
/// three results affect the decision already made; they exist only for side-by-side
/// comparison, matching this project's established Phase 4/5 benchmarking culture. Reuses
/// `SearchConstraints` shaped the same way `CandidateGeneration.cpp`'s own
/// `makeSearchConstraints()` builds them, so the comparison is apples-to-apples with what
/// the real Pareto search (via `GlobalReplanner`) actually searched under.
void recordComparisonAlgorithmRuns(SimulationState& state, const MissionGraph& graph,
                                   const TransferModel& model, const EntityNodeIndex& index,
                                   const RescueVehicle& vehicle, const Emergency& emergency,
                                   Seconds nowSec) {
    const auto vehicleNodeIt = index.vehicleNodes.find(vehicle.id().value());
    const auto emergencyNodeIt = index.satelliteNodes.find(emergency.subject().value());
    if (vehicleNodeIt == index.vehicleNodes.end() || emergencyNodeIt == index.satelliteNodes.end()) {
        return;  // no graph node for this pair (should not happen for a just-dispatched
                // assignment, but telemetry must never be allowed to throw)
    }
    const NodeId source = vehicleNodeIt->second;
    const NodeId target = emergencyNodeIt->second;

    SearchConstraints constraints;
    constraints.vehicleDeltaVBudgetKms = vehicle.deltaVRemainingKms();
    constraints.maxWaitSec = 1.0e13;
    constraints.latestArrivalSec = emergency.deadlineSec();

    const PathResult dijkstraResult =
        findEarliestArrival(graph, model, source, target, nowSec, constraints);
    state.recordAlgorithmRun(AlgorithmRunRecord{
        .algorithm = AlgorithmKind::Dijkstra,
        .timestampSec = nowSec,
        .found = dijkstraResult.isFound(),
        .arrivalTimeSec = dijkstraResult.isFound() ? std::optional(dijkstraResult.arrivalTimeSec())
                                                    : std::nullopt,
        .search = dijkstraResult.metrics(),
    });

    const PathResult aStarResult =
        findEarliestArrivalAStar(graph, model, source, target, nowSec, constraints);
    state.recordAlgorithmRun(AlgorithmRunRecord{
        .algorithm = AlgorithmKind::AStar,
        .timestampSec = nowSec,
        .found = aStarResult.isFound(),
        .arrivalTimeSec =
            aStarResult.isFound() ? std::optional(aStarResult.arrivalTimeSec()) : std::nullopt,
        .search = aStarResult.metrics(),
    });

    const ParetoSearchResult paretoResult =
        findParetoRoutes(graph, model, source, target, nowSec, constraints);
    state.recordAlgorithmRun(AlgorithmRunRecord{
        .algorithm = AlgorithmKind::Pareto,
        .timestampSec = nowSec,
        .found = !paretoResult.isEmpty(),
        .arrivalTimeSec = !paretoResult.isEmpty()
                              ? std::optional(paretoResult.solutions.front().arrivalTimeSec)
                              : std::nullopt,
        .pareto = paretoResult.metrics,
    });
}

/// Runs a global replan over every currently available vehicle and pending emergency, and
/// materialises a real `Mission` for each resulting assignment -- brief §38's "use the
/// existing Phase 6 assignment pipeline," applied end to end (vehicle/emergency state
/// transitions, `Mission` construction, `MissionStart` events).
void runGlobalReplanAndDispatchMissions(SimulationEngine& engine) {
    SimulationState& state = engine.stateMutable();
    const std::vector<VehicleId> availableIds = state.availableVehicleIds();
    const std::vector<EmergencyId> pendingIds = state.pendingEmergencyIds();
    if (availableIds.empty() || pendingIds.empty()) {
        return;
    }

    std::vector<RescueVehicle> available;
    available.reserve(availableIds.size());
    for (VehicleId id : availableIds) available.push_back(*state.findVehicle(id));

    std::vector<Emergency> pending;
    pending.reserve(pendingIds.size());
    for (EmergencyId id : pendingIds) pending.push_back(*state.findEmergency(id));

    const AssignmentResult result =
        evaluateGlobalReplan(engine.graphForDispatch(), engine.modelForDispatch(), available,
                            pending, engine.currentTimeSec(), engine.config().profile,
                            state.nextPlanId);
    ++state.metrics.replansTriggered;

    // The REAL assignment solver's own metrics -- Hungarian, `evaluateGlobalReplan()`'s
    // hardcoded choice (`GlobalReplanner.cpp`) -- captured once per dispatch call, not
    // recomputed (brief §17: higher layers store what algorithms already produced).
    if (!result.assignments.empty()) {
        state.recordAlgorithmRun(AlgorithmRunRecord{
            .algorithm = AlgorithmKind::Hungarian,
            .timestampSec = engine.currentTimeSec(),
            .assignment = result.metrics,
        });
    }

    for (const Assignment& assignment : result.assignments) {
        const MissionId missionId(state.nextMissionId++);
        Mission mission(missionId, assignment.missionPlan, engine.currentTimeSec());
        mission.setState(MissionState::Launched);
        mission.setState(MissionState::InTransit);

        RescueVehicle* vehicle = state.findVehicle(assignment.vehicle);
        Emergency* emergency = state.findEmergency(assignment.emergency);

        // Phase 10's Optimization Lab view: recompute this assignment's own Pareto front
        // for display -- deterministic under the SAME graph/model/vehicle/emergency/now
        // `evaluateGlobalReplan()` just used, so it is identical to the front that actually
        // produced this assignment in every respect except PlanId numbering (a fresh local
        // counter, since these plans are never committed -- only ever read for display).
        // Not threaded through `GlobalReplanner`'s tested public API by reference, to avoid
        // widening that Phase 6/7 surface for a Phase 10 view-only need
        // (`SimulationState.hpp`'s own comment on `missionParetoHistory` has the full
        // reasoning, including the known "does not refresh after a later replan" limit).
        // Captured BEFORE the vehicle/emergency mutations below: `generateCandidateFront()`
        // requires an available vehicle and a pending emergency to produce a feasible
        // front, and both become false the instant this assignment is applied.
        {
            PlanId displayPlanId(1);
            const EntityNodeIndex index = buildEntityNodeIndex(engine.graphForDispatch());
            const CandidateFront displayFront = generateCandidateFront(
                engine.graphForDispatch(), engine.modelForDispatch(), index, *vehicle, *emergency,
                engine.currentTimeSec(), displayPlanId);
            state.missionParetoHistory[missionId.value()].push_back(SimulationState::ParetoFrontSnapshot{
                .planningPass = 1,
                .timestampSec = engine.currentTimeSec(),
                .front = displayFront.front,
            });

            recordComparisonAlgorithmRuns(state, engine.graphForDispatch(), engine.modelForDispatch(),
                                          index, *vehicle, *emergency, engine.currentTimeSec());
        }

        vehicle->assignMission(missionId);
        vehicle->setStatus(VehicleStatus::Assigned);
        vehicle->setStatus(VehicleStatus::Transferring);
        emergency->setState(EmergencyState::Assigned);

        state.missionPlans.emplace(missionId.value(), assignment.missionPlan);
        state.missions.emplace(missionId.value(), std::move(mission));

        engine.eventQueueMutable().schedule(Event{
            .id = EventId(0),
            .timestampSec = engine.currentTimeSec(),
            .kind = EventKind::MissionStart,
            .vehicle = assignment.vehicle,
            .emergency = assignment.emergency,
            .mission = missionId,
            .detail = "global replan assignment",
        });
    }
}

/// Brief §42-43's deliberately simple debris-proximity heuristic: a route leg is
/// "threatened" when either endpoint's orbit has a semi-major axis within
/// `config().debrisProximityThresholdKm` of the debris object's own -- not conjunction
/// analysis, a documented simplification (`SimulationEngine.hpp`'s own config comment).
[[nodiscard]] std::optional<EdgeId> findThreatenedEdge(const SimulationEngine& engine,
                                                       const Mission& mission,
                                                       const DebrisObject& debris) {
    const std::size_t startLeg =
        remainingLegStartIndex(mission.currentLeg(), mission.progressOnLeg());
    const Kilometres debrisRadius = debris.orbit().semiMajorAxisKm();
    for (std::size_t i = startLeg; i < mission.currentRoute().size(); ++i) {
        const EdgeId edgeId = mission.currentRoute()[i];
        const GraphEdge& edge = engine.graphForDispatch().edge(edgeId);
        const Kilometres fromRadius =
            engine.graphForDispatch().node(edge.from()).orbit().semiMajorAxisKm();
        const Kilometres toRadius =
            engine.graphForDispatch().node(edge.to()).orbit().semiMajorAxisKm();
        if (std::fabs(fromRadius - debrisRadius) <= engine.config().debrisProximityThresholdKm ||
            std::fabs(toRadius - debrisRadius) <= engine.config().debrisProximityThresholdKm) {
            return edgeId;
        }
    }
    return std::nullopt;
}

void handleSatelliteFailure(SimulationEngine& engine, const Event& event) {
    SimulationState& state = engine.stateMutable();
    Satellite* satellite = state.findSatellite(*event.satellite);
    requireThat(satellite != nullptr,
                "handleSatelliteFailure: event names a satellite not in this state");
    if (satellite->requiresRescue()) {
        return;  // already failed -- a duplicate event is a no-op, not an error
    }
    forceSatelliteFailed(*satellite);

    const EmergencyId emergencyId(state.nextEmergencyId++);
    Emergency emergency(emergencyId, satellite->id(), event.emergencyKind.value(),
                       event.severity.value(), engine.currentTimeSec(),
                       event.survivalWindowSec.value());
    satellite->assignEmergency(emergencyId);
    state.emergencies.emplace(emergencyId.value(), std::move(emergency));

    runGlobalReplanAndDispatchMissions(engine);
}

void handleDebrisAlert(SimulationEngine& engine, const Event& event) {
    SimulationState& state = engine.stateMutable();
    DebrisObject* debrisObj = state.findDebris(*event.debris);
    requireThat(debrisObj != nullptr,
                "handleDebrisAlert: event names a debris object not in this state");
    debrisObj->markDetected();

    for (MissionId missionId : state.activeMissionIds()) {
        Mission* mission = state.findMission(missionId);
        if (mission->state() != MissionState::InTransit) {
            continue;  // only an in-transit mission has a remaining route to threaten
        }
        const std::optional<EdgeId> threatened = findThreatenedEdge(engine, *mission, *debrisObj);
        if (!threatened.has_value()) {
            continue;
        }
        RescueVehicle* vehicle = state.findVehicle(mission->vehicle());
        Emergency* emergency = state.findEmergency(mission->emergency());
        const MissionChange trigger{
            .kind = ChangeKind::DebrisDetected,
            .atSec = engine.currentTimeSec(),
            .invalidatedEdge = threatened,
            .detail = std::format("debris {} threatens the current route", toString(debrisObj->id())),
        };
        const ReplanDecision decision = engine.replannerMutable().evaluate(
            *mission, *vehicle, *emergency, engine.graphForDispatch(), engine.modelForDispatch(),
            engine.currentTimeSec(), trigger, engine.config().profile, state.nextPlanId);
        applyReplanDecision(engine, *mission, *vehicle, *emergency, decision);
    }
}

void handleVehicleDegradation(SimulationEngine& engine, const Event& event, ChangeKind kind,
                              double magnitudeFraction) {
    SimulationState& state = engine.stateMutable();
    RescueVehicle* vehicle = state.findVehicle(*event.vehicle);
    requireThat(vehicle != nullptr,
                "handleVehicleDegradation: event names a vehicle not in this state");
    if (!vehicle->activeMission().has_value()) {
        return;  // idle vehicle degrading has no mission to reconsider
    }
    Mission* mission = state.findMission(*vehicle->activeMission());
    if (mission == nullptr || mission->state() != MissionState::InTransit) {
        return;
    }
    Emergency* emergency = state.findEmergency(mission->emergency());
    const MissionChange trigger{
        .kind = kind,
        .atSec = engine.currentTimeSec(),
        .magnitudeFraction = magnitudeFraction,
        .detail = event.detail,
    };
    const ReplanDecision decision = engine.replannerMutable().evaluate(
        *mission, *vehicle, *emergency, engine.graphForDispatch(), engine.modelForDispatch(),
        engine.currentTimeSec(), trigger, engine.config().profile, state.nextPlanId);
    applyReplanDecision(engine, *mission, *vehicle, *emergency, decision);
}

void handleRendezvous(SimulationEngine& engine, const Event& event) {
    SimulationState& state = engine.stateMutable();
    Mission* mission = state.findMission(*event.mission);
    requireThat(mission != nullptr, "handleRendezvous: event names a mission not in this state");
    mission->setState(MissionState::Rendezvous);
    mission->setState(MissionState::Success);
    mission->recordCompletion(engine.currentTimeSec());
    ++state.metrics.missionSuccesses;

    RescueVehicle* vehicle = state.findVehicle(mission->vehicle());
    Emergency* emergency = state.findEmergency(mission->emergency());
    Satellite* satellite = state.findSatellite(emergency->subject());

    vehicle->clearMission();
    if (vehicle->canTransitionTo(VehicleStatus::OnStation)) vehicle->setStatus(VehicleStatus::OnStation);
    if (vehicle->canTransitionTo(VehicleStatus::Returning)) vehicle->setStatus(VehicleStatus::Returning);
    if (vehicle->canTransitionTo(VehicleStatus::Idle)) vehicle->setStatus(VehicleStatus::Idle);

    if (emergency->canTransitionTo(EmergencyState::InProgress)) {
        emergency->setState(EmergencyState::InProgress);
    }
    if (emergency->canTransitionTo(EmergencyState::Resolved)) {
        emergency->setState(EmergencyState::Resolved);
    }
    if (satellite != nullptr) {
        satellite->clearEmergency();
        if (satellite->canTransitionTo(SatelliteStatus::Rescued)) {
            satellite->setStatus(SatelliteStatus::Rescued);
        }
    }

    engine.eventQueueMutable().schedule(Event{
        .id = EventId(0),
        .timestampSec = engine.currentTimeSec(),
        .kind = EventKind::RescueComplete,
        .vehicle = mission->vehicle(),
        .emergency = mission->emergency(),
        .mission = mission->id(),
        .detail = "rescue complete",
    });

    // A vehicle freed by completing a rescue may immediately be useful for any emergency
    // still waiting -- the same "idle vehicle becomes available -> reconsider the fleet"
    // logic a preemption's freed vehicle would also need (DYNAMIC_REPLANNING.md §5).
    runGlobalReplanAndDispatchMissions(engine);
}

}  // namespace

void dispatchEvent(SimulationEngine& engine, const Event& event) {
    engine.stateMutable().metrics.eventsProcessed++;
    switch (event.kind) {
        case EventKind::SatelliteFailure:
            handleSatelliteFailure(engine, event);
            break;
        case EventKind::DebrisAlert:
            handleDebrisAlert(engine, event);
            break;
        case EventKind::PropulsionFailure:
            handleVehicleDegradation(engine, event, ChangeKind::VehicleUnavailable, 1.0);
            break;
        case EventKind::FuelCritical:
            handleVehicleDegradation(engine, event, ChangeKind::FuelChanged, 0.9);
            break;
        case EventKind::CommunicationLoss:
            handleVehicleDegradation(engine, event, ChangeKind::CommunicationLost, 1.0);
            break;
        case EventKind::Rendezvous:
            handleRendezvous(engine, event);
            break;
        case EventKind::VehicleStateChange:
        case EventKind::EmergencyEscalation:
        case EventKind::EmergencyResolved:
        case EventKind::MissionStart:
        case EventKind::MissionReplan:
        case EventKind::RescueStart:
        case EventKind::RescueComplete:
        case EventKind::MissionFailure:
            // Notification/logging events -- brief lists these as event KINDS to
            // represent in the timeline; their actual state transitions already happen
            // where they are triggered (handleSatelliteFailure, applyReplanDecision,
            // handleRendezvous). Reaching this dispatcher for one of these kinds means
            // "record that this happened," which the event's own presence in
            // `eventQueue()`'s processed history already accomplishes -- no further
            // mutation needed here.
            break;
    }
}

}  // namespace orca
