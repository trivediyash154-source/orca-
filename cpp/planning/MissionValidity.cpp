#include "MissionValidity.hpp"

#include <algorithm>
#include <format>

#include "../core/Validation.hpp"

namespace orca {

namespace {

constexpr Seconds kEffectivelyNoDeadlineSec = 1.0e18;
constexpr KmPerSec kEffectivelyUnlimitedDeltaVKms = 1.0e6;

/// The replanning-from-current-state constraints convention -- feasibility (the
/// cumulative-fuel hard constraint) is checked against what the vehicle has LEFT
/// (`maxDeltaVKms = deltaVRemainingKms`), but `deltaVBudgetKms` stays the vehicle's TRUE
/// lifetime total, never the remaining amount.
///
/// This is a deliberate DIVERGENCE from `CandidateGeneration.cpp`'s `makeSearchConstraints
/// ()`, which sets both to `deltaVRemainingKms()` -- correct there ONLY because Phase 6
/// exclusively plans fresh vehicles, where `remaining == budget` and the distinction is
/// invisible. `TransferModel.cpp` feeds `constraints.deltaVBudgetKms` directly into
/// `TransferRiskInputs.deltaVBudgetKms` (the risk model's "how much of my TOTAL capability
/// does this burn consume" denominator, per that struct's OWN separate `deltaVRemainingKms`
/// field -- the risk model was already designed to keep these distinct). Collapsing them
/// here, mirroring Phase 6's fresh-vehicle-only convention, made an ALREADY-PARTIALLY-SPENT
/// vehicle's routine transfer look artificially severe: a burn using 95% of a shrunken
/// "remaining-as-budget" denominator scores as dangerously risky, when the SAME burn
/// against the vehicle's real 5.0 km/s budget was unremarkable (~0.15 of it). Found by this
/// file's own `ThinDeltaVMarginIsWarningNotInvalid` test unexpectedly reporting
/// `RiskConstraint` instead of a thin-margin `Warning`. `DynamicReplanner.cpp`'s search
/// path (via `SearchConstraints`, which has only one delta-v field and cannot represent
/// "remaining" and "total" separately) still inherits Phase 6's original convention -- a
/// known, explicitly documented limitation (`DYNAMIC_REPLANNING.md`), not silently
/// carried over.
TransferConstraints makeRemainingRouteConstraints(KmPerSec deltaVRemainingKms,
                                                   KmPerSec deltaVBudgetKms,
                                                   Seconds latestArrivalSec) {
    TransferConstraints c;
    c.maxDeltaVKms = deltaVRemainingKms;
    c.deltaVBudgetKms = deltaVBudgetKms;
    c.availableFuelFraction = 1.0;
    // Unbounded, matching CandidateGeneration.cpp's makeSearchConstraints() EXACTLY --
    // not the 6-hour struct default. A route the original candidate search accepted
    // (search itself never wait-capped) must not appear spuriously invalid here purely
    // because this file used a different wait policy than the one that planned it in the
    // first place (found via this file's own smoke testing: a freshly-launched mission's
    // OWN route, re-evaluated seconds later with no real change, was reported Invalid).
    c.maxWaitSec = 1.0e13;
    c.maxRisk = 1.0;  // unbounded here -- ReplanPolicy::maxAcceptableRisk is the policy
                      // ceiling applied separately in evaluateMissionValidity(), the same
                      // "Group 1 checks decide permanently, this layer decides on top"
                      // split SearchConstraints itself documents.
    c.latestArrivalSec = latestArrivalSec;
    return c;
}

/// The pure walk, with NO diagnosis on failure -- `infeasibilityReason` is always
/// `std::nullopt` in whatever this returns. Diagnosis is layered on top by
/// `diagnoseRemainingRouteInfeasibility()`, which calls THIS function (never the public
/// `evaluateRemainingRouteCost()`) with relaxed constraints. Keeping the two separate is
/// what prevents infinite recursion: the public function used to call the diagnosis
/// helper on every failure, and the diagnosis helper's own relaxed-constraint probes could
/// themselves fail and try to diagnose again -- caught by a stack-overflow crash during
/// this file's own smoke testing before any test made it into the permanent suite.
RemainingRouteEvaluation walkRoute(const MissionGraph& graph, const TransferModel& model,
                                   const std::vector<EdgeId>& route, std::size_t startLegIndex,
                                   Seconds nowSec, const TransferConstraints& constraints,
                                   std::optional<EdgeId> invalidatedEdge = std::nullopt,
                                   std::vector<Seconds>* legArrivalTimesOut = nullptr) {
    RemainingRouteEvaluation result;
    result.arrivalTimeSec = nowSec;
    if (legArrivalTimesOut != nullptr) {
        legArrivalTimesOut->clear();
    }
    if (startLegIndex == route.size()) {
        result.feasible = true;
        return result;
    }

    ObjectiveVector cumulative;
    Seconds departureTimeSec = nowSec;
    for (std::size_t i = startLegIndex; i < route.size(); ++i) {
        if (invalidatedEdge.has_value() && route[i] == *invalidatedEdge) {
            return result;  // externally invalidated -- feasible stays false, no
                            // TransferModel call needed for this leg at all
        }
        const GraphEdge& edge = graph.edge(route[i]);
        const TransferResult leg = model.evaluate(graph.node(edge.from()).orbit(),
                                                   graph.node(edge.to()).orbit(),
                                                   departureTimeSec, constraints);
        if (!leg.feasible) {
            return result;  // feasible stays false, no diagnosis performed here
        }
        cumulative = compose(cumulative, leg.objectives());
        if (cumulative.fuel > 1.0) {
            return result;
        }
        departureTimeSec = leg.geometry.arrivalTimeSec;
        if (legArrivalTimesOut != nullptr) {
            legArrivalTimesOut->push_back(departureTimeSec);
        }
    }

    result.feasible = true;
    result.cost = cumulative;
    result.arrivalTimeSec = departureTimeSec;
    return result;
}

/// 2-tier relaxation diagnosis, the same pattern `CandidateGeneration.cpp`'s
/// `diagnoseInfeasibility()` uses -- reused by calling `walkRoute()` (never
/// `evaluateRemainingRouteCost()`) with relaxed constraints.
std::optional<ConstraintKind> diagnoseRemainingRouteInfeasibility(
    const MissionGraph& graph, const TransferModel& model, const std::vector<EdgeId>& route,
    std::size_t startLegIndex, Seconds nowSec, KmPerSec deltaVRemainingKms,
    KmPerSec deltaVBudgetKms, Seconds latestArrivalSec,
    std::optional<EdgeId> invalidatedEdge) {
    // An externally invalidated edge is not a resource/deadline problem at all -- neither
    // relaxation would ever make it usable again, so it is diagnosed as Unreachable
    // directly rather than wastefully probing two relaxations that cannot possibly help.
    if (invalidatedEdge.has_value()) {
        for (std::size_t i = startLegIndex; i < route.size(); ++i) {
            if (route[i] == *invalidatedEdge) {
                return ConstraintKind::Unreachable;
            }
        }
    }

    const TransferConstraints relaxedDeadline = makeRemainingRouteConstraints(
        deltaVRemainingKms, deltaVBudgetKms, kEffectivelyNoDeadlineSec);
    if (walkRoute(graph, model, route, startLegIndex, nowSec, relaxedDeadline, invalidatedEdge)
            .feasible) {
        return ConstraintKind::SurvivalWindow;
    }

    const TransferConstraints relaxedDeltaV = makeRemainingRouteConstraints(
        kEffectivelyUnlimitedDeltaVKms, kEffectivelyUnlimitedDeltaVKms, latestArrivalSec);
    if (walkRoute(graph, model, route, startLegIndex, nowSec, relaxedDeltaV, invalidatedEdge)
            .feasible) {
        return ConstraintKind::DeltaVBudget;
    }

    return ConstraintKind::Unreachable;
}

}  // namespace

std::size_t remainingLegStartIndex(std::size_t currentLeg, double progressOnLeg) noexcept {
    return progressOnLeg > 0.0 ? currentLeg + 1 : currentLeg;
}

CommitmentPoint commitmentPoint(const Mission& mission, const MissionGraph& graph,
                                const TransferModel& model, Seconds nowSec,
                                KmPerSec vehicleBudgetKms) {
    CommitmentPoint cp;
    cp.startLeg = remainingLegStartIndex(mission.currentLeg(), mission.progressOnLeg());
    cp.readyAtSec = nowSec;
    const std::vector<EdgeId>& route = mission.currentRoute();
    if (cp.startLeg == mission.currentLeg() || mission.currentLeg() >= route.size()) {
        return cp;  // not mid-leg: free to change course now
    }

    // Re-time the governing plan up to the in-progress leg exactly as the executor does.
    TransferConstraints timing;
    timing.maxDeltaVKms = 1.0e6;
    timing.deltaVBudgetKms = vehicleBudgetKms > 0.0 ? vehicleBudgetKms : 1.0;
    timing.availableFuelFraction = 1.0;
    timing.maxWaitSec = 1.0e13;
    timing.maxRisk = 1.0;
    timing.latestArrivalSec = 1.0e18;
    timing.maxPathLengthKm = 1.0e12;
    Seconds departure = mission.planDepartureSec();
    for (std::size_t i = 0; i <= mission.currentLeg(); ++i) {
        const GraphEdge& edge = graph.edge(route[i]);
        const TransferResult leg = model.evaluate(graph.node(edge.from()).orbit(),
                                                  graph.node(edge.to()).orbit(), departure, timing);
        if (i == mission.currentLeg()) {
            cp.inProgressEdge = route[i];
            cp.inProgressDepartureSec = departure;
            cp.readyAtSec = std::max(leg.geometry.arrivalTimeSec, nowSec);
            cp.committedDeltaVKms = leg.geometry.deltaVKms;
            cp.committedCost = leg.objectives();
        }
        departure = leg.geometry.arrivalTimeSec;
    }
    return cp;
}

RemainingRouteEvaluation evaluateRemainingRouteCost(const MissionGraph& graph,
                                                     const TransferModel& model,
                                                     const std::vector<EdgeId>& route,
                                                     std::size_t startLegIndex, Seconds nowSec,
                                                     const TransferConstraints& constraints,
                                                     std::optional<EdgeId> invalidatedEdge,
                                                     std::vector<Seconds>* legArrivalTimesOut) {
    requireThat(startLegIndex <= route.size(),
                "evaluateRemainingRouteCost: startLegIndex out of range");

    RemainingRouteEvaluation result = walkRoute(graph, model, route, startLegIndex, nowSec,
                                                constraints, invalidatedEdge, legArrivalTimesOut);
    if (!result.feasible && startLegIndex < route.size()) {
        result.infeasibilityReason = diagnoseRemainingRouteInfeasibility(
            graph, model, route, startLegIndex, nowSec, constraints.maxDeltaVKms,
            constraints.deltaVBudgetKms, constraints.latestArrivalSec, invalidatedEdge);
    }
    return result;
}

MissionValidity evaluateMissionValidity(const Mission& mission, const RescueVehicle& vehicle,
                                        const Emergency& emergency, const MissionGraph& graph,
                                        const TransferModel& model, Seconds nowSec,
                                        const ReplanPolicy& policy,
                                        std::optional<EdgeId> invalidatedEdge) {
    requireThat(!mission.isTerminal(),
                std::format("evaluateMissionValidity: mission {} is already terminal ({}) "
                            "-- a terminal mission must never reach the replanner",
                            toString(mission.id()), toString(mission.state())));

    MissionValidity validity;

    if (emergency.state() == EmergencyState::Resolved) {
        validity.state = ValidityState::Completed;
        validity.detail = "emergency already resolved";
        validity.remaining.feasible = true;
        return validity;
    }

    if (vehicle.status() == VehicleStatus::Disabled) {
        validity.state = ValidityState::Invalid;
        validity.reason = ReplanReason::VehicleUnavailable;
        validity.violatedConstraint = ConstraintKind::NoAsset;
        validity.detail = std::format("vehicle {} is disabled", toString(vehicle.id()));
        return validity;
    }

    const CommitmentPoint cp =
        commitmentPoint(mission, graph, model, nowSec, vehicle.deltaVBudgetKms());
    const std::size_t startLeg = cp.startLeg;
    const TransferConstraints constraints = makeRemainingRouteConstraints(
        std::max(0.0, vehicle.deltaVRemainingKms() - cp.committedDeltaVKms),
        vehicle.deltaVBudgetKms(), emergency.deadlineSec());
    validity.remaining = evaluateRemainingRouteCost(graph, model, mission.currentRoute(),
                                                     startLeg, cp.readyAtSec, constraints,
                                                     invalidatedEdge);

    if (startLeg == mission.currentRoute().size()) {
        // Every leg already flown, structurally at the target -- the executor (Phase 8)
        // is what actually confirms rendezvous; from a pure route-validity standpoint
        // there is nothing left to plan.
        validity.state = ValidityState::Completed;
        validity.detail = "every leg of the current route has already been flown";
        return validity;
    }

    if (!validity.remaining.feasible) {
        validity.state = ValidityState::Invalid;
        validity.violatedConstraint = validity.remaining.infeasibilityReason;
        switch (validity.remaining.infeasibilityReason.value_or(ConstraintKind::Unreachable)) {
            case ConstraintKind::DeltaVBudget:
                validity.reason = ReplanReason::DeltaVConstraint;
                validity.detail = "remaining route exceeds the vehicle's current delta-v";
                break;
            case ConstraintKind::SurvivalWindow:
                validity.reason = ReplanReason::DeadlineThreat;
                validity.detail = "remaining route arrives after the emergency's deadline";
                break;
            default:
                validity.reason = ReplanReason::RouteInvalidated;
                validity.detail = "remaining route is no longer reachable";
                break;
        }
        return validity;
    }

    if (validity.remaining.cost.risk > policy.maxAcceptableRisk) {
        validity.state = ValidityState::Invalid;
        validity.reason = ReplanReason::RiskConstraint;
        validity.violatedConstraint = ConstraintKind::RiskCeiling;
        validity.detail =
            std::format("remaining risk ({:.4f}) exceeds the policy ceiling ({:.4f})",
                        validity.remaining.cost.risk, policy.maxAcceptableRisk);
        return validity;
    }

    // Feasible and within the hard risk ceiling -- Valid or Warning, depending on margin.
    // `cost.fuel` is relative to the vehicle's TRUE total budget (deltaVBudgetKms, per
    // makeRemainingRouteConstraints' file comment), so recovering the raw km/s this route
    // still needs multiplies back by THAT, not by what remains.
    const double deltaVRequired = validity.remaining.cost.fuel * vehicle.deltaVBudgetKms();
    const bool thinDeltaVMargin =
        deltaVRequired > 0.0 &&
        vehicle.deltaVRemainingKms() < deltaVRequired * policy.warningDeltaVMarginFraction;

    const Seconds remainingWindowSec = emergency.remainingWindowSec(nowSec);
    const Seconds remainingTransferSec = validity.remaining.arrivalTimeSec - nowSec;
    const bool thinDeadlineMargin =
        remainingTransferSec > 0.0 &&
        remainingWindowSec < remainingTransferSec * policy.warningDeadlineMarginFraction;

    if (thinDeltaVMargin || thinDeadlineMargin) {
        validity.state = ValidityState::Warning;
        validity.reason =
            thinDeltaVMargin ? ReplanReason::DeltaVConstraint : ReplanReason::DeadlineThreat;
        validity.detail = thinDeltaVMargin
                              ? "remaining delta-v margin is thin"
                              : "remaining time-to-deadline margin is thin";
        return validity;
    }

    validity.state = ValidityState::Valid;
    return validity;
}

}  // namespace orca
