#pragma once

/// @file SimulationEngine.hpp
/// The orchestrator — Phase 8 brief §44, §63-64. Owns the clock, the world state and the
/// event queue; ties orbital evolution, mission execution and Phase 7's replanner together
/// into one deterministic tick. Contains no planning logic and no transfer physics of its
/// own (brief §2's architectural rule) — every decision comes from `planning/` (Phase 7)
/// or `optimization/`+`algorithms/` (Phase 5/6) via `DynamicReplanner`/`GlobalReplanner`;
/// every physical evaluation comes from `TransferModel::evaluate()` via `MissionExecutor`.
///
/// ## The tick, precisely (brief §64)
///
/// At simulation time `t`, `tick()`:
///   1. Advances the clock by one `timeStepSec()` to `t + dt`.
///   2. (Orbital positions are derived from time, per `Orbit::positionAtKm()` -- nothing
///      to update explicitly; step 2 is a no-op by construction, not an omission.)
///   3. Drains every event with `timestampSec <= t + dt` from the queue, in the queue's
///      own deterministic order, dispatching each via `EventDispatcher.cpp`. A dispatched
///      event may itself invoke `DynamicReplanner`/`GlobalReplanner` and/or schedule
///      follow-up events -- this is where Phase 8 brief §35-38's "material change ->
///      MissionChange -> DynamicReplanner" integration actually happens.
///   4. Advances every active mission's execution (`MissionExecutor.hpp`) to `t + dt`.
///      A mission that arrives this tick has a `Rendezvous` event scheduled for it, fired
///      on the NEXT tick's step 3 -- a one-`dt` lag that keeps step 4 non-recursive.
///
/// "At simulation time t, the engine observes state S(t), processes events scheduled at
/// t, and then produces S(t+dt)" (brief §64's own requested precise statement) is exactly
/// steps 1, 3 and 4 above, in that order.

#include <optional>

#include "../core/RandomGenerator.hpp"
#include "../domain/Scenario.hpp"
#include "../optimization/OptimizationProfile.hpp"
#include "../planning/DynamicReplanner.hpp"
#include "../planning/ReplanPolicy.hpp"
#include "EventQueue.hpp"
#include "SimulationClock.hpp"
#include "SimulationState.hpp"

namespace orca {

/// Configuration a run needs beyond what `Scenario` itself carries -- policy knobs, not
/// world data.
enum class DispatchSolver { Hungarian, Greedy };

struct SimulationEngineConfig {
    OptimizationProfile profile = OptimizationProfile::balanced();
    ReplanPolicy replanPolicy = defaultReplanPolicy();

    /// The proximity heuristic `EventDispatcher.cpp`'s `DebrisAlert` handling uses to
    /// decide whether a debris object threatens a specific graph edge -- deliberately
    /// simple (brief §42): a route leg is "threatened" when EITHER endpoint's orbit has a
    /// semi-major axis within this many km of the debris object's own. Not conjunction
    /// analysis; a documented, explicit simplification.
    Kilometres debrisProximityThresholdKm = 50.0;

    // --- Evaluation options. The defaults reproduce the engine's original behaviour. ---

    /// Solver used by the dispatch step. Greedy exists so an evaluation can compare the two
    /// end to end on identical event sequences.
    DispatchSolver dispatchSolver = DispatchSolver::Hungarian;

    /// 0 (default) dispatches each emergency the moment it is declared. A positive value
    /// instead dispatches all pending emergencies together once per cycle, the setting in
    /// which a global matching can differ from a greedy one.
    Seconds dispatchCycleSec = 0.0;

    /// When true a vehicle is retired after one rescue. The mission graph keeps every vehicle
    /// at its launch orbit, so a reused vehicle would otherwise depart from where it started
    /// rather than from where its previous rescue ended.
    bool singleSortie = false;
};

class SimulationEngine {
public:
    /// Builds the initial world state from `scenario` (`makeInitialState()`,
    /// `SimulationState.hpp`) and a clock at `scenario.config().timeStepSec`. Does not
    /// start running -- call `start()` explicitly (brief §10's pause/resume semantics
    /// apply from construction: a freshly built engine begins paused).
    explicit SimulationEngine(Scenario scenario, SimulationEngineConfig config = {});

    [[nodiscard]] const SimulationState& state() const noexcept { return state_; }
    [[nodiscard]] const SimulationClock& clock() const noexcept { return clock_; }
    [[nodiscard]] const MissionGraph& graph() const noexcept { return graph_; }
    [[nodiscard]] const Scenario& scenario() const noexcept { return scenario_; }
    [[nodiscard]] const DynamicReplanner& replanner() const noexcept { return replanner_; }
    [[nodiscard]] const EventQueue& eventQueue() const noexcept { return queue_; }

    // --- Clock passthrough (brief §44) --------------------------------------

    void start() noexcept { clock_.start(); }
    void pause() noexcept { clock_.pause(); }
    void resume() noexcept { clock_.resume(); }

    /// Returns the engine to its initial state: clock to `t=0` and paused, world state
    /// rebuilt fresh from `scenario_`, event queue and history cleared (brief §66). The
    /// scenario itself (and therefore its seed) is unchanged, so re-running the same
    /// sequence of `scheduleEvent()`/`step()` calls after `reset()` reproduces the same
    /// run -- `SimulationReplayTest` in the test suite checks this directly.
    void reset();

    /// Advances exactly one tick (brief §11). No-op while paused.
    void step();

    /// Advances by `seconds`, calling `step()` `seconds / timeStepSec()` times --
    /// processing every intermediate tick's due events in order, never jumping straight
    /// to the destination time (brief §81: "verify that no scheduled event is silently
    /// skipped"). No-op while paused.
    ///
    /// @throws std::invalid_argument if `seconds` is not a non-negative whole multiple of
    ///         `clock().timeStepSec()` (the same requirement `SimulationClock::advance()`
    ///         states and enforces).
    void advance(Seconds seconds);

    void setSpeed(double multiplier) { clock_.setSpeed(multiplier); }

    /// Assigns a sequential `EventId` and inserts `event` into the queue. The primary way
    /// a caller (test, `EmergencyGenerator`, or a future scenario loader) introduces new
    /// occurrences into an otherwise-deterministic run.
    EventId scheduleEvent(Event event) { return queue_.schedule(std::move(event)); }

    /// Derives a per-subsystem random stream from the scenario's own seed
    /// (`RandomGenerator::forStream()`) -- the one sanctioned way anything in this engine
    /// obtains randomness (brief §22's "never use global random state").
    [[nodiscard]] RandomGenerator randomStream(RandomStream stream) const noexcept {
        return RandomGenerator(scenario_.seed()).forStream(stream);
    }

    // --- Mutable access for EventDispatcher.cpp only ------------------------
    //
    // EventDispatcher.cpp is not a member function (kept as free functions so the tick
    // loop in SimulationEngine.cpp and the event-kind switch in EventDispatcher.cpp stay
    // in separate, independently-sized files -- brief §72). It needs the same mutation
    // access a member function would have, which these accessors provide explicitly
    // rather than via `friend`.

    [[nodiscard]] SimulationState& stateMutable() noexcept { return state_; }
    [[nodiscard]] EventQueue& eventQueueMutable() noexcept { return queue_; }
    [[nodiscard]] const MissionGraph& graphForDispatch() const noexcept { return graph_; }
    [[nodiscard]] const TransferModel& modelForDispatch() const noexcept { return model_; }
    [[nodiscard]] DynamicReplanner& replannerMutable() noexcept { return replanner_; }
    [[nodiscard]] const SimulationEngineConfig& config() const noexcept { return config_; }
    [[nodiscard]] Seconds currentTimeSec() const noexcept { return clock_.currentTimeSec(); }

    /// Time of the last cycle dispatch; used only when `config().dispatchCycleSec > 0`.
    Seconds lastCycleDispatchSec = 0.0;

private:
    void tick();

    Scenario scenario_;
    SimulationEngineConfig config_;
    SimulationClock clock_;
    SimulationState state_;
    EventQueue queue_;
    TransferModel model_;  ///< must be declared (and thus constructed) before `graph_`,
                           ///< which is built FROM it -- member init order follows
                           ///< declaration order, not the constructor's initializer list.
    MissionGraph graph_;
    DynamicReplanner replanner_;
};

}  // namespace orca
