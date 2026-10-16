#pragma once

/// @file EmergencyPriority.hpp
/// Which casualty to serve — distinct from `MissionScorer` (`OPTIMIZATION_MODEL.md` §7 vs
/// §6), and distinct from `OptimizationProfile`'s route-selection weights
/// (`configs/priority.json` vs `configs/profiles.json`).
///
/// `Emergency.hpp` deliberately does not compute this itself: its own file comment states
/// priority "belongs to the optimization layer... because... its feasibility term requires
/// a planner." This is that layer, and Phase 6 is the first phase that actually needs it —
/// `configs/priority.json` did not exist before this phase, though the formula and its
/// defaults were already specified in `OPTIMIZATION_MODEL.md` §7 in Phase 0.
///
/// ## Priority vs profile — two, deliberately un-mergeable, weight systems
///
/// `OptimizationProfile` (`OptimizationProfile.hpp`) picks a route once a Pareto front for
/// one (vehicle, emergency) pair already exists. `PriorityWeights` here picks which
/// emergency matters more when the fleet cannot serve everyone. Folding these into one
/// weight vector would conflate "how should this vehicle fly" with "who gets a vehicle at
/// all" — two different decisions with different natural axes (time/fuel/risk/distance vs
/// severity/urgency/feasibility) and no shared unit between them.

#include "../core/Types.hpp"
#include "../core/Units.hpp"
#include "../domain/Emergency.hpp"

namespace orca {

/// @invariant every weight >= 0, sum > 0 (same shape as `OptimizationProfile`'s invariant,
/// checked the same way).
struct PriorityWeights {
    double wSeverity = 0.4;
    double wUrgency = 0.4;
    double wFeasibility = 0.2;
};

/// The weights `configs/priority.json` documents. Hardcoded here rather than loaded, for
/// the same reason `OptimizationProfile`'s presets are (`io/` — JSON loading — does not
/// exist yet); `emergency_priority_test.cpp` checks these against the config file's
/// literal contents so the two cannot silently drift.
[[nodiscard]] PriorityWeights defaultPriorityWeights() noexcept;

/// Severity's numeric score (`LOW -> 0.25` ... `CRITICAL -> 1.0`) is `core::severityScore()`
/// (`core/Types.hpp`) — already exists from an earlier phase, matching
/// `configs/priority.json`'s `severityScores` exactly; reused here rather than redefined.

/// `1.0` if at least one feasible mission plan exists for this emergency (with any
/// vehicle), `0.2` otherwise — damping, not excluding, per `OPTIMIZATION_MODEL.md` §7.1:
/// "Feasibility damps rather than excludes... a vehicle completing another mission may
/// bring it into range. Excluding it outright would make the situation permanently
/// invisible." Phase 6 has no simulation loop to revisit this later, but the same
/// principle applies within one planning pass: an emergency no *current* vehicle can
/// reach should still be visible in the priority queue at reduced priority, not vanish.
[[nodiscard]] double feasibilityScore(bool feasiblePlanExists) noexcept;

/// `priority = wSeverity*severityScore + wUrgency*urgencyAt(nowSec) + wFeasibility*
/// feasibilityScore`, normalised so the result is in `[0,1]` regardless of whether
/// `weights`' components already sum to 1 (same "normalise at point of use" policy
/// `MissionScorer::scorePlan` follows for `OptimizationProfile`).
[[nodiscard]] double computePriority(const Emergency& emergency, Seconds nowSec,
                                     bool feasiblePlanExists,
                                     const PriorityWeights& weights = defaultPriorityWeights());

}  // namespace orca
