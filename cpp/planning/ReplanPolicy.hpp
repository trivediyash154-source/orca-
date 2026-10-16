#pragma once

/// @file ReplanPolicy.hpp
/// Configurable thresholds governing when the replanner switches plans, warns, or ignores
/// a change entirely -- Phase 7 brief §13-14's "clear policy layer," kept as one small,
/// inspectable struct rather than scattered constants, the same reasoning
/// `OptimizationProfile`/`PriorityWeights` already follow for their own weight sets.
///
/// Deterministic, rule-based thresholds, not a decision-theoretic model (brief §13: "do
/// not yet implement an extremely sophisticated model"). Every field here is a policy
/// decision, not a physical fact -- analogous to `TransferConstraints::maxWaitSec`'s own
/// "a default, not a physical fact" framing.

#include "../core/Units.hpp"

namespace orca {

struct ReplanPolicy {
    // --- Hysteresis / plan stability (brief §14) -----------------------------

    /// An optional alternative must beat the current plan's score by at least this much
    /// (`oldScore - newScore >= switchMargin`, since lower score is better -- §5.1/§6 of
    /// OPTIMIZATION_MODEL.md) before an OPTIONAL switch happens. A mandatory replan (the
    /// current plan is infeasible) ignores this margin entirely -- brief §14, §33: "hard
    /// constraints override switching reluctance."
    double switchMargin = 0.05;

    // --- Materiality thresholds (brief §12): below these, a reported change is not even
    // considered for replanning, mandatory or optional. -----------------------

    /// A fuel/delta-v change smaller than this fraction of the vehicle's *total* budget is
    /// not material on its own (brief §12's "0.01% fuel change probably should not trigger
    /// a full replan"). Does not suppress a MANDATORY replan if the vehicle's current
    /// remaining delta-v is actually insufficient for the remaining route -- materiality
    /// only gates whether an OPTIONAL re-evaluation is worth running at all.
    double materialFuelChangeFraction = 0.02;

    /// A risk change smaller than this (absolute, in the [0,1] risk scale) is not material
    /// on its own.
    double materialRiskChangeFraction = 0.02;

    // --- Hard ceilings (brief §33: "current plan violates maximumRisk") ------

    /// Cumulative risk above this triggers a MANDATORY replan regardless of switchMargin.
    /// Distinct from `TransferConstraints::maxRisk`'s per-search-time meaning (Phase
    /// 4/5's build-time ceiling) -- this is the Phase 7 replanning-policy ceiling applied
    /// to an ALREADY-COMMITTED mission's remaining route.
    double maxAcceptableRisk = 0.85;

    // --- Warning threshold (brief §8: "still feasible but significantly degraded") ------

    /// Remaining delta-v below this fraction of what the remaining route requires (i.e.
    /// `remaining / required < warningDeltaVMarginFraction`) is reported as `Warning`,
    /// not `Invalid` -- still flyable, but with little margin for a further surprise.
    double warningDeltaVMarginFraction = 1.15;

    /// Remaining time to deadline below this fraction of the remaining route's own transfer
    /// time is `Warning` -- the mission will make it, but barely.
    double warningDeadlineMarginFraction = 1.10;

    // --- Priority / preemption (brief §23) -----------------------------------

    /// A newly-arrived (or escalated) emergency must exceed the active mission's own
    /// emergency priority by at least this much to be considered for preemption at all --
    /// `OPTIMIZATION_MODEL.md` §7's priority is in [0,1], so this is on the same scale.
    double preemptionPriorityMargin = 0.15;

    /// Mission progress (0-1, `Mission::progressOnLeg()` combined with leg position) at or
    /// above this fraction of completion is never preempted, regardless of the new
    /// emergency's priority -- brief §23: "low-priority emergency should not interrupt an
    /// almost-complete critical rescue." Deliberately a hard cutoff, not another margin,
    /// so an almost-finished rescue is never second-guessed by a policy tuning mistake.
    double preemptionMaxProgressFraction = 0.85;

    // --- Throttling (brief §47) -----------------------------------------------

    /// Minimum simulation-time gap between two replans of the SAME mission. A change
    /// arriving sooner than this after the mission's last replan is still evaluated for
    /// mandatory replanning (infeasibility is never throttled) but is not eligible for an
    /// OPTIONAL switch. `0.0` disables throttling.
    Seconds minimumReplanIntervalSec = 0.0;
};

[[nodiscard]] constexpr ReplanPolicy defaultReplanPolicy() noexcept { return ReplanPolicy{}; }

}  // namespace orca
