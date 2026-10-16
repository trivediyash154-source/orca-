#include "GlobalReplanner.hpp"

#include <format>

#include "../core/Validation.hpp"
#include "../optimization/AssignmentCost.hpp"
#include "../optimization/AssignmentMatrix.hpp"
#include "../optimization/CandidateGeneration.hpp"

namespace orca {

AssignmentResult evaluateGlobalReplan(const MissionGraph& graph, const TransferModel& model,
                                      const std::vector<RescueVehicle>& availableVehicles,
                                      const std::vector<Emergency>& pendingEmergencies,
                                      Seconds nowSec, const OptimizationProfile& profile,
                                      PlanId& nextPlanId,
                                      const PriorityWeights& priorityWeights) {
    // Every vehicle offered here is about to be considered for a BRAND NEW assignment --
    // one already flying an active mission must never appear in this list (found during
    // Phase 7's adversarial self-review: nothing previously stopped a caller from
    // accidentally passing a busy vehicle through and getting it silently double-booked
    // onto a second emergency, since Phase 6's own assignment machinery has no notion of
    // "already committed" at all -- that concept belongs entirely to the caller's own
    // bookkeeping, which this check makes an explicit, enforced contract rather than an
    // unstated assumption).
    for (const RescueVehicle& vehicle : availableVehicles) {
        requireThat(vehicle.isAvailable(),
                    std::format("evaluateGlobalReplan: vehicle {} is not available for a "
                                "new assignment (status={}) -- an already-committed "
                                "vehicle must never be offered to a global replan",
                                toString(vehicle.id()), toString(vehicle.status())));
    }

    const std::vector<CandidateFront> fronts = generateAllCandidateFronts(
        graph, model, availableVehicles, pendingEmergencies, nowSec, nextPlanId);
    // generateAllCandidateFronts() does not itself report how far it advanced the id
    // sequence (CandidateGeneration.cpp advances a LOCAL copy internally) -- recompute the
    // same way generateCandidateFront() does, from the actual fronts produced, so
    // `nextPlanId` is correct for whatever the caller does next (the same "read the real
    // count off the result, never predict it" discipline CandidateGeneration.cpp's own
    // file comment documents).
    std::uint32_t consumed = 0;
    for (const CandidateFront& front : fronts) {
        consumed += static_cast<std::uint32_t>(front.front.solutions.size() +
                                               front.front.dominated.size());
    }
    nextPlanId = PlanId(nextPlanId.value() + consumed);

    const std::vector<ScoredCandidate> scored = scoreCandidates(fronts, profile);
    const std::vector<AssignmentCostEntry> costs = buildAssignmentCosts(
        fronts, scored, pendingEmergencies, nowSec, profile, priorityWeights);

    std::vector<VehicleId> vehicleIds;
    vehicleIds.reserve(availableVehicles.size());
    for (const RescueVehicle& v : availableVehicles) vehicleIds.push_back(v.id());

    std::vector<EmergencyId> emergencyIds;
    emergencyIds.reserve(pendingEmergencies.size());
    for (const Emergency& e : pendingEmergencies) emergencyIds.push_back(e.id());

    const AssignmentMatrix matrix = buildAssignmentMatrix(costs, vehicleIds, emergencyIds);
    return solveAssignmentHungarian(matrix, fronts, scored);
}

}  // namespace orca
