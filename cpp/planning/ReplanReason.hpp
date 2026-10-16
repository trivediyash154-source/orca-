#pragma once

/// @file ReplanReason.hpp
/// Why a replan decision was made — distinct from `ChangeKind` (`MissionChange.hpp`).
/// A `ChangeKind` is what was *reported*; a `ReplanReason` is what `MissionValidity`
/// concluded *because of* it (and, for the `None` case, in spite of it). The two are not
/// the same enum for the same reason `ConstraintKind` (why a plan is infeasible) is not
/// `EmergencyKind` (what went wrong aboard a satellite) — one is a diagnosis, the other is
/// an event. Phase 7 brief §10's list, verbatim, plus `None` for "no replan needed."

#include <string_view>

namespace orca {

enum class ReplanReason {
    None,
    RouteInvalidated,
    VehicleDegraded,
    TargetDegraded,
    FuelConstraint,
    DeltaVConstraint,
    RiskConstraint,
    NewHigherPriorityEmergency,
    DeadlineThreat,
    VehicleUnavailable,
    EnvironmentChanged,
    CommunicationLoss,
    ManualReplan,
};

[[nodiscard]] constexpr std::string_view toString(ReplanReason r) noexcept {
    switch (r) {
        case ReplanReason::None:                        return "NONE";
        case ReplanReason::RouteInvalidated:             return "ROUTE_INVALIDATED";
        case ReplanReason::VehicleDegraded:              return "VEHICLE_DEGRADED";
        case ReplanReason::TargetDegraded:               return "TARGET_DEGRADED";
        case ReplanReason::FuelConstraint:                return "FUEL_CONSTRAINT";
        case ReplanReason::DeltaVConstraint:              return "DELTA_V_CONSTRAINT";
        case ReplanReason::RiskConstraint:                return "RISK_CONSTRAINT";
        case ReplanReason::NewHigherPriorityEmergency:    return "NEW_HIGHER_PRIORITY_EMERGENCY";
        case ReplanReason::DeadlineThreat:                return "DEADLINE_THREAT";
        case ReplanReason::VehicleUnavailable:            return "VEHICLE_UNAVAILABLE";
        case ReplanReason::EnvironmentChanged:            return "ENVIRONMENT_CHANGED";
        case ReplanReason::CommunicationLoss:             return "COMMUNICATION_LOSS";
        case ReplanReason::ManualReplan:                  return "MANUAL_REPLAN";
    }
    return "UNKNOWN";
}

}  // namespace orca
