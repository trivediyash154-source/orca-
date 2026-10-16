#pragma once

/// @file AssignmentCost.hpp
/// Turns scored (vehicle, emergency) candidates into costs the assignment matrix can
/// compare **across different pairs** — the conceptual review Phase 6 brief §9 and §22
/// demand explicitly, not assumed away.
///
/// # WHY FRONT-RELATIVE NORMALISATION DOES NOT TRANSFER TO ASSIGNMENT
///
/// `MissionScorer::scorePlan()` (Phase 5) normalises a route's cost against *that specific
/// (vehicle, emergency) pair's own* `ideal`/`nadir` — deliberately, per
/// `OPTIMIZATION_MODEL.md` §5.1: "weights describe relative position within the actually-
/// available options," at the stated cost that "scores are not comparable across different
/// planning problems." Two different (vehicle, emergency) pairs are, precisely, two
/// different planning problems — each has its own front, its own ideal, its own nadir. A
/// `scorePlan()` result of `0.3` for (V1,E1) and `0.3` for (V2,E3) do not mean the same
/// thing, and comparing them directly in an assignment matrix would be comparing
/// incommensurable numbers while appearing not to.
///
/// **This document corrects a specific, real inaccuracy `OPTIMIZATION_MODEL.md` §8.1
/// contained since Phase 0**: it claimed assignment-matrix entries were "comparable within
/// a row (same emergency, same front)." That was written before Phase 5 established the
/// actual architecture — every (vehicle, emergency) pair gets its *own* front, never a
/// front shared across vehicles for one emergency — so "same front" was never true for any
/// architecture Phase 5 could have built consistently with the rest of this project's
/// design. Checked directly (not merely suspected) by constructing two vehicles with very
/// different fronts to the same emergency and confirming their front-relative scores are
/// not comparable (`assignment_cost_test.cpp`).
///
/// # THE FIX: A SEPARATE, EXPLICIT ASSIGNMENT-LEVEL NORMALISATION
///
/// `computeGlobalIdeal`/`computeGlobalNadir` compute `ideal`/`nadir` across **every
/// feasible candidate's selected-plan cost in the entire matrix**, not per pair. Every
/// candidate is then normalised and scored against those *shared* bounds via the exact
/// same `optimization::normalize()`/`scorePlan()` machinery Phase 5 built — no new scoring
/// math, only new (global, not per-pair) bounds fed into it. This makes every cell in the
/// assignment matrix answer the same question ("how good is this candidate relative to the
/// full range of what any vehicle could achieve for any emergency this cycle") rather than
/// each cell answering a different, incomparable question.
///
/// # THE COST FORMULA
///
/// `OPTIMIZATION_MODEL.md` §8's formula, reused exactly (not reinvented), with the
/// corrected normalisation basis substituted for `score(...)`:
///
///     cost(V, E) = globalScore(V, E) * (2 - priority(E))
///
/// `priority(E)` is computed once per emergency (`EmergencyPriority.hpp`), not once per
/// pair — it is a property of the emergency, not of any specific vehicle's candidate for
/// it. `(2 - priority)` ranges over `[1,2]`, so it biases the minimiser toward
/// higher-priority emergencies without letting priority alone overwhelm the underlying
/// mission-quality score (`OPTIMIZATION_MODEL.md` §8's own stated bound).
///
/// # INFEASIBLE ENTRIES: A DOCUMENTED FINITE SENTINEL, NEVER `infinity()`
///
/// `kInfeasibleAssignmentCost` is a large, finite, documented constant — not
/// `std::numeric_limits<double>::infinity()`, which the Phase 6 brief explicitly warns
/// against using inside arithmetic Hungarian will perform (subtraction during row/column
/// reduction could produce `inf - inf = NaN`, silently corrupting the matrix). Bounded
/// well above any real feasible cost (feasible costs are bounded in `[0,2]` by the formula
/// above), so no real assignment can ever be mistaken for, or numerically confused with, an
/// infeasible one.

#include <vector>

#include "../core/ObjectiveVector.hpp"
#include "../core/Types.hpp"
#include "../domain/Emergency.hpp"
#include "EmergencyPriority.hpp"
#include "MissionCandidate.hpp"
#include "OptimizationProfile.hpp"

namespace orca {

/// Large enough that no real, feasible assignment cost (bounded in [0,2] by the formula
/// above) could ever be mistaken for it, small enough to stay well within `double`'s safe
/// arithmetic range for subtraction during Hungarian's row/column reduction.
inline constexpr double kInfeasibleAssignmentCost = 1.0e9;

/// Componentwise minimum of every FEASIBLE candidate's selected-plan cost, across the
/// entire matrix (not per pair). Returns the zero vector if no candidate is feasible.
[[nodiscard]] ObjectiveVector computeGlobalIdeal(const std::vector<CandidateFront>& fronts,
                                                 const std::vector<ScoredCandidate>& scored);

/// Componentwise maximum, same scope as `computeGlobalIdeal`.
[[nodiscard]] ObjectiveVector computeGlobalNadir(const std::vector<CandidateFront>& fronts,
                                                 const std::vector<ScoredCandidate>& scored);

struct AssignmentCostEntry {
    VehicleId vehicle;
    EmergencyId emergency;
    bool feasible = false;
    double cost = kInfeasibleAssignmentCost;

    /// The emergency's own priority (not vehicle-specific), retained for diagnostics and
    /// explanation (Phase 6 brief §15) — not part of what Hungarian minimises directly,
    /// since it is already folded into `cost`.
    double priority = 0.0;
};

/// Builds one cost entry per candidate. `emergencies` and `nowSec` are needed to compute
/// each emergency's priority exactly once (a property of the emergency, shared by every
/// vehicle's entry for it) — `feasiblePlanExists` for that computation is true iff *any*
/// candidate for that emergency, across all vehicles, is feasible.
[[nodiscard]] std::vector<AssignmentCostEntry> buildAssignmentCosts(
    const std::vector<CandidateFront>& fronts, const std::vector<ScoredCandidate>& scored,
    const std::vector<Emergency>& emergencies, Seconds nowSec,
    const OptimizationProfile& profile,
    const PriorityWeights& priorityWeights = defaultPriorityWeights());

}  // namespace orca
