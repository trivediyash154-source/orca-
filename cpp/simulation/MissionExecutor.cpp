#include "MissionExecutor.hpp"

#include <algorithm>
#include <format>
#include <vector>

#include "../core/Validation.hpp"

namespace orca {

std::vector<LegTiming> computeLegTimings(const MissionGraph& graph, const TransferModel& model,
                                         const std::vector<EdgeId>& route,
                                         Seconds planDepartureSec) {
    TransferConstraints permissive;
    permissive.maxDeltaVKms = 1.0e6;
    permissive.deltaVBudgetKms = 1.0e6;
    permissive.availableFuelFraction = 1.0;
    permissive.maxWaitSec = 1.0e13;
    permissive.maxRisk = 1.0;
    permissive.latestArrivalSec = 1.0e18;

    std::vector<LegTiming> timings;
    timings.reserve(route.size());
    Seconds departureSec = planDepartureSec;
    for (const EdgeId edgeId : route) {
        const GraphEdge& edge = graph.edge(edgeId);
        const TransferResult leg = model.evaluate(graph.node(edge.from()).orbit(),
                                                   graph.node(edge.to()).orbit(), departureSec,
                                                   permissive);
        requireThat(leg.feasible,
                    std::format("computeLegTimings: leg {} of an already-adopted route is "
                                "no longer physically evaluable -- this indicates a plan "
                                "was adopted inconsistently with the graph it came from",
                                toString(edgeId)));
        timings.push_back(LegTiming{
            .departureSec = departureSec,
            .arrivalSec = leg.geometry.arrivalTimeSec,
            .deltaVKms = leg.geometry.deltaVKms,
        });
        departureSec = leg.geometry.arrivalTimeSec;
    }
    return timings;
}

ExecutionResult advanceMissionProgress(Mission& mission, RescueVehicle& vehicle,
                                       const MissionPlan& plan, const MissionGraph& graph,
                                       const TransferModel& model, Seconds nowSec) {
    requireThat(mission.state() == MissionState::InTransit,
                std::format("advanceMissionProgress: mission {} must be InTransit (is {})",
                            toString(mission.id()), toString(mission.state())));
    requireThat(plan.route() == mission.currentRoute(),
                std::format("advanceMissionProgress: plan {} does not match mission {}'s "
                            "current route -- pass the plan currently governing it",
                            toString(plan.id()), toString(mission.id())));

    // Idempotence guard: if the mission already arrived on a PREVIOUS call (final leg,
    // progress already at 1.0) but the caller has not yet transitioned it out of
    // InTransit, a second call must not re-consume the final leg's delta-v a second time.
    // Found via this file's own resource-consumption test calling advanceMissionProgress
    // twice in a row without an intervening state transition -- exactly the ordering a
    // caller who processes `Arrived` asynchronously (a scheduled event fired on a LATER
    // tick, as `SimulationEngine::tick()` itself does) could hit.
    if (mission.isOnFinalLeg() && mission.progressOnLeg() >= 1.0) {
        return ExecutionResult{.outcome = ExecutionOutcome::Arrived,
                              .deltaVConsumedThisCallKms = 0.0};
    }

    const std::vector<LegTiming> timings =
        computeLegTimings(graph, model, plan.route(), plan.departureTimeSec());

    ExecutionResult result;
    while (mission.currentLeg() < timings.size() &&
          nowSec >= timings[mission.currentLeg()].arrivalSec) {
        const LegTiming& leg = timings[mission.currentLeg()];
        vehicle.consumeDeltaV(leg.deltaVKms);
        mission.recordDeltaVSpent(leg.deltaVKms);
        result.deltaVConsumedThisCallKms += leg.deltaVKms;

        if (mission.isOnFinalLeg()) {
            mission.setProgressOnLeg(1.0);
            result.outcome = ExecutionOutcome::Arrived;
            return result;
        }
        mission.advanceLeg();
        result.outcome = ExecutionOutcome::LegCompleted;
    }

    // Either no leg completed this call, or the mission is now mid a LATER leg that has
    // not yet arrived -- update progress within whichever leg is current.
    const LegTiming& current = timings[mission.currentLeg()];
    const Seconds legDuration = current.arrivalSec - current.departureSec;
    const double fraction =
        legDuration > 0.0 ? std::clamp((nowSec - current.departureSec) / legDuration, 0.0, 1.0)
                          : 1.0;
    mission.setProgressOnLeg(fraction);
    return result;
}

}  // namespace orca
