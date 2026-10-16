#pragma once

/// @file Hungarian.hpp
/// Minimum-cost perfect bipartite matching on a square cost matrix — the Kuhn-Munkres
/// algorithm, implemented via successive shortest augmenting paths with dual potentials
/// (the standard `O(n^3)` formulation, sometimes attributed to Jonker-Volgenant). C++20,
/// from scratch, no external solver (Phase 6 brief §11).
///
/// # DELIBERATELY DOMAIN-AGNOSTIC
///
/// This file knows nothing about vehicles, emergencies, orbital mechanics, or ORCA at all
/// — it solves "given an `n x n` cost matrix, find the permutation minimising total cost,"
/// full stop (Phase 6 brief §4: "The Hungarian algorithm must not know about orbital
/// mechanics"). Rectangular matrices, infeasibility semantics, unassigned-emergency
/// penalties and vehicle-idle-is-free padding all live one layer up, in
/// `optimization/AssignmentMatrix.hpp` — this mirrors exactly how `Dominance.hpp` knows
/// nothing about missions while `ParetoSearch.cpp` uses it, and how `MissionScorer.hpp`
/// knows nothing about search while it scores search's output.
///
/// # WHY THIS ALGORITHM, PRECISELY
///
/// Maintains row potentials `u[i]` and column potentials `v[j]` such that the *reduced
/// cost* `cost[i][j] - u[i] - v[j]` is always `>= 0`, with equality exactly on matched
/// edges once a row is fully processed. For each row in turn, it grows a shortest-path
/// tree from that row (via a Dijkstra-like relaxation over reduced costs) until it reaches
/// an unmatched column, then augments the matching along that path. Every row is
/// processed exactly once, each taking `O(n^2)` (an inner loop over all `n` columns, up to
/// `n` times as the tree grows) — `O(n^3)` total, matching the Phase 6 brief's target.
///
/// # CORRECTNESS, IN ONE PARAGRAPH (full argument: `ASSIGNMENT_MODEL.md` §15)
///
/// The potentials are a certificate: for any matching `M` respecting `cost[i][j] >= u[i] +
/// v[j]` everywhere with equality on `M`'s edges, `M`'s total cost equals `sum(u) + sum(v)`
/// — and by LP duality, no matching can cost less than `sum(u) + sum(v)` for *any* valid
/// potentials, feasible or not. The algorithm maintains feasibility of the potentials as an
/// invariant at every step (the `delta` update below adjusts them exactly enough to admit a
/// new tight edge without ever violating `cost >= u + v` elsewhere), so the final matching's
/// cost, which equals the final `sum(u) + sum(v)`, is optimal by construction — not merely
/// observed to be low.

#include <cstddef>
#include <vector>

namespace orca {

struct HungarianResult {
    /// `rowToColumn[i]` = the column row `i` is matched to. Always a valid permutation of
    /// `[0, n)` for an `n x n` input — every row gets exactly one column and vice versa,
    /// regardless of the cost values (there is no "leave unmatched" in the raw solver;
    /// that semantic lives in the domain-level wrapper via padding, see the file comment).
    std::vector<std::size_t> rowToColumn;

    /// `sum(costMatrix[i][rowToColumn[i]])` — computed once, so callers never need to
    /// re-sum it (and risk a transcription mismatch between the matrix and the result).
    double totalCost = 0.0;

    /// Number of reduced-cost comparisons performed — the operation Hungarian spends most
    /// of its time on, and the number `ASSIGNMENT_MODEL.md`'s complexity claim is measured
    /// against (Phase 6 brief §15's "Hungarian iterations/operations if practical").
    long operationCount = 0;
};

/// Solves minimum-cost perfect bipartite matching on `costMatrix`, which must be square.
///
/// @throws std::invalid_argument if `costMatrix` is not square (every row the same length
///         as the number of rows) or is empty.
[[nodiscard]] HungarianResult solveSquareAssignment(
    const std::vector<std::vector<double>>& costMatrix);

}  // namespace orca
