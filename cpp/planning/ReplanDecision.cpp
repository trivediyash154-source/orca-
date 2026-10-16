#include "ReplanDecision.hpp"

#include <format>

namespace orca {

namespace {

/// One sentence per `ReplanReason` -- table-driven, not generated. Extends `toString()`'s
/// wire name into the "Decision rationale" line Phase 7 brief §26's example shows.
std::string_view rationaleFor(ReplanReason reason) {
    switch (reason) {
        case ReplanReason::None:
            return "The current route remains within every constraint and no materially "
                  "better alternative was found.";
        case ReplanReason::RouteInvalidated:
            return "The previous route is no longer usable.";
        case ReplanReason::VehicleDegraded:
            return "The assigned vehicle's condition has degraded.";
        case ReplanReason::TargetDegraded:
            return "The emergency's condition has changed materially.";
        case ReplanReason::FuelConstraint:
            return "The vehicle's remaining fuel is no longer sufficient for the current route.";
        case ReplanReason::DeltaVConstraint:
            return "The vehicle's remaining delta-v is no longer sufficient for the current route.";
        case ReplanReason::RiskConstraint:
            return "The previous route violated the risk threshold.";
        case ReplanReason::NewHigherPriorityEmergency:
            return "A higher-priority emergency now outweighs continuing this mission.";
        case ReplanReason::DeadlineThreat:
            return "The current route no longer arrives before the emergency's deadline.";
        case ReplanReason::VehicleUnavailable:
            return "The assigned vehicle is no longer available.";
        case ReplanReason::EnvironmentChanged:
            return "The operating environment changed materially.";
        case ReplanReason::CommunicationLoss:
            return "Communication with the vehicle was lost.";
        case ReplanReason::ManualReplan:
            return "A replan was manually requested.";
    }
    return "Unknown reason.";
}

std::string formatCost(std::string_view label, const ObjectiveVector& cost,
                       Seconds arrivalSec, Seconds nowSec) {
    return std::format("{}: Risk = {:.4f}, ETA = {:.2f} min", label, cost.risk,
                       (arrivalSec - nowSec) / 60.0);
}

}  // namespace

std::string explain(const ReplanDecision& decision) {
    std::string out;
    out += std::format("Decision:\n{}\n\n", toString(decision.decision));
    out += std::format("Reason:\n{}\n\n", rationaleFor(decision.reason));

    out += formatCost("Previous", decision.previousRemainingCost, decision.previousArrivalSec,
                      decision.decidedAtSec);
    out += "\n";

    if (decision.newPlan.has_value()) {
        out += formatCost("New", decision.newPlan->totalCost(),
                          decision.newPlan->arrivalTimeSec(), decision.decidedAtSec);
        out += "\n";
    }

    if (decision.decision == ReplanDecisionType::Abort) {
        out += std::format("\nAbort reason:\n{}\n", toString(decision.abortReason));
    }

    if (!decision.detail.empty()) {
        out += std::format("\nDetail:\n{}\n", decision.detail);
    }

    out += std::format("\nDecision rationale:\n{}\n", rationaleFor(decision.reason));
    return out;
}

}  // namespace orca
