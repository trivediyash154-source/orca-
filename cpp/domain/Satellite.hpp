#pragma once

/// @file Satellite.hpp
/// A tracked satellite — the thing that fails and needs rescuing.
///
/// ## No stored position
///
/// `Satellite` holds an `Orbit` and derives position from it via `positionAtKm(t)`. It
/// does not cache a position, and there is no `update(dt)` that advances one. Position is
/// a pure function of orbit and time (Orbit.hpp), so caching it would create a second
/// piece of state that can disagree with the first, and an `update()` that someone forgets
/// to call.
///
/// ## Status and health are coupled, in one direction
///
/// Health drives status, never the reverse. `applyDamage()` lowers a subsystem and then
/// lets status follow the thresholds. `setStatus()` exists for the transitions health
/// cannot express — rescue and loss — and validates against the documented state machine.

#include <optional>
#include <string>

#include "../core/Types.hpp"
#include "../core/Units.hpp"
#include "../core/Vector3.hpp"
#include "Orbit.hpp"
#include "Subsystems.hpp"

namespace orca {

/// Subsystem health thresholds that drive satellite status transitions.
///
/// These are **modelling choices, not physical facts**, so they carry defaults here and
/// move to `configs/` when the scenario loader lands in Phase 14. They are named rather
/// than inlined so that the move is a change of source, not a hunt for literals.
struct HealthThresholds {
    double warningBelow = 0.6;   ///< worst subsystem below this -> Warning
    double criticalBelow = 0.3;  ///< worst subsystem below this -> Critical
    double failedAtOrBelow = 0.0;  ///< worst subsystem at or below this -> Failed
};

class Satellite {
public:
    /// @throws std::invalid_argument if `name` is empty, `fuelFraction` is outside [0,1],
    ///         or any subsystem health is outside [0,1].
    Satellite(SatelliteId id,
              std::string name,
              Orbit orbit,
              Fraction fuelFraction = 1.0,
              Subsystems health = Subsystems::nominal());

    // --- Identity and state -------------------------------------------------

    [[nodiscard]] SatelliteId id() const noexcept { return id_; }
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] const Orbit& orbit() const noexcept { return orbit_; }
    [[nodiscard]] SatelliteStatus status() const noexcept { return status_; }
    [[nodiscard]] const Subsystems& health() const noexcept { return health_; }
    [[nodiscard]] Fraction fuelFraction() const noexcept { return fuelFraction_; }

    /// The emergency this satellite is the subject of, if any.
    [[nodiscard]] const std::optional<EmergencyId>& activeEmergency() const noexcept {
        return activeEmergency_;
    }

    // --- Derived ------------------------------------------------------------

    /// ECI position at `timeSec`. Derived from the orbit; never cached.
    [[nodiscard]] Vector3 positionAtKm(Seconds timeSec) const noexcept;

    /// Functioning normally enough to need no intervention.
    [[nodiscard]] bool isOperational() const noexcept;

    /// Degraded to the point of needing attention, but not yet failed.
    [[nodiscard]] bool isCritical() const noexcept;

    /// Failed and awaiting rescue. This is the predicate the planner keys on.
    [[nodiscard]] bool requiresRescue() const noexcept;

    /// No further transitions are possible.
    [[nodiscard]] bool isTerminal() const noexcept;

    // --- Mutation -----------------------------------------------------------

    /// Reduce a subsystem's health by `amount` and update status accordingly.
    ///
    /// Damage is clamped at zero rather than going negative, and status advances only
    /// along transitions the state machine permits — so damaging an already-rescued
    /// satellite lowers its health without resurrecting it into a failed state.
    void applyDamage(Subsystem subsystem, double amount,
                     const HealthThresholds& thresholds = {});

    /// Force a status transition.
    ///
    /// @throws std::invalid_argument if the transition is not permitted by the state
    ///         machine in DATA_MODEL.md §3.1. Rescue and loss are the transitions health
    ///         alone cannot express, which is why this exists.
    void setStatus(SatelliteStatus next);

    /// True when `next` is a legal transition from the current status.
    [[nodiscard]] bool canTransitionTo(SatelliteStatus next) const noexcept;

    /// @throws std::invalid_argument if `fraction` is outside [0,1].
    void setFuelFraction(Fraction fraction);

    void assignEmergency(EmergencyId emergency) noexcept;
    void clearEmergency() noexcept;

private:
    /// Move status to whatever the current health implies, if the state machine allows it.
    void refreshStatusFromHealth(const HealthThresholds& thresholds) noexcept;

    SatelliteId id_;
    std::string name_;
    Orbit orbit_;
    SatelliteStatus status_ = SatelliteStatus::Nominal;
    Subsystems health_;
    Fraction fuelFraction_;
    std::optional<EmergencyId> activeEmergency_;
};

/// True when `next` is reachable from `from` under DATA_MODEL.md §3.1.
///
/// Free function so the state machine can be tested without constructing a satellite.
[[nodiscard]] bool isValidTransition(SatelliteStatus from, SatelliteStatus next) noexcept;

}  // namespace orca
