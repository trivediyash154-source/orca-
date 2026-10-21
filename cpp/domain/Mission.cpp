#include "Mission.hpp"

#include <format>

#include "../core/Math.hpp"
#include "../core/Validation.hpp"

namespace orca {

bool isValidTransition(MissionState from, MissionState next) noexcept {
    if (from == next) {
        return true;  // idempotent
    }
    switch (from) {
        case MissionState::Planned:
            return next == MissionState::Launched || next == MissionState::Aborted;
        case MissionState::Launched:
            return next == MissionState::InTransit || next == MissionState::Aborted ||
                   next == MissionState::Failed;
        case MissionState::InTransit:
            return next == MissionState::Replanning || next == MissionState::Rendezvous ||
                   next == MissionState::Failed || next == MissionState::Aborted;
        case MissionState::Replanning:
            return next == MissionState::InTransit || next == MissionState::Aborted;
        case MissionState::Rendezvous:
            return next == MissionState::Success || next == MissionState::Failed;
        case MissionState::Success:
        case MissionState::Failed:
        case MissionState::Aborted:
            return false;  // terminal
    }
    return false;
}

Mission::Mission(MissionId id, const MissionPlan& plan, Seconds startedAtSec)
    : id_(id),
      sourcePlan_(plan.id()),
      vehicle_(plan.vehicle()),
      emergency_(plan.emergency()),
      currentRoute_(plan.route()),
      startedAtSec_(startedAtSec),
      planDepartureSec_(plan.departureTimeSec()) {
    requireThat(plan.isFeasible(),
                std::format("Mission {}: cannot commit infeasible plan {}", toString(id_),
                            toString(plan.id())));
}

bool Mission::isTerminal() const noexcept {
    return state_ == MissionState::Success || state_ == MissionState::Failed ||
           state_ == MissionState::Aborted;
}

bool Mission::isActive() const noexcept {
    return state_ == MissionState::Launched || state_ == MissionState::InTransit ||
           state_ == MissionState::Replanning || state_ == MissionState::Rendezvous;
}

bool Mission::isOnFinalLeg() const noexcept {
    return !currentRoute_.empty() && currentLeg_ + 1 == currentRoute_.size();
}

bool Mission::canTransitionTo(MissionState next) const noexcept {
    return isValidTransition(state_, next);
}

void Mission::setState(MissionState next) {
    requireThat(canTransitionTo(next),
                std::format("Mission {}: illegal state transition {} -> {}", toString(id_),
                            toString(state_), toString(next)));
    state_ = next;
}

void Mission::setProgressOnLeg(double progress) {
    requireThat(inUnitInterval(progress),
                std::format("Mission {}: progressOnLeg ({:.4f}) must lie in [0,1]",
                            toString(id_), progress));
    progressOnLeg_ = progress;
}

void Mission::advanceLeg() {
    requireThat(!isOnFinalLeg(),
                std::format("Mission {}: already on the final leg ({} of {}); transition "
                            "to Rendezvous instead",
                            toString(id_), currentLeg_ + 1, currentRoute_.size()));
    ++currentLeg_;
    progressOnLeg_ = 0.0;
}

void Mission::recordDeltaVSpent(KmPerSec amountKms) {
    requireThat(amountKms >= 0.0,
                std::format("Mission {}: delta-v spend ({:.4f} km/s) must be non-negative",
                            toString(id_), amountKms));
    deltaVSpentKms_ += amountKms;
}

void Mission::adoptRoute(const MissionPlan& replacement) {
    requireThat(replacement.isFeasible(),
                std::format("Mission {}: cannot adopt infeasible plan {}", toString(id_),
                            toString(replacement.id())));
    requireThat(!replacement.route().empty(),
                std::format("Mission {}: replacement plan {} has no route", toString(id_),
                            toString(replacement.id())));

    rejectedAlternatives_.push_back(sourcePlan_);
    sourcePlan_ = replacement.id();
    currentRoute_ = replacement.route();
    planDepartureSec_ = replacement.departureTimeSec();
    currentLeg_ = 0;
    progressOnLeg_ = 0.0;
    ++replanCount_;
}

void Mission::recordCompletion(Seconds atSec) {
    requireThat(atSec >= startedAtSec_,
                std::format("Mission {}: completion ({:.3f}) precedes start ({:.3f})",
                            toString(id_), atSec, startedAtSec_));
    completedAtSec_ = atSec;
}

}  // namespace orca
