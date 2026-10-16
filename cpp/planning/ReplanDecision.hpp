#pragma once

/// @file ReplanDecision.hpp
/// The result `DynamicReplanner::evaluate()` returns, plus a deterministic, structured
/// explanation of it -- Phase 7 brief §11, §26.
///
/// ## "Structured reasons and values," not generated prose
///
/// `explain()` formats a `ReplanDecision` into the brief's example layout (§26) using
/// fixed templates keyed on `ReplanReason` -- the same style `toString()` already uses
/// throughout this codebase, extended from a one-word wire name to a one-sentence
/// rationale. Nothing here calls a language model or assembles free-form text; every
/// number printed is read directly off the `ReplanDecision`'s own fields.

#include <optional>
#include <string>
#include <string_view>

#include "../core/ObjectiveVector.hpp"
#include "../core/Units.hpp"
#include "../domain/MissionPlan.hpp"
#include "MissionValidity.hpp"
#include "ReplanReason.hpp"

namespace orca {

enum class ReplanDecisionType { Continue, Replan, Abort, Complete };

[[nodiscard]] constexpr std::string_view toString(ReplanDecisionType t) noexcept {
    switch (t) {
        case ReplanDecisionType::Continue: return "CONTINUE";
        case ReplanDecisionType::Replan:   return "REPLAN";
        case ReplanDecisionType::Abort:    return "ABORT";
        case ReplanDecisionType::Complete: return "COMPLETE";
    }
    return "UNKNOWN";
}

/// Why an `Abort` was chosen -- distinct from `ReplanReason`, which explains why
/// replanning was *attempted*; this explains why it *failed* (Phase 7 brief §20).
enum class AbortReason {
    None,
    NoFeasibleContinuation,
    VehicleUnavailable,
    TargetUnavailable,
    InsufficientFuel,
    DeadlineImpossible,
    RiskUnacceptable,

    /// Not a failure: the mission is being deliberately abandoned in favour of a
    /// higher-priority emergency (`DynamicReplanner::evaluatePreemption()`, Phase 7 brief
    /// §23). Kept distinct from the failure reasons above so a caller/UI can tell
    /// "this rescue could not continue" apart from "this rescue was intentionally
    /// superseded" -- both end the mission the same way (`MissionState::Aborted`) but mean
    /// very different things.
    Preempted,
};

[[nodiscard]] constexpr std::string_view toString(AbortReason r) noexcept {
    switch (r) {
        case AbortReason::None:                    return "NONE";
        case AbortReason::NoFeasibleContinuation:  return "NO_FEASIBLE_CONTINUATION";
        case AbortReason::VehicleUnavailable:      return "VEHICLE_UNAVAILABLE";
        case AbortReason::TargetUnavailable:       return "TARGET_UNAVAILABLE";
        case AbortReason::InsufficientFuel:        return "INSUFFICIENT_FUEL";
        case AbortReason::DeadlineImpossible:      return "DEADLINE_IMPOSSIBLE";
        case AbortReason::RiskUnacceptable:        return "RISK_UNACCEPTABLE";
        case AbortReason::Preempted:                return "PREEMPTED";
    }
    return "UNKNOWN";
}

/// The outcome of one `DynamicReplanner::evaluate()` call. Every field a caller (test,
/// eventual UI, eventual log) needs to act on or explain the decision is here directly --
/// Phase 7 brief §11's "preserve enough information for diagnostics/logging/explanation."
struct ReplanDecision {
    ReplanDecisionType decision = ReplanDecisionType::Continue;
    ReplanReason reason = ReplanReason::None;
    AbortReason abortReason = AbortReason::None;  ///< set iff decision == Abort
    MissionValidity validity;

    /// The plan being continued (`Continue`) or replaced (`Replan`/`Abort`) -- always the
    /// plan that governed the mission before this decision.
    PlanId previousPlan;
    ObjectiveVector previousRemainingCost;  ///< MissionValidity's own remaining-route cost
    Seconds previousArrivalSec = 0.0;

    /// Set iff `decision == Replan` -- the new plan to adopt via `Mission::adoptRoute()`.
    std::optional<MissionPlan> newPlan;

    /// Free-text detail carried through from `MissionValidity`/the triggering
    /// `MissionChange`, never parsed, only for logging (`MissionChange.hpp`'s own
    /// reasoning applies here too).
    std::string detail;

    Seconds decidedAtSec = 0.0;
};

/// Formats `decision` into the structured layout Phase 7 brief §26 shows by example.
/// Deterministic and table-driven -- see the file comment.
[[nodiscard]] std::string explain(const ReplanDecision& decision);

}  // namespace orca
