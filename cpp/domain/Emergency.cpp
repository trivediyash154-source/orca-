#include "Emergency.hpp"

#include <cmath>
#include <format>

#include "../core/Math.hpp"
#include "../core/Validation.hpp"

namespace orca {

bool isValidTransition(EmergencyState from, EmergencyState next) noexcept {
    if (from == next) {
        return true;  // idempotent
    }
    // A window can close at any point before resolution.
    if (next == EmergencyState::Expired) {
        return from != EmergencyState::Resolved;
    }
    switch (from) {
        case EmergencyState::Pending:
            return next == EmergencyState::Assigned || next == EmergencyState::Unreachable;
        case EmergencyState::Assigned:
            return next == EmergencyState::InProgress || next == EmergencyState::Pending;
        case EmergencyState::InProgress:
            return next == EmergencyState::Resolved || next == EmergencyState::Pending;
        case EmergencyState::Unreachable:
            return next == EmergencyState::Pending;
        case EmergencyState::Resolved:
        case EmergencyState::Expired:
            return false;  // terminal
    }
    return false;
}

Emergency::Emergency(EmergencyId id,
                     SatelliteId subject,
                     EmergencyKind kind,
                     Severity severity,
                     Seconds detectedAtSec,
                     Seconds survivalWindowSec)
    : id_(id),
      subject_(subject),
      kind_(kind),
      severity_(severity),
      detectedAtSec_(detectedAtSec),
      survivalWindowSec_(survivalWindowSec) {
    requireThat(std::isfinite(detectedAtSec_) && detectedAtSec_ >= 0.0,
                std::format("Emergency {}: detectedAtSec ({:.3f}) must be finite and "
                            "non-negative",
                            toString(id_), detectedAtSec_));
    requireThat(std::isfinite(survivalWindowSec_) && survivalWindowSec_ > 0.0,
                std::format("Emergency {}: survivalWindowSec ({:.3f}) must be positive",
                            toString(id_), survivalWindowSec_));
}

Seconds Emergency::deadlineSec() const noexcept {
    return detectedAtSec_ + survivalWindowSec_;
}

Seconds Emergency::remainingWindowSec(Seconds nowSec) const noexcept {
    const Seconds remaining = deadlineSec() - nowSec;
    return remaining > 0.0 ? remaining : 0.0;
}

double Emergency::urgencyAt(Seconds nowSec) const noexcept {
    // survivalWindowSec_ is guaranteed positive by the constructor, so this cannot
    // divide by zero.
    return clampUnit(1.0 - remainingWindowSec(nowSec) / survivalWindowSec_);
}

bool Emergency::isPastDeadline(Seconds nowSec) const noexcept {
    return nowSec > deadlineSec();
}

bool Emergency::isTerminal() const noexcept {
    return state_ == EmergencyState::Resolved || state_ == EmergencyState::Expired;
}

void Emergency::setState(EmergencyState next) {
    requireThat(canTransitionTo(next),
                std::format("Emergency {}: illegal state transition {} -> {}",
                            toString(id_), toString(state_), toString(next)));
    state_ = next;
}

bool Emergency::canTransitionTo(EmergencyState next) const noexcept {
    return isValidTransition(state_, next);
}

}  // namespace orca
