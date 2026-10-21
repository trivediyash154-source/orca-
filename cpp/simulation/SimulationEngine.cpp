#include "SimulationEngine.hpp"

#include <cmath>
#include <format>

#include "../core/Constants.hpp"
#include "../core/Validation.hpp"
#include "../graph/GraphBuilder.hpp"
#include "EventDispatcher.hpp"
#include "MissionExecutor.hpp"

namespace orca {

namespace {
MissionGraph buildInitialGraph(const Scenario& scenario, const TransferModel& model) {
    return GraphBuilder::build(scenario, model).graph;
}
}  // namespace

SimulationEngine::SimulationEngine(Scenario scenario, SimulationEngineConfig config)
    : scenario_(std::move(scenario)),
      config_(std::move(config)),
      clock_(scenario_.config().timeStepSec),
      state_(makeInitialState(scenario_)),
      model_(),
      graph_(buildInitialGraph(scenario_, model_)),
      replanner_(config_.replanPolicy) {}

void SimulationEngine::reset() {
    clock_.reset();
    state_ = makeInitialState(scenario_);
    queue_ = EventQueue();
    // The replanner's own history/metrics are a record of DECISIONS MADE, analogous to
    // the event log -- a fresh run should start with a fresh record, the same reasoning
    // `state_.metrics` resets via `makeInitialState()` above.
    replanner_ = DynamicReplanner(config_.replanPolicy);
    lastCycleDispatchSec = 0.0;
}

void SimulationEngine::tick() {
    clock_.step();
    const Seconds now = clock_.currentTimeSec();
    state_.simulationTimeSec = now;

    // Step 3: drain every event due by `now`, in the queue's own deterministic order.
    while (!queue_.empty() && queue_.peek().timestampSec <= now) {
        const Event event = queue_.pop();
        dispatchEvent(*this, event);
        state_.recordEvent(event);
    }

    // Step 3b (evaluation option): cycle dispatch of everything pending.
    if (config_.dispatchCycleSec > 0.0 && now - lastCycleDispatchSec >= config_.dispatchCycleSec) {
        lastCycleDispatchSec = now;
        dispatchPendingEmergencies(*this);
    }

    // Step 4: advance every active, in-transit mission's execution to `now`.
    for (MissionId missionId : state_.activeMissionIds()) {
        Mission* mission = state_.findMission(missionId);
        if (mission->state() != MissionState::InTransit) {
            continue;  // Replanning/Rendezvous/etc. are handled by their own events, not
                      // by mechanical leg execution
        }
        const MissionPlan* plan = state_.findMissionPlan(missionId);
        RescueVehicle* vehicle = state_.findVehicle(mission->vehicle());
        const ExecutionResult result =
            advanceMissionProgress(*mission, *vehicle, *plan, graph_, model_, now);
        ++state_.metrics.missionsUpdated;
        if (result.outcome == ExecutionOutcome::Arrived) {
            queue_.schedule(Event{
                .id = EventId(0),
                .timestampSec = now,
                .kind = EventKind::Rendezvous,
                .vehicle = mission->vehicle(),
                .emergency = mission->emergency(),
                .mission = missionId,
                .detail = "final leg arrived",
            });
        }
    }

    ++state_.metrics.ticks;
}

void SimulationEngine::step() {
    if (!clock_.isRunning()) {
        return;
    }
    tick();
}

void SimulationEngine::advance(Seconds seconds) {
    requireThat(seconds >= 0.0,
                std::format("SimulationEngine::advance: seconds ({:.6f}) must be "
                            "non-negative",
                            seconds));
    const double stepsExact = seconds / clock_.timeStepSec();
    const double stepsRounded = std::round(stepsExact);
    requireThat(std::fabs(stepsExact - stepsRounded) < kEpsilon,
                std::format("SimulationEngine::advance: {:.6f}s is not a whole multiple of "
                            "the {:.6f}s timestep",
                            seconds, clock_.timeStepSec()));
    if (!clock_.isRunning()) {
        return;
    }
    // Deliberately NOT clock_.advance(seconds) followed by one event drain -- that would
    // jump straight to the destination time and could silently skip every event scheduled
    // strictly between the old and new time (brief §81's explicit prohibition). Ticking
    // one dt at a time processes every intermediate instant's due events in order, exactly
    // as if the caller had called step() this many times individually.
    const auto wholeSteps = static_cast<long>(stepsRounded);
    for (long i = 0; i < wholeSteps; ++i) {
        tick();
    }
}

}  // namespace orca
