#pragma once

/// @file MissionScorer.hpp
/// Turns a `ParetoFront` plus an `OptimizationProfile` into one recommended plan — the
/// **Preference Selection** stage, kept structurally separate from search and dominance
/// (`ParetoSearch.hpp`'s file comment, Phase 5 brief §5). This file never constructs a
/// `ParetoFront` and never calls `findParetoRoutes()`; it only reads what it is given.
///
/// ## Selection is `O(|front| * k)`, not a re-search
///
/// `OPTIMIZATION_MODEL.md` §6.2-6.3: `score(plan) = sum_i w_i * norm[i](plan)`,
/// `recommended = argmin`. With `|front| ~ 40` and `k = 4`, that's on the order of 160
/// multiply-adds — microseconds. This is what makes re-scoring under a changed profile
/// cheap enough to run on every UI slider tick without re-running search.
///
/// ## Deterministic tie-breaking, total order
///
/// `OPTIMIZATION_MODEL.md` §10's exact policy, applied in order until a unique winner
/// remains: lowest score, then lowest risk, then lowest time, then lowest fuel, then
/// lowest `PlanId` — the final, guaranteed-unique arbiter (`PlanId`s are assigned in
/// deterministic generation order, `ParetoFront.hpp`). Without a guaranteed-unique final
/// key, two equal-scoring plans could be selected differently depending on container
/// iteration order — exactly the "deterministic except when it isn't" bug class this
/// policy exists to close off.

#include <optional>
#include <vector>

#include "ObjectiveNormalizer.hpp"
#include "OptimizationProfile.hpp"
#include "ParetoFront.hpp"

namespace orca {

struct ScoreBreakdown {
    NormalizedObjectives normalized;

    /// `weightedX = profile.wX (normalised to sum 1) * normalized.X` — the per-objective
    /// contribution to `totalScore`, exposed so a future explanation panel can show "why"
    /// (Phase 5 brief §33, §62) without recomputing anything.
    double weightedTime = 0.0;
    double weightedFuel = 0.0;
    double weightedRisk = 0.0;
    double weightedDistance = 0.0;

    double totalScore = 0.0;  ///< sum of the four weighted contributions; lower is better
};

/// Scores `cost` against `front`'s own `ideal`/`nadir` under `profile`. `profile`'s
/// weights are normalised to sum to 1 internally (`OPTIMIZATION_MODEL.md` §6.2) — only
/// their ratios matter to the caller.
///
/// @throws std::invalid_argument if `ideal`/`nadir` are inconsistent (see
///         `ObjectiveNormalizer.hpp`'s `normalize()`).
[[nodiscard]] ScoreBreakdown scorePlan(const ObjectiveVector& cost,
                                       const ObjectiveVector& ideal,
                                       const ObjectiveVector& nadir,
                                       const OptimizationProfile& profile);

/// One `ScoreBreakdown` per entry in `front.solutions`, same order, same indices — the
/// full breakdown for every plan on the front, for a UI that wants to show them all
/// (Phase 5 brief §33), not just the recommendation.
[[nodiscard]] std::vector<ScoreBreakdown> scoreAllPlans(const ParetoFront& front,
                                                        const OptimizationProfile& profile);

struct RecommendedPlan {
    /// Index into the `ParetoFront::solutions` the recommendation was selected from — not
    /// a pointer or a copy, so it carries no lifetime assumption beyond "the same `front`
    /// used to compute it is still around," and no unnecessary `MissionPlan` copy
    /// (Phase 5 brief §26, §64).
    std::size_t planIndex = 0;
    ScoreBreakdown breakdown;
};

/// Selects the recommended plan from `front.solutions` under `profile`, using the
/// deterministic tie-break policy in the file comment.
///
/// @returns `std::nullopt` if `front.solutions` is empty (no feasible plan to recommend —
///          a normal, expected outcome, `OPTIMIZATION_MODEL.md` §9, never an error).
[[nodiscard]] std::optional<RecommendedPlan> selectRecommendedPlan(
    const ParetoFront& front, const OptimizationProfile& profile);

}  // namespace orca
