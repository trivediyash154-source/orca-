#include "ParetoFront.hpp"

#include <algorithm>
#include <limits>

namespace orca {

namespace {

/// Componentwise min (ideal) or max (nadir) of `cost[i]` across `routes`. Returns the zero
/// vector for an empty `routes` — meaningless in that case, but never garbage: callers
/// only use it when `!routes.empty()`.
ObjectiveVector componentwiseExtreme(const std::vector<ParetoRoute>& routes, bool wantMin) {
    if (routes.empty()) {
        return ObjectiveVector{};
    }
    ObjectiveVector extreme = routes.front().totalCost;
    for (std::size_t i = 1; i < routes.size(); ++i) {
        const ObjectiveVector& cost = routes[i].totalCost;
        for (std::size_t dim = 0; dim < kObjectiveCount; ++dim) {
            if (wantMin ? (cost[dim] < extreme[dim]) : (cost[dim] > extreme[dim])) {
                extreme[dim] = cost[dim];
            }
        }
    }
    return extreme;
}

MissionPlan toMissionPlan(const ParetoRoute& route, PlanId planId, VehicleId vehicle,
                          EmergencyId emergency, Seconds departureTimeSec) {
    std::vector<EdgeId> edges;
    edges.reserve(route.legs.size());
    for (const PathLeg& leg : route.legs) {
        edges.push_back(leg.edge);
    }
    return MissionPlan::makeFeasible(planId, vehicle, emergency, std::move(edges),
                                     route.totalCost, departureTimeSec,
                                     route.arrivalTimeSec);
}

}  // namespace

ParetoFront buildParetoFront(const ParetoSearchResult& searchResult, VehicleId vehicle,
                             EmergencyId emergency, PlanId firstPlanId) {
    ParetoFront front;
    front.truncated = searchResult.metrics.truncated;
    front.labelsGenerated = searchResult.metrics.labelsGenerated;
    front.labelsPruned = searchResult.metrics.labelsDominated + searchResult.metrics.labelsDuplicate;

    // The trivial source == target search produces one zero-leg ParetoRoute.
    // MissionPlan::makeFeasible rejects an empty route by design (see file comment) --
    // there is no mission to plan when no travel is needed, so this front is empty.
    if (searchResult.source == searchResult.target) {
        return front;
    }

    PlanId::Value nextPlanValue = firstPlanId.value();
    front.solutions.reserve(searchResult.solutions.size());
    for (const ParetoRoute& route : searchResult.solutions) {
        front.solutions.push_back(toMissionPlan(route, PlanId(nextPlanValue), vehicle,
                                                emergency, searchResult.startTimeSec));
        ++nextPlanValue;
    }
    front.dominated.reserve(searchResult.dominated.size());
    for (const ParetoRoute& route : searchResult.dominated) {
        front.dominated.push_back(toMissionPlan(route, PlanId(nextPlanValue), vehicle,
                                                emergency, searchResult.startTimeSec));
        ++nextPlanValue;
    }

    front.ideal = componentwiseExtreme(searchResult.solutions, /*wantMin=*/true);
    front.nadir = componentwiseExtreme(searchResult.solutions, /*wantMin=*/false);

    return front;
}

}  // namespace orca
