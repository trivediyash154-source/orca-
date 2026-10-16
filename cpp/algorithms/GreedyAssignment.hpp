#pragma once

/// @file GreedyAssignment.hpp
/// The deliberately simple baseline `solveSquareAssignment()` (`Hungarian.hpp`) is
/// compared against — repeatedly pick the cheapest still-available, still-feasible
/// (vehicle, emergency) pair until one side runs out, exactly as Phase 6 brief §16
/// describes it.
///
/// # WHY THIS OPERATES ON THE RECTANGULAR MATRIX, NOT HUNGARIAN'S PADDED SQUARE ONE
///
/// This was not the first design tried. Running greedy over the *same* padded square
/// matrix `optimization/AssignmentMatrix.hpp` builds for Hungarian (an appealing idea —
/// identical input, maximally fair comparison) was tried and found to be **badly wrong**,
/// caught by end-to-end smoke testing before it became a permanent test that would have
/// enshrined the bug as expected behaviour:
///
/// The padded matrix's "vehicle idle" cells cost exactly `0` (`AssignmentMatrix.hpp`'s
/// file comment — idling a vehicle really is free). A cheapest-pair-first greedy strategy,
/// applied to a matrix containing `0`-cost cells, picks every one of them before ever
/// considering a real cell with positive cost — because they are, individually, the
/// cheapest thing available. The result: greedy would assign every vehicle to an idle
/// slot and every emergency to "unassigned" before ever serving anyone, reporting a
/// trivial `totalCost = 0` with **zero real assignments** — not a legitimate baseline,
/// just a degenerate artifact of feeding a locally-greedy strategy a matrix that contains
/// a "do nothing, for free" option. Hungarian does not fall into this trap because it
/// minimises the *global* sum, where leaving emergencies unassigned costs far more in
/// aggregate (`AssignmentCost.hpp`'s unassigned-emergency penalty) — but greedy, by
/// construction, never looks at the global picture.
///
/// Padding does not exist for its own sake — it exists to give Hungarian's underlying
/// perfect-matching machinery a square input, which greedy never needed in the first
/// place. Operating directly on the natural rectangular (vehicles x emergencies) problem,
/// with infeasible cells excluded from consideration entirely rather than priced as a
/// large-but-still-comparable number, avoids the artifact and matches the brief's own
/// algorithm description exactly.

#include <cstddef>
#include <optional>
#include <vector>

namespace orca {

struct GreedyResult {
    /// `rowToColumn[i]` is the column row `i` was matched to, or `std::nullopt` if row `i`
    /// went unmatched (one side of a rectangular matrix is exhausted before the other, or
    /// every remaining pair for that row was infeasible).
    std::vector<std::optional<std::size_t>> rowToColumn;
    double totalCost = 0.0;
    int assignedCount = 0;
};

/// Deterministic tie-break: among equally-cheap feasible pairs, the one with the smallest
/// row index wins, then the smallest column index (same policy `Hungarian.hpp`'s
/// comparison partner and the rest of this codebase's tie-breaks use).
///
/// @throws std::invalid_argument if `costMatrix`/`feasible` are empty, or `feasible`'s
///         shape does not match `costMatrix`'s.
[[nodiscard]] GreedyResult solveGreedyRectangularAssignment(
    const std::vector<std::vector<double>>& costMatrix,
    const std::vector<std::vector<bool>>& feasible);

}  // namespace orca
