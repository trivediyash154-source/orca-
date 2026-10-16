#pragma once

/// @file MissionValidity.hpp
/// Is an active mission's CURRENT route still good, evaluated from where the vehicle
/// actually is right now -- Phase 7 brief §7-8. This file answers exactly that question
/// and nothing more: it does not search for alternatives (that is `DynamicReplanner`'s
/// job) and it does not re-derive transfer physics (every number here comes from calling
/// the existing `TransferModel::evaluate()`, the same function `ParetoSearch.cpp` and
/// `CandidateGeneration.cpp` call).
///
/// ## Why "evaluate the remaining route" is not "duplicate the search"
///
/// `evaluateRemainingRouteCost()` walks a SPECIFIC, ALREADY-KNOWN sequence of edges
/// (`Mission::currentRoute()`) and sums each leg's real evaluation -- it never explores
/// alternatives or compares candidates. This is the same operation
/// `pareto_search_oracle_test.cpp`'s `enumerateSimplePaths` performs to verify search
/// results independently: evaluating one known path is a fundamentally different
/// operation from searching for the best one, and reuses `TransferModel::evaluate()`
/// exactly as every other caller does.
///
/// ## Replanning-from-current-state convention, reused from Phase 6
///
/// `evaluateMissionValidity()` builds its `TransferConstraints` the same way
/// `CandidateGeneration.cpp`'s `makeSearchConstraints()` does for a fresh dispatch:
/// `deltaVBudgetKms = vehicle.deltaVRemainingKms()` (the CURRENT remaining capability,
/// treated as the budget for evaluating what remains, not the vehicle's lifetime total)
/// and `latestArrivalSec = emergency.deadlineSec()` (the CURRENT deadline). This is
/// deliberately NOT `TransferConstraints::forVehicle()`, which fixes `deltaVBudgetKms` to
/// the vehicle's *total* budget for a different purpose (comparing fuel spend across a
/// vehicle's whole lifetime) -- using it here would make the cumulative-fuel hard
/// constraint check against the wrong denominator for an already-partially-spent vehicle.

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "../core/ObjectiveVector.hpp"
#include "../core/Types.hpp"
#include "../core/Units.hpp"
#include "../domain/Emergency.hpp"
#include "../domain/Mission.hpp"
#include "../domain/RescueVehicle.hpp"
#include "../graph/MissionGraph.hpp"
#include "../transfer/TransferModel.hpp"
#include "ReplanPolicy.hpp"
#include "ReplanReason.hpp"

namespace orca {

/// Phase 7 brief §8's five states, exact semantics documented per-value since the brief's
/// own prose is easy to misread at the Warning/Invalid boundary.
enum class ValidityState {
    /// The current route remains feasible with a comfortable margin on every constraint.
    Valid,
    /// Still feasible, but a constraint's margin has become thin (`ReplanPolicy`'s
    /// `warning*MarginFraction` fields) -- not yet a reason to replan, but worth flagging.
    Warning,
    /// The current route no longer satisfies a required constraint (delta-v, deadline,
    /// risk ceiling, vehicle availability). A mandatory replan is warranted; whether an
    /// alternative exists is not yet known at this point -- that is what attempting a
    /// replan determines.
    Invalid,
    /// Set only by `DynamicReplanner` after an `Invalid` mission's replan attempt found no
    /// feasible alternative either -- never produced by `evaluateMissionValidity()`
    /// directly, since checking "does any alternative exist" is a search, not a validity
    /// check (this file's own scope boundary, stated in the file comment).
    Impossible,
    /// The mission's objective is already achieved (`Emergency::state() ==
    /// EmergencyState::Resolved`) or the mission itself reached `MissionState::Success`.
    Completed,
};

[[nodiscard]] constexpr std::string_view toString(ValidityState s) noexcept {
    switch (s) {
        case ValidityState::Valid:      return "VALID";
        case ValidityState::Warning:    return "WARNING";
        case ValidityState::Invalid:    return "INVALID";
        case ValidityState::Impossible: return "IMPOSSIBLE";
        case ValidityState::Completed:  return "COMPLETED";
    }
    return "UNKNOWN";
}

/// The result of walking a KNOWN sequence of edges from a given leg onward, summing real
/// per-leg evaluations -- not a search result (no alternatives are considered).
struct RemainingRouteEvaluation {
    bool feasible = false;
    ObjectiveVector cost;
    Seconds arrivalTimeSec = 0.0;

    /// Set iff `!feasible`. Which single constraint was found binding, using the same
    /// 2-tier relaxation diagnosis `CandidateGeneration.cpp`'s `diagnoseInfeasibility()`
    /// uses (relax deadline alone, then delta-v alone) -- reused, not reimplemented, via
    /// calling `evaluateRemainingRouteCost()` itself with relaxed constraints.
    std::optional<ConstraintKind> infeasibilityReason;
};

/// Which leg of `route` remaining evaluation should start from, given the mission is
/// `progressOnLeg` of the way through `currentLeg`. A leg already in progress
/// (`progressOnLeg > 0`) is treated as committed -- its delta-v is already being spent and
/// cannot be un-spent -- so remaining evaluation starts at the NEXT leg. A leg not yet
/// begun (`progressOnLeg == 0`) is still fully re-plannable, so it is included.
[[nodiscard]] std::size_t remainingLegStartIndex(std::size_t currentLeg,
                                                 double progressOnLeg) noexcept;

/// Walks `route[startLegIndex..]`, evaluating each leg via `model.evaluate()` at its
/// actual departure time (the previous leg's arrival, or `nowSec` for the first walked
/// leg) and composing costs via `ObjectiveVector::compose()` -- exactly how
/// `ParetoSearch.cpp` composes a path's cost, applied here to one fixed, known path
/// instead of during a search.
///
/// `startLegIndex == route.size()` (every leg already flown) returns a feasible,
/// zero-cost result -- the mission has structurally already arrived; whether that means
/// `Completed` is `evaluateMissionValidity()`'s call, not this function's.
///
/// @param invalidatedEdge Phase 8: if set and any walked leg uses this exact edge, that leg
///        is treated as infeasible regardless of what `model.evaluate()` says physically --
///        the mechanism `simulation/`'s debris/environment handling uses to invalidate a
///        route without touching `TransferModel`/`GraphBuilder` at all (`MissionChange.hpp`
///        `invalidatedEdge`'s own field comment named this exact use before Phase 8 wired
///        it up). `std::nullopt` (the default) preserves Phase 7's original behaviour
///        exactly.
/// @param legArrivalTimesOut Phase 8: if non-null, filled with each walked leg's actual
///        arrival time, in order -- `MissionExecutor.hpp` needs per-leg timing to decide
///        when a leg completes, and re-deriving it via a second walk would either diverge
///        from this one or duplicate it; a single walk now optionally reports both.
///
/// @throws std::invalid_argument if `startLegIndex > route.size()`.
[[nodiscard]] RemainingRouteEvaluation evaluateRemainingRouteCost(
    const MissionGraph& graph, const TransferModel& model, const std::vector<EdgeId>& route,
    std::size_t startLegIndex, Seconds nowSec, const TransferConstraints& constraints,
    std::optional<EdgeId> invalidatedEdge = std::nullopt,
    std::vector<Seconds>* legArrivalTimesOut = nullptr);

struct MissionValidity {
    ValidityState state = ValidityState::Valid;
    ReplanReason reason = ReplanReason::None;
    std::optional<ConstraintKind> violatedConstraint;

    /// Human-readable, names the numbers -- same convention as
    /// `ConstraintViolation::detail` (`MissionPlan.hpp`).
    std::string detail;

    /// The remaining route's own evaluation -- always populated (even when
    /// `state == Completed`, in which case it is the trivial zero-leg result).
    RemainingRouteEvaluation remaining;
};

/// The single entry point this file exists to provide. Does not search for alternatives;
/// see the file comment for why that is a deliberate scope boundary, not an omission.
///
/// @param invalidatedEdge Phase 8: forwarded verbatim to `evaluateRemainingRouteCost()` --
///        see that function's own parameter comment.
///
/// @throws std::invalid_argument if `mission` is already in a terminal `MissionState`
///         (`Success`/`Failed`/`Aborted`) -- a terminal mission should never reach the
///         replanner; that is a caller error, the same discipline `Mission::setState()`
///         itself already enforces for illegal transitions.
[[nodiscard]] MissionValidity evaluateMissionValidity(
    const Mission& mission, const RescueVehicle& vehicle, const Emergency& emergency,
    const MissionGraph& graph, const TransferModel& model, Seconds nowSec,
    const ReplanPolicy& policy = defaultReplanPolicy(),
    std::optional<EdgeId> invalidatedEdge = std::nullopt);

}  // namespace orca
