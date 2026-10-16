#pragma once

/// @file Event.hpp
/// One scheduled occurrence in the simulation timeline — Phase 8 brief §12.
///
/// ## Why a flat struct with optional fields, not a class hierarchy
///
/// `EventKind` alone determines which of `satelliteId`/`vehicleId`/`emergencyId`/
/// `missionId`/`debrisId` are populated for a given event -- exactly the same "one shape,
/// kind decides which fields matter" choice `MissionChange.hpp` already made for the same
/// reason (Phase 7): every consumer is one `switch` in `EventDispatcher.cpp`, not a
/// `std::visit` over a polymorphic hierarchy that would need one for a handful of fields.

#include <optional>
#include <string>
#include <string_view>

#include "../core/Types.hpp"
#include "../core/Units.hpp"

namespace orca {

/// Phase 8 brief §12's list, verbatim.
enum class EventKind {
    SatelliteFailure,
    PropulsionFailure,
    CommunicationLoss,
    FuelCritical,
    DebrisAlert,
    EmergencyEscalation,
    EmergencyResolved,
    MissionStart,
    MissionReplan,
    Rendezvous,
    RescueStart,
    RescueComplete,
    MissionFailure,
    VehicleStateChange,
};

[[nodiscard]] constexpr std::string_view toString(EventKind k) noexcept {
    switch (k) {
        case EventKind::SatelliteFailure:   return "SATELLITE_FAILURE";
        case EventKind::PropulsionFailure:  return "PROPULSION_FAILURE";
        case EventKind::CommunicationLoss:  return "COMMUNICATION_LOSS";
        case EventKind::FuelCritical:        return "FUEL_CRITICAL";
        case EventKind::DebrisAlert:         return "DEBRIS_ALERT";
        case EventKind::EmergencyEscalation: return "EMERGENCY_ESCALATION";
        case EventKind::EmergencyResolved:   return "EMERGENCY_RESOLVED";
        case EventKind::MissionStart:        return "MISSION_START";
        case EventKind::MissionReplan:       return "MISSION_REPLAN";
        case EventKind::Rendezvous:          return "RENDEZVOUS";
        case EventKind::RescueStart:         return "RESCUE_START";
        case EventKind::RescueComplete:      return "RESCUE_COMPLETE";
        case EventKind::MissionFailure:      return "MISSION_FAILURE";
        case EventKind::VehicleStateChange:  return "VEHICLE_STATE_CHANGE";
    }
    return "UNKNOWN";
}

/// Same-timestamp processing order — Phase 8 brief §17: "define a stable priority... do
/// not allow registration order to determine behavior." Lower value processes first.
/// Environmental facts are established before anything reacts to them; replanning
/// consumes the state changes above it; completion notifications are last because they
/// summarise what already happened this tick, not what causes further reaction within it.
///
///   1. Environmental events        -- DebrisAlert
///   2. Vehicle/system state changes -- SatelliteFailure, PropulsionFailure,
///      CommunicationLoss, FuelCritical, VehicleStateChange
///   3. Mission milestones          -- MissionStart, Rendezvous, RescueStart
///   4. Replanning triggers         -- MissionReplan, EmergencyEscalation
///   5. Completion notifications    -- RescueComplete, MissionFailure, EmergencyResolved
[[nodiscard]] constexpr int eventPriorityTier(EventKind k) noexcept {
    switch (k) {
        case EventKind::DebrisAlert:
            return 1;
        case EventKind::SatelliteFailure:
        case EventKind::PropulsionFailure:
        case EventKind::CommunicationLoss:
        case EventKind::FuelCritical:
        case EventKind::VehicleStateChange:
            return 2;
        case EventKind::MissionStart:
        case EventKind::Rendezvous:
        case EventKind::RescueStart:
            return 3;
        case EventKind::MissionReplan:
        case EventKind::EmergencyEscalation:
            return 4;
        case EventKind::RescueComplete:
        case EventKind::MissionFailure:
        case EventKind::EmergencyResolved:
            return 5;
    }
    return 6;
}

/// One event. `id` is assigned sequentially by `EventQueue::schedule()` -- never a memory
/// address, never random (Phase 8 brief §14) -- so the same scenario always produces the
/// same IDs in the same order, which is what makes logs and same-timestamp ordering
/// reproducible.
struct Event {
    EventId id;
    Seconds timestampSec = 0.0;
    EventKind kind;

    std::optional<SatelliteId> satellite;
    std::optional<VehicleId> vehicle;
    std::optional<EmergencyId> emergency;
    std::optional<MissionId> mission;
    std::optional<DebrisId> debris;
    std::optional<EdgeId> invalidatedEdge;  ///< carried through to a MissionChange, if any

    /// `SatelliteFailure`: what kind of failure and how severe -- carried on the event
    /// itself (rather than re-derived at fire time) because `EmergencyGenerator`'s random
    /// mode draws these from `rng` at SCHEDULING time; re-drawing at fire time would
    /// consume the random stream in an order dependent on which events actually fire,
    /// breaking reproducibility the moment a scenario's timeline changed at all.
    std::optional<EmergencyKind> emergencyKind;
    std::optional<Severity> severity;

    /// `SatelliteFailure`, `EmergencyEscalation`: the survival window a newly created (or
    /// re-scored) emergency should use, seconds from detection.
    std::optional<Seconds> survivalWindowSec;

    /// Free-text detail, never parsed -- the same "carried through, not consumed"
    /// convention `MissionChange.hpp`'s own `detail` field uses.
    std::string detail;
};

}  // namespace orca
