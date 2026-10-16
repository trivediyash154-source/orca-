#pragma once

/// @file MissionCandidate.hpp
/// One (vehicle, emergency) pair's candidate data, at two distinct stages — kept as two
/// separate structs for the same reason `ParetoSearch`/`MissionScorer` are separate
/// functions (`ParetoSearch.hpp`'s file comment): re-scoring under a different
/// `OptimizationProfile` must never re-run search. `CandidateFront` is the search-stage
/// output; `ScoredCandidate` is the scoring-stage output built from it. Phase 6 brief §8's
/// "no re-search on weight change" requirement is what this split exists to make
/// structurally true, not just documented.

#include <optional>

#include "../core/Types.hpp"
#include "MissionScorer.hpp"
#include "ParetoFront.hpp"

namespace orca {

/// The result of running Pareto search once for one (vehicle, emergency) pair. Does not
/// depend on any `OptimizationProfile` — building this is the only step that ever calls
/// `findParetoRoutes()`.
struct CandidateFront {
    VehicleId vehicle;
    EmergencyId emergency;
    bool feasible = false;

    /// Set iff `!feasible`. `std::nullopt` when feasible -- there is no "reason" for a
    /// success. Reuses the existing `ConstraintKind` (`core/Types.hpp`,
    /// `MissionPlan::ConstraintViolation`'s own type) rather than a new enum, per the
    /// project's established "use existing structures if equivalent ones exist"
    /// discipline.
    std::optional<ConstraintKind> infeasibilityReason;

    /// Empty (`front.isEmpty()`) iff `!feasible`. Retained even when infeasible so
    /// `front.labelsGenerated`/`front.truncated` stay available for diagnostics.
    ParetoFront front;
};

/// One (vehicle, emergency) candidate scored under a specific `OptimizationProfile` --
/// route-level score only (`MissionScorer::ScoreBreakdown`), not yet the assignment-level
/// cost (`AssignmentCost.hpp` builds that from this, plus emergency priority, plus
/// cross-candidate normalisation -- Phase 6 brief §9, §22).
struct ScoredCandidate {
    VehicleId vehicle;
    EmergencyId emergency;
    bool feasible = false;
    std::optional<ConstraintKind> infeasibilityReason;

    /// Index into the corresponding `CandidateFront::front.solutions` -- not a copy of the
    /// `MissionPlan`, so scoring every candidate in a large matrix does not copy every
    /// plan on every front (Phase 6 brief §14 "adapt... where appropriate", mirroring
    /// `RecommendedPlan::planIndex`'s own reasoning in `MissionScorer.hpp`). `std::nullopt`
    /// iff `!feasible`.
    std::optional<std::size_t> selectedPlanIndex;

    /// `std::nullopt` iff `!feasible`.
    std::optional<ScoreBreakdown> breakdown;
};

}  // namespace orca
