#pragma once

/// @file Dominance.hpp
/// The single predicate every Pareto-search correctness argument in `ParetoSearch.cpp`
/// rests on: does cost vector `a` make `b` strictly unnecessary to keep.
///
/// ## Why this lives in `algorithms/`, not `optimization/`
///
/// The Phase 5 brief's illustrative directory sketch places `Dominance` under
/// `optimization/`. That is not possible here, for the exact reason `ObjectiveVector`
/// itself was moved from `optimization/` to `core/` in Phase 1
/// (`ObjectiveVector.hpp`'s file comment): `ParetoSearch.cpp` calls `dominates()` at every
/// label insertion, and `ARCHITECTURE.md`'s dependency table has `optimization/` depend on
/// `algorithms/`, never the reverse. Putting `Dominance` in `optimization/` would force
/// `algorithms/` to depend upward on it, recreating the cycle P2 forbids. `optimization/`
/// (front construction, normalisation, scoring) uses this same `dominates()` too — it
/// simply reaches down to `algorithms/` for it, the same direction it already reaches
/// down for everything else it needs.
///
/// ## Two epsilons, deliberately never confused
///
/// `dominates()` uses a **fixed, small, non-configurable** floating-point noise tolerance
/// — the same role and magnitude (`1e-6`) `Dijkstra.cpp`/`AStar.cpp`'s relaxation epsilon
/// already plays, justified the same way (`TRANSFER_MODEL.md` §13.3's few-ulp noise
/// floor). This is "exact mode": it exists only so that two independently computed
/// doubles that are mathematically equal do not spuriously fail to compare as equal. It is
/// not a knob, and it cannot be loosened by a caller.
///
/// `dominatesWithEpsilon()` is a **separate, explicitly-named function** for controlled
/// approximation (ε-dominance, ALGORITHMS.md §5.7) — deliberately requiring the caller to
/// pass a real, meaningful epsilon every time, with no default. This makes it impossible
/// to reach approximate pruning by accident: the exact-mode default (`dominates`) and the
/// approximate mode (`dominatesWithEpsilon`) are different functions with different names,
/// not one function with a dial that happens to default to something safe today.

#include "../core/ObjectiveVector.hpp"

namespace orca {

/// Floating-point noise tolerance for exact-mode dominance and equivalence — not an
/// approximation parameter. See the file comment.
inline constexpr double kDominanceNoiseEpsilon = 1.0e-6;

/// True if `a` dominates `b`: no worse than `b` in every objective, strictly better in at
/// least one, within `kDominanceNoiseEpsilon` noise tolerance. Irreflexive, asymmetric,
/// transitive, and a partial (not total) order — property-tested in `dominance_test.cpp`,
/// not merely asserted (ALGORITHMS.md §6.4).
[[nodiscard]] bool dominates(const ObjectiveVector& a, const ObjectiveVector& b) noexcept;

/// True if `a` and `b` represent the same point in objective space, within
/// `kDominanceNoiseEpsilon` on every dimension. **Not** the same predicate as
/// `!dominates(a,b) && !dominates(b,a)` — two genuinely incomparable vectors (one better on
/// time, the other on fuel) also satisfy that, but are not equivalent. This function
/// exists so the search can tell "these are duplicates of the same route shape" apart from
/// "these are a real trade-off, both must survive" (Phase 5 brief §9-10).
[[nodiscard]] bool isEquivalent(const ObjectiveVector& a, const ObjectiveVector& b) noexcept;

/// Controlled-approximation dominance: `a` ε-dominates `b` if `a[i] <= b[i] * (1+epsilon)`
/// for every `i`, with at least one dimension where `a` is genuinely (noise-tolerance)
/// better. Treats near-identical labels as dominated, trading completeness for a smaller
/// frontier. `epsilon` must be passed explicitly and is not defaulted — see the file
/// comment for why. `epsilon < 0` is rejected (`std::invalid_argument`): a negative
/// tolerance would make dominance *stricter* than exact mode, which is not what
/// ε-dominance means and is almost certainly a caller error.
///
/// @throws std::invalid_argument if `epsilon < 0`.
[[nodiscard]] bool dominatesWithEpsilon(const ObjectiveVector& a, const ObjectiveVector& b,
                                       double epsilon);

}  // namespace orca
