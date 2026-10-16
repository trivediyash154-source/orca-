#pragma once

/// @file MissionChange.hpp
/// A structured representation of "something changed" — the input `DynamicReplanner`
/// consumes. Phase 7 brief §9: this exists so the replanner has something to reason about
/// *now*, not because the full simulation event system (Phase 8) is built yet. Nothing
/// here subscribes to anything or fires on a clock; a `MissionChange` is constructed by
/// whoever detected the change (a test, or Phase 8's event engine later) and handed
/// directly to `DynamicReplanner`.
///
/// ## Why fields are optional rather than one union-like struct with everything
///
/// Each `ChangeKind` uses only the fields relevant to it; the rest stay at their defaults.
/// A tagged union (`std::variant`) would be more type-safe, but every consumer would need
/// a `std::visit` for what is, in practice, a handful of `if (kind == ...)` checks in one
/// place (`MissionValidity.cpp`) — the simpler shape was chosen deliberately, matching
/// `MissionChange`'s job as a plain data carrier, not a polymorphic type.

#include <optional>
#include <string>

#include "../core/Types.hpp"
#include "../core/Units.hpp"

namespace orca {

/// What kind of change is being reported. Phase 7 brief §9's list, verbatim.
enum class ChangeKind {
    FuelChanged,
    VehicleUnavailable,
    TargetConditionChanged,
    RiskChanged,
    DebrisDetected,
    EmergencyAdded,
    EmergencyEscalated,
    EmergencyResolved,
    CommunicationLost,
    RouteInvalidated,
    DeadlineChanged,
    ManualReplan,
};

[[nodiscard]] constexpr std::string_view toString(ChangeKind k) noexcept {
    switch (k) {
        case ChangeKind::FuelChanged:           return "FUEL_CHANGED";
        case ChangeKind::VehicleUnavailable:    return "VEHICLE_UNAVAILABLE";
        case ChangeKind::TargetConditionChanged: return "TARGET_CONDITION_CHANGED";
        case ChangeKind::RiskChanged:            return "RISK_CHANGED";
        case ChangeKind::DebrisDetected:         return "DEBRIS_DETECTED";
        case ChangeKind::EmergencyAdded:         return "EMERGENCY_ADDED";
        case ChangeKind::EmergencyEscalated:     return "EMERGENCY_ESCALATED";
        case ChangeKind::EmergencyResolved:      return "EMERGENCY_RESOLVED";
        case ChangeKind::CommunicationLost:      return "COMMUNICATION_LOST";
        case ChangeKind::RouteInvalidated:       return "ROUTE_INVALIDATED";
        case ChangeKind::DeadlineChanged:        return "DEADLINE_CHANGED";
        case ChangeKind::ManualReplan:           return "MANUAL_REPLAN";
    }
    return "UNKNOWN";
}

/// One reported change. `atSec` is when it was detected (simulation time); the fields
/// below it are populated only for the `ChangeKind`s that need them -- see each field's
/// comment for which.
struct MissionChange {
    ChangeKind kind;
    Seconds atSec = 0.0;

    /// `RouteInvalidated`, `DebrisDetected`: the specific edge no longer usable, if known.
    /// `std::nullopt` means "the current route in general," not a single leg.
    std::optional<EdgeId> invalidatedEdge;

    /// `EmergencyAdded`: the new emergency's id, for a global-replan trigger to look it up.
    std::optional<EmergencyId> newEmergency;

    /// How significant this change is, as a fraction the caller supplies -- per-kind
    /// meaning: `FuelChanged`/`RiskChanged` -- the magnitude of the change as a fraction of
    /// the vehicle's total delta-v budget / an absolute risk delta in [0,1];
    /// `TargetConditionChanged`/`DeadlineChanged` -- fraction of the original survival
    /// window consumed by the change; other kinds -- left `std::nullopt`, treated as
    /// always material (brief §12's "a new catastrophic emergency" is not a matter of
    /// degree the way a fuel reading is). `DynamicReplanner` compares this against
    /// `ReplanPolicy`'s `material*Fraction` thresholds before spending time on an OPTIONAL
    /// re-evaluation -- never on a MANDATORY one, which always runs regardless.
    std::optional<double> magnitudeFraction;

    /// Free-text detail, surfaced verbatim in `ReplanExplanation` -- e.g. "debris field
    /// DBR-042 crosses leg 2's transfer corridor." Never parsed by the replanner itself,
    /// only carried through for the eventual UI/log consumer (Phase 7 brief §26).
    std::string detail;
};

}  // namespace orca
