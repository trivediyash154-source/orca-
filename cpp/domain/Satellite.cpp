#include "Satellite.hpp"

#include <format>
#include <utility>

#include "../core/Math.hpp"
#include "../core/Validation.hpp"

namespace orca {

namespace {

/// Position on the degradation ladder Nominal -> Warning -> Critical -> Failed.
/// Rescued and Lost sit off the ladder entirely and return -1.
[[nodiscard]] constexpr int ladderRank(SatelliteStatus s) noexcept {
    switch (s) {
        case SatelliteStatus::Nominal:  return 0;
        case SatelliteStatus::Warning:  return 1;
        case SatelliteStatus::Critical: return 2;
        case SatelliteStatus::Failed:   return 3;
        case SatelliteStatus::Rescued:
        case SatelliteStatus::Lost:     return -1;
    }
    return -1;
}

[[nodiscard]] constexpr SatelliteStatus statusFromRank(int rank) noexcept {
    switch (rank) {
        case 0:  return SatelliteStatus::Nominal;
        case 1:  return SatelliteStatus::Warning;
        case 2:  return SatelliteStatus::Critical;
        default: return SatelliteStatus::Failed;
    }
}

}  // namespace

bool isValidTransition(SatelliteStatus from, SatelliteStatus next) noexcept {
    if (from == next) {
        return true;  // idempotent
    }
    switch (from) {
        case SatelliteStatus::Nominal:
            return next == SatelliteStatus::Warning;
        case SatelliteStatus::Warning:
            return next == SatelliteStatus::Nominal || next == SatelliteStatus::Critical;
        case SatelliteStatus::Critical:
            // Recovery back to Warning is permitted: degradation is not one-way, and a
            // repaired subsystem should be able to walk the status back down.
            return next == SatelliteStatus::Warning || next == SatelliteStatus::Failed;
        case SatelliteStatus::Failed:
            return next == SatelliteStatus::Rescued || next == SatelliteStatus::Lost;
        case SatelliteStatus::Rescued:
        case SatelliteStatus::Lost:
            return false;  // terminal
    }
    return false;
}

Satellite::Satellite(SatelliteId id,
                     std::string name,
                     Orbit orbit,
                     Fraction fuelFraction,
                     Subsystems health)
    : id_(id),
      name_(std::move(name)),
      orbit_(orbit),
      health_(health),
      fuelFraction_(fuelFraction) {
    requireThat(!name_.empty(),
                std::format("Satellite {}: name must not be empty", toString(id_)));
    requireThat(inUnitInterval(fuelFraction_),
                std::format("Satellite {}: fuelFraction ({:.4f}) must lie in [0,1]",
                            toString(id_), fuelFraction_));
    requireThat(health_.allInUnitInterval(),
                std::format("Satellite {}: all subsystem health values must lie in [0,1]",
                            toString(id_)));

    // Status is derived from the supplied health rather than defaulted to Nominal, so a
    // scenario that starts a satellite in a degraded state gets a consistent status
    // without the author having to state it twice.
    refreshStatusFromHealth(HealthThresholds{});
}

Vector3 Satellite::positionAtKm(Seconds timeSec) const noexcept {
    return orbit_.positionAtKm(timeSec);
}

bool Satellite::isOperational() const noexcept {
    return status_ == SatelliteStatus::Nominal || status_ == SatelliteStatus::Warning;
}

bool Satellite::isCritical() const noexcept {
    return status_ == SatelliteStatus::Critical;
}

bool Satellite::requiresRescue() const noexcept {
    return status_ == SatelliteStatus::Failed;
}

bool Satellite::isTerminal() const noexcept {
    return status_ == SatelliteStatus::Rescued || status_ == SatelliteStatus::Lost;
}

bool Satellite::canTransitionTo(SatelliteStatus next) const noexcept {
    return isValidTransition(status_, next);
}

void Satellite::applyDamage(Subsystem subsystem, double amount,
                            const HealthThresholds& thresholds) {
    requireThat(amount >= 0.0,
                std::format("Satellite {}: damage amount ({:.4f}) must be non-negative",
                            toString(id_), amount));
    health_.set(subsystem, health_.get(subsystem) - amount);
    refreshStatusFromHealth(thresholds);
}

void Satellite::setStatus(SatelliteStatus next) {
    requireThat(canTransitionTo(next),
                std::format("Satellite {}: illegal status transition {} -> {}",
                            toString(id_), toString(status_), toString(next)));
    status_ = next;
}

void Satellite::setFuelFraction(Fraction fraction) {
    requireThat(inUnitInterval(fraction),
                std::format("Satellite {}: fuelFraction ({:.4f}) must lie in [0,1]",
                            toString(id_), fraction));
    fuelFraction_ = fraction;
}

void Satellite::assignEmergency(EmergencyId emergency) noexcept {
    activeEmergency_ = emergency;
}

void Satellite::clearEmergency() noexcept {
    activeEmergency_.reset();
}

void Satellite::refreshStatusFromHealth(const HealthThresholds& thresholds) noexcept {
    if (isTerminal()) {
        return;  // rescued or lost satellites do not re-enter the health state machine
    }

    const double worst = health_.worst();
    SatelliteStatus target = SatelliteStatus::Nominal;
    if (worst <= thresholds.failedAtOrBelow) {
        target = SatelliteStatus::Failed;
    } else if (worst < thresholds.criticalBelow) {
        target = SatelliteStatus::Critical;
    } else if (worst < thresholds.warningBelow) {
        target = SatelliteStatus::Warning;
    }

    // Walk the ladder one rung at a time rather than jumping. A satellite dropping
    // straight from Nominal to Failed must still pass through Warning and Critical:
    // otherwise the transition table would be documentation rather than a constraint,
    // and Phase 11's event emission would miss the intermediate states it needs to
    // report.
    int current = ladderRank(status_);
    const int destination = ladderRank(target);
    if (current < 0 || destination < 0) {
        return;
    }

    while (current != destination) {
        const int step = current + (destination > current ? 1 : -1);
        const SatelliteStatus next = statusFromRank(step);
        if (!isValidTransition(status_, next)) {
            break;
        }
        status_ = next;
        current = step;
    }
}

}  // namespace orca
