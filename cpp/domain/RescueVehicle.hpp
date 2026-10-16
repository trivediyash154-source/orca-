#pragma once

/// @file RescueVehicle.hpp
/// A rescue vehicle — the scarce resource the whole optimization problem is about.
///
/// ## Delta-v is the real quantity; fuel fraction is the gauge
///
/// The vehicle stores delta-v in km/s, a physical quantity. `fuelFraction()` reports
/// `remaining / budget` — how full the tank is, in [0,1].
///
/// Do not confuse that with `ObjectiveVector::fuel`, which is a *plan's cost*:
/// `deltaV_required / budget`. Both are fractions of the same denominator, so they are
/// directly comparable for a given vehicle — `canAfford()` is exactly the test that a
/// plan's fuel cost does not exceed the current fuel fraction.
///
/// Neither is comparable **across** vehicles with different budgets. RV-02 spending 13.6%
/// and RV-03 spending 13.6% are different quantities of delta-v. This is the single
/// easiest number in ORCA to misread (ARCHITECTURE_REVIEW.md §14.3), which is why the
/// accessor is named after the ratio rather than after "fuel".
///
/// ## Delta-v never increases
///
/// There is no refuelling in v1 (SIMULATION_MODEL.md §8, item 10), so
/// `deltaVRemainingKms` is monotonically non-increasing. `consumeDeltaV()` is the only
/// mutator and it enforces this.

#include <optional>
#include <string>

#include "../core/Types.hpp"
#include "../core/Units.hpp"
#include "../core/Vector3.hpp"
#include "Orbit.hpp"
#include "Subsystems.hpp"

namespace orca {

class RescueVehicle {
public:
    /// @throws std::invalid_argument if `name` is empty, the delta-v budget is not
    ///         positive, or `rescueCapacity` is not positive.
    RescueVehicle(VehicleId id,
                  std::string name,
                  Orbit orbit,
                  KmPerSec deltaVBudgetKms,
                  int rescueCapacity = 1,
                  Subsystems health = Subsystems::nominal());

    // --- Identity and state -------------------------------------------------

    [[nodiscard]] VehicleId id() const noexcept { return id_; }
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] const Orbit& orbit() const noexcept { return orbit_; }
    [[nodiscard]] VehicleStatus status() const noexcept { return status_; }
    [[nodiscard]] const Subsystems& health() const noexcept { return health_; }
    [[nodiscard]] KmPerSec deltaVBudgetKms() const noexcept { return deltaVBudgetKms_; }
    [[nodiscard]] KmPerSec deltaVRemainingKms() const noexcept { return deltaVRemainingKms_; }
    [[nodiscard]] int rescueCapacity() const noexcept { return rescueCapacity_; }

    [[nodiscard]] const std::optional<MissionId>& activeMission() const noexcept {
        return activeMission_;
    }

    // --- Derived ------------------------------------------------------------

    /// ECI position at `timeSec`. Derived from the orbit; never cached.
    ///
    /// Note this is the vehicle's *orbital* position. While transferring, its true
    /// position lies along the mission trajectory rather than on this orbit; that is
    /// mission state and belongs to `MissionExecutor` in Phase 12, not here.
    [[nodiscard]] Vector3 positionAtKm(Seconds timeSec) const noexcept;

    /// Remaining delta-v as a fraction of this vehicle's own budget, in [0,1].
    [[nodiscard]] Fraction fuelFraction() const noexcept;

    /// True when this vehicle could be dispatched: not busy, not disabled, has delta-v.
    [[nodiscard]] bool isAvailable() const noexcept;

    /// True when `requiredKms` is within the remaining budget.
    [[nodiscard]] bool canAfford(KmPerSec requiredKms) const noexcept;

    // --- Mutation -----------------------------------------------------------

    /// Spend delta-v.
    ///
    /// @throws std::invalid_argument if `amountKms` is negative, or exceeds the
    ///         remaining budget. Overspending is rejected rather than clamped: a vehicle
    ///         that silently completed an unaffordable transfer would make the
    ///         feasibility guarantee in OPTIMIZATION_MODEL.md §9 meaningless.
    void consumeDeltaV(KmPerSec amountKms);

    /// @throws std::invalid_argument if the transition is not permitted.
    void setStatus(VehicleStatus next);

    [[nodiscard]] bool canTransitionTo(VehicleStatus next) const noexcept;

    void assignMission(MissionId mission) noexcept;
    void clearMission() noexcept;

private:
    VehicleId id_;
    std::string name_;
    Orbit orbit_;
    VehicleStatus status_ = VehicleStatus::Idle;
    Subsystems health_;
    KmPerSec deltaVBudgetKms_;
    KmPerSec deltaVRemainingKms_;
    int rescueCapacity_;
    std::optional<MissionId> activeMission_;
};

/// Vehicle status transition table.
///
/// This state machine was not specified in Phase 0 — DATA_MODEL.md §3.2 listed the states
/// but gave no diagram. It is defined here and has been added to DATA_MODEL.md.
///
///     Idle         -> Assigned
///     Assigned     -> Transferring | Idle          (dispatch, or cancellation)
///     Transferring -> OnStation | Returning
///     OnStation    -> Returning | Transferring     (onward tasking)
///     Returning    -> Idle
///     any          -> Disabled                     (terminal)
[[nodiscard]] bool isValidTransition(VehicleStatus from, VehicleStatus next) noexcept;

}  // namespace orca
