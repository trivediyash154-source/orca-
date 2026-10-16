#include "AssignmentCost.hpp"

#include <unordered_map>

#include "../core/Validation.hpp"
#include "MissionScorer.hpp"
#include "ObjectiveNormalizer.hpp"

namespace orca {

namespace {

/// `fronts` and `scored` must be parallel (same index = same candidate) -- true whenever
/// `scored` came from `scoreCandidates(fronts, profile)`, which every caller in this
/// codebase uses.
const ObjectiveVector* selectedCostOrNull(const CandidateFront& front,
                                         const ScoredCandidate& candidate) {
    if (!candidate.feasible) {
        return nullptr;
    }
    return &front.front.solutions[*candidate.selectedPlanIndex].totalCost();
}

std::vector<ObjectiveVector> collectFeasibleCosts(const std::vector<CandidateFront>& fronts,
                                                  const std::vector<ScoredCandidate>& scored) {
    std::vector<ObjectiveVector> costs;
    costs.reserve(scored.size());
    for (std::size_t i = 0; i < scored.size(); ++i) {
        if (const ObjectiveVector* cost = selectedCostOrNull(fronts[i], scored[i])) {
            costs.push_back(*cost);
        }
    }
    return costs;
}

}  // namespace

ObjectiveVector computeGlobalIdeal(const std::vector<CandidateFront>& fronts,
                                   const std::vector<ScoredCandidate>& scored) {
    return computeIdeal(collectFeasibleCosts(fronts, scored));
}

ObjectiveVector computeGlobalNadir(const std::vector<CandidateFront>& fronts,
                                   const std::vector<ScoredCandidate>& scored) {
    return computeNadir(collectFeasibleCosts(fronts, scored));
}

std::vector<AssignmentCostEntry> buildAssignmentCosts(
    const std::vector<CandidateFront>& fronts, const std::vector<ScoredCandidate>& scored,
    const std::vector<Emergency>& emergencies, Seconds nowSec,
    const OptimizationProfile& profile, const PriorityWeights& priorityWeights) {
    const ObjectiveVector globalIdeal = computeGlobalIdeal(fronts, scored);
    const ObjectiveVector globalNadir = computeGlobalNadir(fronts, scored);

    // Per-emergency feasibility: true iff at least one candidate for that emergency,
    // across every vehicle, is feasible. Priority is a property of the emergency, not of
    // any one vehicle's attempt at it, so this is computed once per emergency and shared
    // across every entry naming it.
    std::unordered_map<std::uint32_t, bool> emergencyHasFeasibleCandidate;
    for (const ScoredCandidate& candidate : scored) {
        bool& hasFeasible = emergencyHasFeasibleCandidate[candidate.emergency.value()];
        hasFeasible = hasFeasible || candidate.feasible;
    }

    std::unordered_map<std::uint32_t, double> priorityByEmergency;
    for (const Emergency& emergency : emergencies) {
        const bool feasibleExists = emergencyHasFeasibleCandidate[emergency.id().value()];
        priorityByEmergency[emergency.id().value()] =
            computePriority(emergency, nowSec, feasibleExists, priorityWeights);
    }

    // Every emergency `scored` actually references must have a priority computed above --
    // i.e. `emergencies` must be the same list `scored` was built against. Checked
    // explicitly (found during Phase 6's adversarial self-review, the same
    // mismatched-parallel-array risk `AssignmentMatrix.cpp`/`AssignmentResult.cpp` guard
    // against) rather than left to `.at()`'s `std::out_of_range`, which is not this
    // codebase's uniform "caller error" exception type (`Validation.hpp`'s `requireThat`).
    for (const ScoredCandidate& candidate : scored) {
        requireThat(priorityByEmergency.count(candidate.emergency.value()) > 0,
                    "buildAssignmentCosts: scored references an emergency not present in "
                    "the emergencies list");
    }

    std::vector<AssignmentCostEntry> entries;
    entries.reserve(scored.size());
    for (std::size_t i = 0; i < scored.size(); ++i) {
        const ScoredCandidate& candidate = scored[i];
        const double priority = priorityByEmergency.at(candidate.emergency.value());

        AssignmentCostEntry entry{.vehicle = candidate.vehicle,
                                  .emergency = candidate.emergency,
                                  .feasible = candidate.feasible,
                                  .priority = priority};

        if (const ObjectiveVector* cost = selectedCostOrNull(fronts[i], candidate)) {
            const ScoreBreakdown breakdown =
                scorePlan(*cost, globalIdeal, globalNadir, profile);
            entry.cost = breakdown.totalScore * (2.0 - priority);
        }

        entries.push_back(entry);
    }
    return entries;
}

}  // namespace orca
