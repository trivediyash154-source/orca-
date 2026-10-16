#include "DynamicReplanner.hpp"

#include <algorithm>
#include <chrono>

#include "../algorithms/ParetoSearch.hpp"
#include "../core/Validation.hpp"
#include "../optimization/CandidateGeneration.hpp"
#include "../optimization/MissionScorer.hpp"
#include "../optimization/ObjectiveNormalizer.hpp"
#include "../optimization/ParetoFront.hpp"
#include "MissionValidity.hpp"

namespace orca {

namespace {

// Unbounded, matching CandidateGeneration.cpp's makeSearchConstraints() EXACTLY --
// MissionValidity.cpp's own file comment has the reasoning (and the bug this avoids
// repeating) for why this must match Phase 6's original planning convention rather than
// the struct's 6-hour default.
constexpr Seconds kReplanMaxWaitSec = 1.0e13;

SearchConstraints makeReplanSearchConstraints(KmPerSec deltaVRemainingKms,
                                              Seconds latestArrivalSec) {
    SearchConstraints c;
    c.vehicleDeltaVBudgetKms = deltaVRemainingKms;
    c.maxWaitSec = kReplanMaxWaitSec;
    c.latestArrivalSec = latestArrivalSec;
    return c;
}

/// Runs the real Phase 5 search from `sourceNode` (the vehicle's CURRENT position on the
/// graph, per `MissionValidity.hpp`'s leg-progress convention) to `targetNode`, and turns
/// the result into a `MissionPlan`-bearing `ParetoFront` via the real Phase 5 conversion --
/// the only two calls this file makes to produce a new candidate. Advances `nextPlanId`.
ParetoFront generateReplanFront(const MissionGraph& graph, const TransferModel& model,
                                NodeId sourceNode, NodeId targetNode, Seconds nowSec,
                                KmPerSec deltaVRemainingKms, Seconds latestArrivalSec,
                                VehicleId vehicle, EmergencyId emergency, PlanId& nextPlanId) {
    const SearchConstraints constraints =
        makeReplanSearchConstraints(deltaVRemainingKms, latestArrivalSec);
    const ParetoSearchResult raw =
        findParetoRoutes(graph, model, sourceNode, targetNode, nowSec, constraints);
    ParetoFront front = buildParetoFront(raw, vehicle, emergency, nextPlanId);
    nextPlanId = PlanId(nextPlanId.value() +
                        static_cast<std::uint32_t>(front.solutions.size() + front.dominated.size()));
    return front;
}

bool routeUsesEdge(const MissionPlan& plan, EdgeId edge) {
    const auto& route = plan.route();
    return std::find(route.begin(), route.end(), edge) != route.end();
}

void recomputeBounds(ParetoFront& front) {
    if (front.solutions.empty()) {
        return;
    }
    std::vector<ObjectiveVector> costs;
    costs.reserve(front.solutions.size());
    for (const MissionPlan& plan : front.solutions) costs.push_back(plan.totalCost());
    front.ideal = computeIdeal(costs);
    front.nadir = computeNadir(costs);
}

/// Phase 8: `findParetoRoutes()` has no "exclude this edge" option (Phase 4/5 are
/// unmodified -- `DynamicReplanner.hpp`'s own file comment), so a debris-invalidated edge
/// cannot be kept OUT of the search. It can, however, be filtered out of the RESULT: a
/// multi-objective search already returns every non-dominated route, not just one, so any
/// solution that happens to use the invalidated edge is simply excluded before selection,
/// leaving whichever non-dominated alternatives remain. `ideal`/`nadir` are recomputed over
/// the SURVIVING solutions only -- the original bounds may have been set by a route that no
/// longer exists as an option.
///
/// ## Falling back to `dominated`, and why that is not fabricating a solution
///
/// If EVERY non-dominated solution used the invalidated edge, `front.solutions` filters to
/// empty even though `front.dominated` may still hold other real, physically feasible
/// routes reaching the target -- they were only ever excluded from `solutions` for being
/// WORSE than the (now-unusable) best route, never for being infeasible. Found via this
/// file's own scenario testing: a genuinely alternate, if inferior, path existed but the
/// mission reported `Abort` anyway, because the better route's dominance had pruned the
/// alternative out of consideration before the invalidation was even known. Falling back to
/// the best surviving `dominated` route when `solutions` is empty is not inventing a
/// solution -- every route in `dominated` was independently confirmed feasible by the same
/// search (`ParetoFront.hpp`'s own "retained for visualisation" scope, extended here to
/// "retained as a fallback candidate pool" once the front's own optimal choice is gone).
ParetoFront excludeSolutionsUsingEdge(const ParetoFront& front,
                                      std::optional<EdgeId> invalidatedEdge) {
    if (!invalidatedEdge.has_value()) {
        return front;
    }
    ParetoFront filtered;
    filtered.dominated = front.dominated;
    filtered.truncated = front.truncated;
    filtered.labelsGenerated = front.labelsGenerated;
    filtered.labelsPruned = front.labelsPruned;
    for (const MissionPlan& plan : front.solutions) {
        if (!routeUsesEdge(plan, *invalidatedEdge)) {
            filtered.solutions.push_back(plan);
        }
    }
    if (filtered.solutions.empty()) {
        for (const MissionPlan& plan : front.dominated) {
            if (!routeUsesEdge(plan, *invalidatedEdge)) {
                filtered.solutions.push_back(plan);
            }
        }
    }
    recomputeBounds(filtered);
    return filtered;
}

ReplanReason reasonForChangeKind(ChangeKind kind) noexcept {
    switch (kind) {
        case ChangeKind::FuelChanged:            return ReplanReason::FuelConstraint;
        case ChangeKind::VehicleUnavailable:     return ReplanReason::VehicleUnavailable;
        case ChangeKind::TargetConditionChanged: return ReplanReason::TargetDegraded;
        case ChangeKind::RiskChanged:             return ReplanReason::RiskConstraint;
        case ChangeKind::DebrisDetected:          return ReplanReason::RouteInvalidated;
        // Deliberately NOT NewHigherPriorityEmergency: evaluate()'s local optional path
        // only ever searches for a better route to THIS mission's OWN emergency -- it has
        // no mechanism to reconsider whether a DIFFERENT emergency should take priority
        // over continuing. That question belongs entirely to
        // DynamicReplanner::evaluatePreemption(), a separate call. Mapping these here to
        // NewHigherPriorityEmergency would misleadingly claim evaluate() acted on the new
        // emergency's priority when it structurally cannot -- found during Phase 7's
        // adversarial self-review, before any caller could be misled by it.
        case ChangeKind::EmergencyAdded:          return ReplanReason::EnvironmentChanged;
        case ChangeKind::EmergencyEscalated:      return ReplanReason::EnvironmentChanged;
        case ChangeKind::EmergencyResolved:       return ReplanReason::None;
        case ChangeKind::CommunicationLost:       return ReplanReason::CommunicationLoss;
        case ChangeKind::RouteInvalidated:        return ReplanReason::RouteInvalidated;
        case ChangeKind::DeadlineChanged:         return ReplanReason::DeadlineThreat;
        case ChangeKind::ManualReplan:            return ReplanReason::ManualReplan;
    }
    return ReplanReason::EnvironmentChanged;
}

AbortReason mapAbortReason(ReplanReason reason) noexcept {
    switch (reason) {
        case ReplanReason::DeltaVConstraint:
        case ReplanReason::FuelConstraint:
            return AbortReason::InsufficientFuel;
        case ReplanReason::DeadlineThreat:
            return AbortReason::DeadlineImpossible;
        case ReplanReason::RiskConstraint:
            return AbortReason::RiskUnacceptable;
        case ReplanReason::VehicleUnavailable:
            return AbortReason::VehicleUnavailable;
        case ReplanReason::RouteInvalidated:
        case ReplanReason::TargetDegraded:
        case ReplanReason::EnvironmentChanged:
        case ReplanReason::NewHigherPriorityEmergency:
        case ReplanReason::CommunicationLoss:
        case ReplanReason::ManualReplan:
        case ReplanReason::VehicleDegraded:
        case ReplanReason::None:
            return AbortReason::NoFeasibleContinuation;
    }
    return AbortReason::NoFeasibleContinuation;
}

/// Phase 7 brief §12: "did this change materially affect the mission?" `ManualReplan` and
/// any kind the caller did not attach a magnitude to are always material (see
/// `MissionChange.hpp`'s own field comment for why). A magnitude IS attached: compared
/// against the fuel-specific threshold for `FuelChanged`, the shared generic threshold
/// otherwise.
bool isMaterialChange(const MissionChange& trigger, const ReplanPolicy& policy) noexcept {
    if (!trigger.magnitudeFraction.has_value()) {
        return true;
    }
    const double threshold = trigger.kind == ChangeKind::FuelChanged
                                 ? policy.materialFuelChangeFraction
                                 : policy.materialRiskChangeFraction;
    return *trigger.magnitudeFraction >= threshold;
}

NodeId currentRouteNode(const MissionGraph& graph, const std::vector<EdgeId>& route,
                        std::size_t startLeg) {
    return graph.edge(route[startLeg]).from();
}

}  // namespace

DynamicReplanner::DynamicReplanner(ReplanPolicy policy) : policy_(policy) {}

std::optional<Seconds> DynamicReplanner::lastReplanAtSec(MissionId mission) const noexcept {
    const auto it = std::find_if(lastReplanAtSecByMission_.begin(),
                                 lastReplanAtSecByMission_.end(),
                                 [&](const auto& p) { return p.first == mission.value(); });
    return it != lastReplanAtSecByMission_.end() ? std::optional<Seconds>(it->second)
                                                 : std::nullopt;
}

void DynamicReplanner::recordReplanTimestamp(MissionId mission, Seconds atSec) {
    for (auto& p : lastReplanAtSecByMission_) {
        if (p.first == mission.value()) {
            p.second = atSec;
            return;
        }
    }
    lastReplanAtSecByMission_.emplace_back(mission.value(), atSec);
}

void DynamicReplanner::finalize(const Mission& mission, const ReplanDecision& decision,
                                std::chrono::steady_clock::time_point startedAt,
                                Seconds nowSec) {
    const auto finishedAt = std::chrono::steady_clock::now();
    metrics_.totalReplanComputationMs +=
        std::chrono::duration<double, std::milli>(finishedAt - startedAt).count();

    history_.push_back(ReplanHistoryEntry{
        .timestampSec = nowSec,
        .mission = mission.id(),
        .reason = decision.reason,
        .previousPlan = decision.previousPlan,
        .newPlan = decision.newPlan ? std::optional<PlanId>(decision.newPlan->id()) : std::nullopt,
        .decision = decision.decision,
    });
}

ReplanDecision DynamicReplanner::evaluatePreemption(const Mission& mission,
                                                    double missionProgressFraction,
                                                    double currentEmergencyPriority,
                                                    double candidateEmergencyPriority,
                                                    Seconds nowSec) {
    requireThat(missionProgressFraction >= 0.0 && missionProgressFraction <= 1.0,
                "DynamicReplanner::evaluatePreemption: missionProgressFraction must lie "
                "in [0,1]");

    ReplanDecision decision{.previousPlan = mission.sourcePlan(), .decidedAtSec = nowSec};

    const bool tooFarAlong = missionProgressFraction >= policy_.preemptionMaxProgressFraction;
    const bool priorityGapSufficient =
        (candidateEmergencyPriority - currentEmergencyPriority) >= policy_.preemptionPriorityMargin;

    if (tooFarAlong || !priorityGapSufficient) {
        decision.decision = ReplanDecisionType::Continue;
        decision.reason = ReplanReason::None;
        decision.detail = tooFarAlong
                              ? "mission progress exceeds the preemption cutoff"
                              : "candidate emergency's priority margin is insufficient";
        ++metrics_.planRetentions;
    } else {
        decision.decision = ReplanDecisionType::Abort;
        decision.reason = ReplanReason::NewHigherPriorityEmergency;
        decision.abortReason = AbortReason::Preempted;
        decision.detail = "preempted by a higher-priority emergency";
        ++metrics_.aborts;
    }

    history_.push_back(ReplanHistoryEntry{
        .timestampSec = nowSec,
        .mission = mission.id(),
        .reason = decision.reason,
        .previousPlan = decision.previousPlan,
        .newPlan = std::nullopt,
        .decision = decision.decision,
    });
    return decision;
}

ReplanDecision DynamicReplanner::evaluate(const Mission& mission, const RescueVehicle& vehicle,
                                          const Emergency& emergency, const MissionGraph& graph,
                                          const TransferModel& model, Seconds nowSec,
                                          const MissionChange& trigger,
                                          const OptimizationProfile& profile,
                                          PlanId& nextPlanId) {
    const auto startedAt = std::chrono::steady_clock::now();
    ++metrics_.replanRequests;

    ReplanDecision decision{.previousPlan = mission.sourcePlan(), .decidedAtSec = nowSec};

    const MissionValidity validity = evaluateMissionValidity(
        mission, vehicle, emergency, graph, model, nowSec, policy_, trigger.invalidatedEdge);
    decision.validity = validity;
    decision.previousRemainingCost = validity.remaining.cost;
    decision.previousArrivalSec = validity.remaining.arrivalTimeSec;
    decision.detail = validity.detail;

    if (validity.state == ValidityState::Completed) {
        decision.decision = ReplanDecisionType::Complete;
        decision.reason = ReplanReason::None;
        ++metrics_.completions;
    } else if (validity.state == ValidityState::Invalid) {
        // Mandatory: the current route no longer works, regardless of switchMargin.
        decision.reason = validity.reason;
        ++metrics_.mandatoryReplans;

        if (validity.violatedConstraint == ConstraintKind::NoAsset) {
            // The vehicle itself -- not the route -- is the problem (disabled). Searching
            // for a new ROUTE for a vehicle that cannot fly ANY route is meaningless;
            // offering a different vehicle is GlobalReplanner's job, not this call's
            // (`DynamicReplanner.hpp`'s own file comment on the local/global split).
            // Found via this file's own scenario testing: an earlier version searched
            // anyway and reported REPLAN for a Disabled vehicle, which made no physical
            // sense.
            decision.decision = ReplanDecisionType::Abort;
            decision.abortReason = AbortReason::VehicleUnavailable;
            ++metrics_.aborts;
            finalize(mission, decision, startedAt, nowSec);
            return decision;
        }

        const std::size_t startLeg =
            remainingLegStartIndex(mission.currentLeg(), mission.progressOnLeg());
        requireThat(startLeg < mission.currentRoute().size(),
                    "DynamicReplanner::evaluate: Invalid validity but no route remains -- "
                    "should have been Completed");
        const EntityNodeIndex index = buildEntityNodeIndex(graph);
        const auto satelliteIt = index.satelliteNodes.find(emergency.subject().value());
        requireThat(satelliteIt != index.satelliteNodes.end(),
                    "DynamicReplanner::evaluate: emergency's subject satellite is not a "
                    "node in this graph");
        const NodeId sourceNode = currentRouteNode(graph, mission.currentRoute(), startLeg);

        const ParetoFront rawFront =
            generateReplanFront(graph, model, sourceNode, satelliteIt->second, nowSec,
                               vehicle.deltaVRemainingKms(), emergency.deadlineSec(),
                               mission.vehicle(), mission.emergency(), nextPlanId);
        metrics_.candidatePlansGenerated +=
            static_cast<int>(rawFront.solutions.size() + rawFront.dominated.size());
        const ParetoFront front = excludeSolutionsUsingEdge(rawFront, trigger.invalidatedEdge);

        const std::optional<RecommendedPlan> recommended = selectRecommendedPlan(front, profile);
        if (!recommended.has_value()) {
            decision.decision = ReplanDecisionType::Abort;
            decision.abortReason = mapAbortReason(validity.reason);
            ++metrics_.aborts;
        } else {
            decision.decision = ReplanDecisionType::Replan;
            decision.newPlan = front.solutions[recommended->planIndex];
            ++metrics_.planSwitches;
            recordReplanTimestamp(mission.id(), nowSec);
        }
    } else {
        // Valid or Warning -- optional replan path. Materiality and throttling gate
        // whether an alternative is even searched for; a thin margin (Warning) does not
        // by itself force a search, only the trigger's own significance does.
        const bool material = isMaterialChange(trigger, policy_);
        const std::optional<Seconds> lastReplan = lastReplanAtSec(mission.id());
        const bool throttled = lastReplan.has_value() && policy_.minimumReplanIntervalSec > 0.0 &&
                               (nowSec - *lastReplan) < policy_.minimumReplanIntervalSec;

        if (!material || throttled || trigger.kind == ChangeKind::EmergencyResolved) {
            decision.decision = ReplanDecisionType::Continue;
            decision.reason = ReplanReason::None;
            ++metrics_.planRetentions;
        } else {
            ++metrics_.optionalReplans;
            const std::size_t startLeg =
                remainingLegStartIndex(mission.currentLeg(), mission.progressOnLeg());
            requireThat(startLeg < mission.currentRoute().size(),
                        "DynamicReplanner::evaluate: Valid/Warning but no route remains -- "
                        "should have been Completed");
            const EntityNodeIndex index = buildEntityNodeIndex(graph);
            const auto satelliteIt = index.satelliteNodes.find(emergency.subject().value());
            requireThat(satelliteIt != index.satelliteNodes.end(),
                        "DynamicReplanner::evaluate: emergency's subject satellite is not a "
                        "node in this graph");
            const NodeId sourceNode = currentRouteNode(graph, mission.currentRoute(), startLeg);

            const ParetoFront rawFront =
                generateReplanFront(graph, model, sourceNode, satelliteIt->second, nowSec,
                                   vehicle.deltaVRemainingKms(), emergency.deadlineSec(),
                                   mission.vehicle(), mission.emergency(), nextPlanId);
            metrics_.candidatePlansGenerated +=
                static_cast<int>(rawFront.solutions.size() + rawFront.dominated.size());
            const ParetoFront front = excludeSolutionsUsingEdge(rawFront, trigger.invalidatedEdge);

            const std::optional<RecommendedPlan> recommended =
                selectRecommendedPlan(front, profile);
            if (!recommended.has_value()) {
                // Current plan is still feasible (validity said so); simply nothing better
                // was found. Retain, do not abort -- there is nothing wrong with staying.
                decision.decision = ReplanDecisionType::Continue;
                decision.reason = ReplanReason::None;
                ++metrics_.planRetentions;
            } else {
                // Fair comparison: score both the retained option and every new candidate
                // against SHARED bounds (the same global-normalisation fix
                // ASSIGNMENT_MODEL.md §7 built for Phase 6's assignment layer, reused here
                // for a two-option comparison instead of a whole matrix).
                std::vector<ObjectiveVector> combined;
                combined.reserve(front.solutions.size() + 1);
                combined.push_back(validity.remaining.cost);
                for (const MissionPlan& sol : front.solutions) combined.push_back(sol.totalCost());
                const ObjectiveVector ideal = computeIdeal(combined);
                const ObjectiveVector nadir = computeNadir(combined);

                const double oldScore =
                    scorePlan(validity.remaining.cost, ideal, nadir, profile).totalScore;
                const MissionPlan& candidate = front.solutions[recommended->planIndex];
                const double newScore =
                    scorePlan(candidate.totalCost(), ideal, nadir, profile).totalScore;

                if (oldScore - newScore >= policy_.switchMargin) {
                    decision.decision = ReplanDecisionType::Replan;
                    decision.reason = reasonForChangeKind(trigger.kind);
                    decision.newPlan = candidate;
                    ++metrics_.planSwitches;
                    recordReplanTimestamp(mission.id(), nowSec);
                } else {
                    decision.decision = ReplanDecisionType::Continue;
                    decision.reason = ReplanReason::None;
                    ++metrics_.planRetentions;
                }
            }
        }
    }

    finalize(mission, decision, startedAt, nowSec);
    return decision;
}

}  // namespace orca
