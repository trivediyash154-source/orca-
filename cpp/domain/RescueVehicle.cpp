#include "RescueVehicle.hpp"

#include <format>
#include <utility>

#include "../core/Constants.hpp"
#include "../core/Validation.hpp"

namespace orca {

bool isValidTransition(VehicleStatus from, VehicleStatus next) noexcept {
    if (from == next) {
        return true;  // idempotent
    }
    if (next == VehicleStatus::Disabled) {
        return from != VehicleStatus::Disabled;  // anything can fail, once
    }
    switch (from) {
        case VehicleStatus::Idle:
            return next == VehicleStatus::Assigned;
        case VehicleStatus::Assigned:
            return next == VehicleStatus::Transferring || next == VehicleStatus::Idle;
        case VehicleStatus::Transferring:
            return next == VehicleStatus::OnStation || next == VehicleStatus::Returning;
        case VehicleStatus::OnStation:
            return next == VehicleStatus::Returning || next == VehicleStatus::Transferring;
        case VehicleStatus::Returning:
            return next == VehicleStatus::Idle;
        case VehicleStatus::Disabled:
            return false;  // terminal
    }
    return false;
}

RescueVehicle::RescueVehicle(VehicleId id,
                             std::string name,
                             Orbit orbit,
                             KmPerSec deltaVBudgetKms,
                             int rescueCapacity,
                             Subsystems health)
    : id_(id),
      name_(std::move(name)),
      orbit_(orbit),
      health_(health),
      deltaVBudgetKms_(deltaVBudgetKms),
      deltaVRemainingKms_(deltaVBudgetKms),
      rescueCapacity_(rescueCapacity) {
    requireThat(!name_.empty(),
                std::format("RescueVehicle {}: name must not be empty", toString(id_)));
    requireThat(deltaVBudgetKms_ > 0.0,
                std::format("RescueVehicle {}: deltaVBudgetKms ({:.4f}) must be positive",
                            toString(id_), deltaVBudgetKms_));
    requireThat(rescueCapacity_ > 0,
                std::format("RescueVehicle {}: rescueCapacity ({}) must be positive",
                            toString(id_), rescueCapacity_));
    requireThat(health_.allInUnitInterval(),
                std::format("RescueVehicle {}: all subsystem health values must lie in [0,1]",
                            toString(id_)));
}

Vector3 RescueVehicle::positionAtKm(Seconds timeSec) const noexcept {
    return orbit_.positionAtKm(timeSec);
}

Fraction RescueVehicle::fuelFraction() const noexcept {
    return deltaVRemainingKms_ / deltaVBudgetKms_;
}

bool RescueVehicle::isAvailable() const noexcept {
    const bool free = status_ == VehicleStatus::Idle || status_ == VehicleStatus::OnStation;
    return free && deltaVRemainingKms_ > kEpsilon;
}

bool RescueVehicle::canAfford(KmPerSec requiredKms) const noexcept {
    return requiredKms <= deltaVRemainingKms_;
}

void RescueVehicle::consumeDeltaV(KmPerSec amountKms) {
    requireThat(amountKms >= 0.0,
                std::format("RescueVehicle {}: delta-v spend ({:.4f} km/s) must be "
                            "non-negative; refuelling is not modelled",
                            toString(id_), amountKms));
    requireThat(canAfford(amountKms),
                std::format("RescueVehicle {}: delta-v spend ({:.4f} km/s) exceeds "
                            "remaining budget ({:.4f} km/s)",
                            toString(id_), amountKms, deltaVRemainingKms_));
    deltaVRemainingKms_ -= amountKms;
}

void RescueVehicle::setStatus(VehicleStatus next) {
    requireThat(canTransitionTo(next),
                std::format("RescueVehicle {}: illegal status transition {} -> {}",
                            toString(id_), toString(status_), toString(next)));
    status_ = next;
}

bool RescueVehicle::canTransitionTo(VehicleStatus next) const noexcept {
    return isValidTransition(status_, next);
}

void RescueVehicle::assignMission(MissionId mission) noexcept {
    activeMission_ = mission;
}

void RescueVehicle::clearMission() noexcept {
    activeMission_.reset();
}

}  // namespace orca
