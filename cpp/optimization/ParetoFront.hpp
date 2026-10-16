#pragma once

/// @file ParetoFront.hpp
/// Turns `algorithms::findParetoRoutes()`'s raw, domain-agnostic result into the
/// `MissionPlan`-bearing structure `DATA_MODEL.md` §7 specifies as `ParetoFront`.
///
/// ## Why this conversion is a separate function in `optimization/`, not part of the search
///
/// `ParetoSearch.hpp`'s file comment explains the boundary this respects:
/// `algorithms/` has no knowledge of missions, vehicles or emergencies
/// (`ARCHITECTURE.md`'s dependency table), so `findParetoRoutes()` cannot itself produce
/// `MissionPlan` objects — building one requires a `VehicleId`, an `EmergencyId` and a
/// `PlanId`, none of which a generic node-to-node search has or needs. This file is where
/// a caller who *does* have that context (because it invoked the search for a specific
/// vehicle rescuing a specific emergency) supplies it and gets `MissionPlan`s back.
///
/// ## The trivial (source == target) case produces an EMPTY front, not a plan
///
/// `MissionPlan::makeFeasible` rejects an empty route (`MissionPlan.hpp`'s documented
/// `@throws`) — a plan is, by definition, a route somewhere. A search where source equals
/// target has nothing to plan, so `buildParetoFront` returns a front with no solutions and
/// no dominated plans in that case, rather than attempting (and failing) to construct a
/// zero-leg `MissionPlan`. Tested explicitly (`pareto_front_test.cpp`), not left as an
/// unhandled edge case.
///
/// ## PlanId assignment
///
/// Sequential from `firstPlanId`, `solutions` first (in the order `ParetoSearchResult`
/// already holds them) then `dominated`. `OPTIMIZATION_MODEL.md` §10 requires only that
/// assignment be *deterministic*, not that it reflect wall-clock discovery order — this
/// policy satisfies that with no additional bookkeeping in the search itself.

#include "../algorithms/ParetoSearch.hpp"
#include "../core/Types.hpp"
#include "../domain/MissionPlan.hpp"

namespace orca {

struct ParetoFront {
    std::vector<MissionPlan> solutions;  ///< mutually non-dominated (property-tested)
    std::vector<MissionPlan> dominated;  ///< retained for visualisation (DATA_MODEL.md §7)
    ObjectiveVector ideal;   ///< per-objective best across `solutions`; zero vector if empty
    ObjectiveVector nadir;   ///< per-objective worst across `solutions`; zero vector if empty
    bool truncated = false;  ///< mirrors ParetoSearchMetrics::truncated
    int labelsGenerated = 0;
    int labelsPruned = 0;  ///< labelsDominated + labelsDuplicate from the raw search

    [[nodiscard]] std::size_t size() const noexcept { return solutions.size(); }
    [[nodiscard]] bool isEmpty() const noexcept { return solutions.empty(); }
};

/// Builds a `ParetoFront` from a raw search result plus the mission context the search
/// itself does not know about.
///
/// @param firstPlanId The first `PlanId` to assign; subsequent plans get consecutive
///        values in the order described in the file comment.
[[nodiscard]] ParetoFront buildParetoFront(const ParetoSearchResult& searchResult,
                                          VehicleId vehicle, EmergencyId emergency,
                                          PlanId firstPlanId);

}  // namespace orca
