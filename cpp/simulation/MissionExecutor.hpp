#pragma once

/// @file MissionExecutor.hpp
/// Progresses an ALREADY-APPROVED mission through simulated time — Phase 8 brief §25.
/// Never plans a route; every `EdgeId` this file walks came from `DynamicReplanner`/
/// `GlobalReplanner` (Phase 7) or the initial `MissionPlanner` (Phase 5/6) pipeline.
///
/// ## Estimated cost vs. actual consumption (brief §29-31)
///
/// A `MissionPlan`'s `totalCost()` is what planning ESTIMATED the whole route would cost,
/// computed once, before departure. This file consumes resources per LEG, as each leg
/// actually completes -- `vehicle.consumeDeltaV()` and `mission.recordDeltaVSpent()` are
/// called with that leg's own evaluated delta-v, never with the plan's aggregate figure.
/// A mission 60% through its route has spent only the delta-v its completed legs actually
/// required; replanning from there (Phase 7) sees exactly that remainder, never a reset
/// (brief §31 — this is the concrete mechanism that keeps Phase 7's own remaining-resource
/// guarantee true once execution is real, not merely hypothetical).
///
/// ## Multi-leg completion in one call (brief §28, §81)
///
/// `advanceMissionProgress()` completes every leg whose arrival time has already passed,
/// in order, within a single call -- not just the first one. This is what makes a large
/// `advance(1000.0)`-style jump behave identically to 1000 individual `step()` calls: no
/// leg completion is skipped merely because more than one became due within one tick.
///
/// ## Rendezvous, simplified (brief §34)
///
/// This file treats a transfer's EVALUATED endpoint (the final leg's `TransferResult`
/// reporting `feasible == true` and arriving) as a successful rendezvous — ORCA's transfer
/// model already IS the simplified simulation model (`TRANSFER_MODEL.md`), so there is no
/// separate, higher-fidelity proximity/relative-velocity check to perform here. Stated
/// explicitly per the brief's own instruction to document this rather than let it be an
/// implicit assumption.

#include <vector>

#include "../core/Types.hpp"
#include "../core/Units.hpp"
#include "../domain/Mission.hpp"
#include "../domain/MissionPlan.hpp"
#include "../domain/RescueVehicle.hpp"
#include "../graph/MissionGraph.hpp"
#include "../transfer/TransferModel.hpp"

namespace orca {

/// One leg's actual departure/arrival time and delta-v, from re-evaluating `TransferModel`
/// at that leg's real (not reference/graph-build-time) departure -- what
/// `computeLegTimings()` below reports per leg.
struct LegTiming {
    Seconds departureSec = 0.0;
    Seconds arrivalSec = 0.0;
    KmPerSec deltaVKms = 0.0;
};

/// Walks every leg of `route` from `planDepartureSec`, calling nothing but
/// `TransferModel::evaluate()` -- the one shared source of transfer physics -- under
/// deliberately permissive constraints (the route was already confirmed feasible when it
/// was adopted; re-litigating feasibility is `MissionValidity.hpp`'s job, not this one's).
/// Exposed publicly (not file-local to `MissionExecutor.cpp`) so a second consumer that
/// needs the same real, per-leg schedule -- `io/ViewProjection.cpp`, rendering a mission's
/// trajectory -- calls this instead of duplicating the computation (`ARCHITECTURE.md` §6.1's
/// "the frontend/serializer never derives a value the engine could compute," applied here to
/// mean "and neither does a second copy of this function").
///
/// @throws std::invalid_argument if any leg of `route` is no longer evaluable from its
///         computed departure time (indicates a route adopted inconsistently with the
///         graph it came from).
[[nodiscard]] std::vector<LegTiming> computeLegTimings(const MissionGraph& graph,
                                                        const TransferModel& model,
                                                        const std::vector<EdgeId>& route,
                                                        Seconds planDepartureSec);

/// What happened during one `advanceMissionProgress()` call -- distinct from `Mission`'s
/// own state (which this reflects, not duplicates), reported so `SimulationEngine` knows
/// whether to schedule a `Rendezvous`/`RescueComplete` event without re-deriving it.
enum class ExecutionOutcome {
    /// Still mid-leg; only `progressOnLeg` changed.
    InProgress,
    /// One or more legs completed, but the mission is not yet on/past its final leg.
    LegCompleted,
    /// The final leg's evaluated endpoint was reached this call -- rendezvous achieved.
    Arrived,
};

struct ExecutionResult {
    ExecutionOutcome outcome = ExecutionOutcome::InProgress;

    /// Total actual delta-v consumed by legs completed THIS call (zero if `InProgress`).
    /// Already applied to `vehicle`/`mission` by the time this is returned -- reported
    /// for logging/telemetry, not for the caller to apply a second time.
    KmPerSec deltaVConsumedThisCallKms = 0.0;
};

/// Advances `mission` (already `InTransit`, governed by `plan`) to reflect `nowSec`,
/// completing every leg whose arrival time has already passed and updating
/// `progressOnLeg` for whichever leg is current afterward. Mutates `mission` and `vehicle`
/// through their own validated APIs only (`Mission::advanceLeg()`,
/// `Mission::recordDeltaVSpent()`, `Mission::setProgressOnLeg()`,
/// `RescueVehicle::consumeDeltaV()`) -- never writes a field directly.
///
/// Does not transition `mission` to `Rendezvous`/`Success` itself -- `ExecutionOutcome::
/// Arrived` tells the caller (`SimulationEngine`) that it should, via the same domain
/// state-machine API this file itself uses, so the transition happens in exactly one
/// place regardless of which event triggered it (brief §33's "use domain APIs/state
/// machines, do not bypass state validation").
///
/// @throws std::invalid_argument if `mission.state() != MissionState::InTransit`, or if
///         `plan`'s route does not match `mission.currentRoute()` -- a caller error: the
///         plan passed in must be the one CURRENTLY governing this mission.
[[nodiscard]] ExecutionResult advanceMissionProgress(Mission& mission, RescueVehicle& vehicle,
                                                     const MissionPlan& plan,
                                                     const MissionGraph& graph,
                                                     const TransferModel& model, Seconds nowSec);

}  // namespace orca
