#pragma once

/// @file ParetoLabel.hpp
/// One partial route in the label-setting search — the multi-objective analogue of
/// Dijkstra's `bestArrival[node]`, except a node may hold many simultaneously-useful
/// labels instead of one.
///
/// ## Flat pool, index-based predecessors — not pointers
///
/// Labels live in one `std::vector<ParetoLabel>` (`ParetoSearch.cpp`), addressed by
/// `LabelId`. A `std::vector` reallocates as it grows; a pointer into it would dangle the
/// moment that happens, so predecessor chains are `std::optional<LabelId>` — a stable
/// index — never `ParetoLabel*` (Phase 5 brief §64, ALGORITHMS.md §5.3).
///
/// ## Why `legResult` is stored, not looked up from the edge
///
/// Same reasoning as `PathLeg` (`PathResult.hpp`): a time-dependent edge's cost depends on
/// *when* it is used, so the edge's own cached `referenceResult` is only ever correct for
/// one specific departure time. Each label stores the actual `TransferResult` computed at
/// its own predecessor's real arrival time, exactly as `PathLeg` does — this is
/// intentional reuse of an already-correct pattern, not a parallel invention.
///
/// ## Why `cost.fuel` is the cumulative delta-v state, not a separate field
///
/// The Phase 5 brief (§22) warns that `objective.fuel` and a vehicle's delta-v budget "are
/// not necessarily identical," and to store cumulative delta-v explicitly if so. Checked
/// directly against `TransferModel.cpp`: `fuelCost = deltaVKms / constraints.deltaVBudgetKms`
/// per leg, and `deltaVBudgetKms` is the same constant (the searching vehicle's total
/// capability, `SearchConstraints::vehicleDeltaVBudgetKms`) for every relaxation in a
/// single search. Since `ObjectiveVector::compose()` adds `fuel` additively,
/// `label.cost.fuel` after `n` legs equals `(sum of every leg's deltaVKms) / budget` —
/// exactly the accumulated-delta-v-over-budget ratio the cumulative constraint needs.
/// `label.cost.fuel > 1.0` (with tolerance) is therefore both necessary and sufficient to
/// detect a cumulative delta-v violation, with no redundant field required. This was
/// verified against the real formula, not assumed.

#include <optional>

#include "../core/ObjectiveVector.hpp"
#include "../core/Types.hpp"
#include "../transfer/TransferTypes.hpp"

namespace orca {

struct ParetoLabel {
    NodeId node;
    ObjectiveVector cost;  ///< accumulated from source via ObjectiveVector::compose()

    /// `std::nullopt` only for a root label (the label a search starts with at `source`).
    std::optional<LabelId> predecessor;
    std::optional<EdgeId> viaEdge;
    std::optional<TransferResult> legResult;  ///< the actual evaluation of `viaEdge`

    Seconds arrivalTimeSec = 0.0;  ///< absolute time; departure time for edges out of `node`

    /// Lazy-deletion flag: false once this label has been dominated or found equivalent to
    /// a survivor (`Dominance.hpp`). Mirrors Dijkstra/A*'s `bestArrival`-vs-current-entry
    /// staleness check, generalised to "this specific label is no longer in the
    /// non-dominated set," rather than "this node has a single better value now."
    bool active = true;
};

}  // namespace orca
