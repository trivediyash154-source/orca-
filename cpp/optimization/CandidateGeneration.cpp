#include "CandidateGeneration.hpp"

#include <variant>

#include "../algorithms/Dijkstra.hpp"
#include "../algorithms/ParetoSearch.hpp"
#include "../core/Validation.hpp"
#include "ParetoFront.hpp"

namespace orca {

namespace {

constexpr Seconds kEffectivelyNoDeadlineSec = 1.0e18;
constexpr KmPerSec kEffectivelyUnlimitedDeltaVKms = 1.0e6;

SearchConstraints makeSearchConstraints(Seconds vehicleDeltaVBudgetKms,
                                        Seconds latestArrivalSec) {
    SearchConstraints c;
    c.vehicleDeltaVBudgetKms = vehicleDeltaVBudgetKms;
    c.maxWaitSec = 1.0e13;  // unbounded here -- Group 1's risk/distance ceilings and the
                            // deadline are the constraints this layer cares about; a wait
                            // horizon is a per-search policy knob Phase 6 does not narrow.
    c.latestArrivalSec = latestArrivalSec;
    return c;
}

/// Diagnoses why `vehicle` cannot reach `emergency` -- see the file comment's honest,
/// bounded scope. Only called on the already-confirmed-infeasible path, so it costs
/// nothing in the common (feasible) case.
ConstraintKind diagnoseInfeasibility(const MissionGraph& graph,
                                     const TransferModel& transferModel, NodeId vehicleNode,
                                     NodeId emergencyNode, const RescueVehicle& vehicle,
                                     Seconds originalDeadlineSec, Seconds nowSec) {
    const SearchConstraints relaxedDeadline =
        makeSearchConstraints(vehicle.deltaVRemainingKms(), kEffectivelyNoDeadlineSec);
    if (!findParetoRoutes(graph, transferModel, vehicleNode, emergencyNode, nowSec,
                          relaxedDeadline)
             .isEmpty()) {
        return ConstraintKind::SurvivalWindow;
    }

    const SearchConstraints relaxedDeltaV =
        makeSearchConstraints(kEffectivelyUnlimitedDeltaVKms, originalDeadlineSec);
    if (!findParetoRoutes(graph, transferModel, vehicleNode, emergencyNode, nowSec,
                          relaxedDeltaV)
             .isEmpty()) {
        return ConstraintKind::DeltaVBudget;
    }

    // Neither relaxed alone succeeds -- either both are simultaneously binding, or the
    // pair is unreachable regardless of resources. Reported as Unreachable; the file
    // comment states this simplification explicitly.
    return ConstraintKind::Unreachable;
}

}  // namespace

EntityNodeIndex buildEntityNodeIndex(const MissionGraph& graph) {
    EntityNodeIndex index;
    for (const GraphNode& node : graph.nodes()) {
        std::visit(
            [&](auto entityId) {
                using EntityType = std::decay_t<decltype(entityId)>;
                if constexpr (std::is_same_v<EntityType, VehicleId>) {
                    index.vehicleNodes.emplace(entityId.value(), node.id());
                } else {
                    index.satelliteNodes.emplace(entityId.value(), node.id());
                }
            },
            node.entity());
    }
    return index;
}

CandidateFront generateCandidateFront(const MissionGraph& graph,
                                      const TransferModel& transferModel,
                                      const EntityNodeIndex& index,
                                      const RescueVehicle& vehicle,
                                      const Emergency& emergency, Seconds nowSec,
                                      PlanId& nextPlanId) {
    const auto vehicleIt = index.vehicleNodes.find(vehicle.id().value());
    requireThat(vehicleIt != index.vehicleNodes.end(),
                "generateCandidateFront: vehicle is not a node in this graph");
    const auto satelliteIt = index.satelliteNodes.find(emergency.subject().value());
    requireThat(satelliteIt != index.satelliteNodes.end(),
                "generateCandidateFront: emergency's subject satellite is not a node in "
                "this graph");

    CandidateFront candidate{.vehicle = vehicle.id(), .emergency = emergency.id()};

    if (!vehicle.isAvailable()) {
        candidate.feasible = false;
        candidate.infeasibilityReason = ConstraintKind::NoAsset;
        return candidate;
    }

    const NodeId vehicleNode = vehicleIt->second;
    const NodeId emergencyNode = satelliteIt->second;
    const Seconds deadlineSec = emergency.deadlineSec();

    const SearchConstraints constraints =
        makeSearchConstraints(vehicle.deltaVRemainingKms(), deadlineSec);
    const ParetoSearchResult raw =
        findParetoRoutes(graph, transferModel, vehicleNode, emergencyNode, nowSec,
                         constraints);

    candidate.feasible = !raw.isEmpty();
    if (!candidate.feasible) {
        candidate.infeasibilityReason =
            diagnoseInfeasibility(graph, transferModel, vehicleNode, emergencyNode, vehicle,
                                 deadlineSec, nowSec);
    }

    candidate.front = buildParetoFront(raw, vehicle.id(), emergency.id(), nextPlanId);
    // Read the actual count of PlanIds consumed off the returned front, rather than
    // predicting it from `raw` -- buildParetoFront() returns an EMPTY front (consuming no
    // PlanIds at all) for the trivial source==target case (ParetoFront.hpp's file
    // comment), which would make a prediction from `raw.solutions.size()` wrong in that
    // one case. Vehicle and satellite nodes are always structurally distinct
    // (GraphNode::entity() is never simultaneously both), so that case cannot occur via
    // this function's real call pattern -- but computing the advance from the actual
    // result, not a predicted one, makes that a fact this code does not need to rely on.
    const std::uint32_t planCount = static_cast<std::uint32_t>(
        candidate.front.solutions.size() + candidate.front.dominated.size());
    nextPlanId = PlanId(nextPlanId.value() + planCount);

    return candidate;
}

std::vector<CandidateFront> generateAllCandidateFronts(
    const MissionGraph& graph, const TransferModel& transferModel,
    const std::vector<RescueVehicle>& vehicles, const std::vector<Emergency>& emergencies,
    Seconds nowSec, PlanId firstPlanId) {
    const EntityNodeIndex index = buildEntityNodeIndex(graph);

    std::vector<CandidateFront> candidates;
    candidates.reserve(vehicles.size() * emergencies.size());
    PlanId nextPlanId = firstPlanId;
    for (const RescueVehicle& vehicle : vehicles) {
        for (const Emergency& emergency : emergencies) {
            candidates.push_back(generateCandidateFront(graph, transferModel, index,
                                                         vehicle, emergency, nowSec,
                                                         nextPlanId));
        }
    }
    return candidates;
}

std::vector<ScoredCandidate> scoreCandidates(const std::vector<CandidateFront>& fronts,
                                             const OptimizationProfile& profile) {
    std::vector<ScoredCandidate> scored;
    scored.reserve(fronts.size());
    for (const CandidateFront& candidateFront : fronts) {
        ScoredCandidate scoredCandidate{.vehicle = candidateFront.vehicle,
                                       .emergency = candidateFront.emergency,
                                       .feasible = candidateFront.feasible,
                                       .infeasibilityReason =
                                           candidateFront.infeasibilityReason};

        if (candidateFront.feasible) {
            const std::optional<RecommendedPlan> recommended =
                selectRecommendedPlan(candidateFront.front, profile);
            // candidateFront.feasible == true guarantees front.solutions is non-empty
            // (CandidateFront's own invariant, generateCandidateFront's construction), so
            // selectRecommendedPlan cannot return nullopt here.
            scoredCandidate.selectedPlanIndex = recommended->planIndex;
            scoredCandidate.breakdown = recommended->breakdown;
        }

        scored.push_back(std::move(scoredCandidate));
    }
    return scored;
}

}  // namespace orca
