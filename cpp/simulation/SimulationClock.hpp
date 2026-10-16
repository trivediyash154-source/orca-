#pragma once

/// @file SimulationClock.hpp
/// Simulation time — Phase 8 brief §6-11: a fixed logical timestep, deterministic, and
/// independent of render frame rate.
///
/// ## Fixed timestep, not wall-clock
///
/// `step()` always advances by exactly `timeStepSec()` regardless of how much real time
/// elapsed since the last call. This is what makes the simulation "behave identically
/// regardless of whether the frontend renders at 30/60/120 FPS" (brief §8): the clock has
/// no notion of frames at all, only of `dt`.
///
/// ## Speed is informational, never physics
///
/// `speedMultiplier()` exists for a future real-time caller to decide HOW OFTEN to call
/// `step()`/`advance()` per unit of wall-clock time -- it changes nothing about `dt`, event
/// ordering, or any computed result (brief §9's explicit requirement). This class never
/// reads the wall clock itself; a caller translating "2x speed" into "call step() twice as
/// often" is that caller's responsibility, deliberately kept outside this class so the
/// clock's own behaviour never depends on real elapsed time (`std::chrono::steady_clock`
/// appears nowhere in this file).

#include "../core/Units.hpp"

namespace orca {

class SimulationClock {
public:
    /// @throws std::invalid_argument if `timeStepSec` is not positive.
    explicit SimulationClock(Seconds timeStepSec = 1.0) noexcept(false);

    [[nodiscard]] Seconds currentTimeSec() const noexcept { return currentTimeSec_; }
    [[nodiscard]] Seconds timeStepSec() const noexcept { return timeStepSec_; }
    [[nodiscard]] double speedMultiplier() const noexcept { return speedMultiplier_; }
    [[nodiscard]] bool isRunning() const noexcept { return running_; }

    /// Begin (or resume from a fresh state) advancing. Idempotent: starting an
    /// already-running clock is a no-op, not an error.
    void start() noexcept;

    /// Stop advancing. `step()`/`advance()` become no-ops while paused (brief §10) --
    /// they do not throw, so a caller does not need to check `isRunning()` before every
    /// call; the pause takes effect by making time simply not move.
    void pause() noexcept;

    /// Resume after `pause()`. Equivalent to `start()` -- kept as a distinctly-named
    /// operation because "resume" and "start" mean different things to a caller even
    /// though this class's own state machine treats them identically.
    void resume() noexcept;

    /// Return to `t=0`, not running. Does not change `timeStepSec()` or
    /// `speedMultiplier()` -- those are configuration, not run state.
    void reset() noexcept;

    /// Advance by exactly one `timeStepSec()`, if running. No-op while paused.
    void step() noexcept;

    /// Advance by `seconds`, in whole `timeStepSec()` increments plus one final partial
    /// step if `seconds` is not an exact multiple -- so `advance(1000.0)` with a 1.0s
    /// step behaves identically to calling `step()` 1000 times (brief §70's "support
    /// advance(1000.0) ... for large jumps," verified directly by
    /// `AdvanceMatchesRepeatedStep`). No-op (returns immediately) while paused.
    ///
    /// @throws std::invalid_argument if `seconds` is negative.
    void advance(Seconds seconds) noexcept(false);

    /// Force the clock to an exact time, running or not. For test setup and for a future
    /// scrubber/seek feature -- deliberately distinct from `advance()`, which is the
    /// normal simulation path and validates against negative deltas.
    ///
    /// @throws std::invalid_argument if `timeSec` is negative.
    void setTime(Seconds timeSec) noexcept(false);

    /// @throws std::invalid_argument if `multiplier` is not positive.
    void setSpeed(double multiplier) noexcept(false);

private:
    Seconds currentTimeSec_ = 0.0;
    Seconds timeStepSec_;
    double speedMultiplier_ = 1.0;
    bool running_ = false;
};

}  // namespace orca
