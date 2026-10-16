#include "SimulationClock.hpp"

#include <cmath>
#include <format>

#include "../core/Constants.hpp"
#include "../core/Validation.hpp"

namespace orca {

SimulationClock::SimulationClock(Seconds timeStepSec) noexcept(false)
    : timeStepSec_(timeStepSec) {
    requireThat(timeStepSec > 0.0,
                std::format("SimulationClock: timeStepSec ({:.6f}) must be positive",
                            timeStepSec));
}

void SimulationClock::start() noexcept { running_ = true; }
void SimulationClock::pause() noexcept { running_ = false; }
void SimulationClock::resume() noexcept { running_ = true; }

void SimulationClock::reset() noexcept {
    currentTimeSec_ = 0.0;
    running_ = false;
}

void SimulationClock::step() noexcept {
    if (!running_) {
        return;
    }
    currentTimeSec_ += timeStepSec_;
}

void SimulationClock::advance(Seconds seconds) noexcept(false) {
    requireThat(seconds >= 0.0,
                std::format("SimulationClock::advance: seconds ({:.6f}) must be "
                            "non-negative",
                            seconds));
    if (!running_) {
        return;
    }
    // Whole-multiple-of-timeStepSec_ requirement, not a soft rounding: the fixed-timestep
    // invariant (brief §7-8) is that every advance is composed of whole logical ticks, so
    // a caller asking for a fractional number of steps is a caller error to surface now,
    // not silently truncated into a slightly-wrong later time that would drift the same
    // scenario differently depending on HOW it was advanced (one big advance() vs many
    // step() calls) -- exactly the kind of order/path-dependence determinism forbids.
    const double stepsExact = seconds / timeStepSec_;
    const double stepsRounded = std::round(stepsExact);
    requireThat(std::fabs(stepsExact - stepsRounded) < kEpsilon,
                std::format("SimulationClock::advance: {:.6f}s is not a whole multiple of "
                            "the {:.6f}s timestep",
                            seconds, timeStepSec_));
    currentTimeSec_ += stepsRounded * timeStepSec_;
}

void SimulationClock::setTime(Seconds timeSec) noexcept(false) {
    requireThat(timeSec >= 0.0,
                std::format("SimulationClock::setTime: timeSec ({:.6f}) must be "
                            "non-negative",
                            timeSec));
    currentTimeSec_ = timeSec;
}

void SimulationClock::setSpeed(double multiplier) noexcept(false) {
    requireThat(multiplier > 0.0,
                std::format("SimulationClock::setSpeed: multiplier ({:.6f}) must be "
                            "positive",
                            multiplier));
    speedMultiplier_ = multiplier;
}

}  // namespace orca
