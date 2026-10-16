#pragma once

/// @file ObjectiveVector.hpp
/// The four-objective cost vector — ORCA's unit of currency for all optimization.
///
/// ## Why this lives in core/ and not optimization/
///
/// ARCHITECTURE.md §5 originally placed `ObjectiveVector` under `optimization/`. That is
/// not possible: `GraphEdge` (graph/) is weighted by an `ObjectiveVector`, and the
/// dependency direction is `optimization/ -> algorithms/ -> graph/`. Putting the type in
/// optimization/ would require graph/ to depend upward on optimization/, creating exactly
/// the cycle P2 forbids.
///
/// The resolution: the *type* is a core vocabulary type; optimization/ owns the
/// *operations* on it (normalisation, scoring, front construction). ARCHITECTURE.md has
/// been corrected accordingly.
///
/// ## All four objectives are minimised
///
/// Any quantity where "more is better" must be negated or inverted at its source. This is
/// not a stylistic preference: it keeps the dominance predicate (Phase 2) free of
/// per-objective knowledge, so it can loop over dimensions generically. A single
/// "except for this one, higher is better" special case would make dominance
/// non-generic and every proof about it conditional.

#include <cassert>
#include <cmath>
#include <cstddef>

namespace orca {

/// Number of objectives. Dominance and normalisation loop over this rather than
/// unrolling, so adding a fifth objective is a localised change (ARCHITECTURE.md §15).
inline constexpr std::size_t kObjectiveCount = 4;

/// A point in ORCA's four-dimensional objective space. Lower is better in every
/// component. See DATA_MODEL.md §1 for units and invariants.
struct ObjectiveVector {
    double time = 0.0;      ///< seconds, >= 0
    double fuel = 0.0;      ///< fraction of the vehicle's own delta-v budget, >= 0
    double risk = 0.0;      ///< [0,1], simulation risk
    double distance = 0.0;  ///< km, >= 0

    [[nodiscard]] constexpr double operator[](std::size_t i) const noexcept {
        switch (i) {
            case 0: return time;
            case 1: return fuel;
            case 2: return risk;
            case 3: return distance;
            default: break;
        }
        assert(false && "ObjectiveVector index out of range");
        return 0.0;
    }

    [[nodiscard]] constexpr double& operator[](std::size_t i) noexcept {
        switch (i) {
            case 0: return time;
            case 1: return fuel;
            case 2: return risk;
            case 3: return distance;
            default: break;
        }
        assert(false && "ObjectiveVector index out of range");
        return time;
    }

    friend constexpr bool operator==(const ObjectiveVector&,
                                     const ObjectiveVector&) noexcept = default;
};

/// True when every component is finite, non-negative, and risk lies in [0,1].
/// This is the DATA_MODEL.md §1 invariant, expressed once so both asserts and tests
/// can use it.
[[nodiscard]] inline bool isValid(const ObjectiveVector& v) noexcept {
    for (std::size_t i = 0; i < kObjectiveCount; ++i) {
        if (!std::isfinite(v[i]) || v[i] < 0.0) {
            return false;
        }
    }
    return v.risk <= 1.0;
}

/// Compose two cost vectors along a path: `a` then `b`.
///
/// Time, fuel and distance are additive. Risk is NOT — it composes as independent
/// survival:
///
///     risk = 1 - (1 - a.risk) * (1 - b.risk)
///
/// so two 10% legs give 19%, not 20%. Three properties follow, and the Pareto search
/// depends on all three (ALGORITHMS.md §5.6):
///
///   1. Bounded — the result cannot exceed 1 however many legs are composed.
///   2. Monotonically non-decreasing — adding a leg never lowers path risk. This is what
///      makes dominance pruning sound. If it ever became false, the Pareto search would
///      silently return wrong fronts.
///   3. Order-independent — multiplication commutes, so leg ordering does not matter.
[[nodiscard]] constexpr ObjectiveVector compose(const ObjectiveVector& a,
                                                const ObjectiveVector& b) noexcept {
    return ObjectiveVector{
        a.time + b.time,
        a.fuel + b.fuel,
        1.0 - (1.0 - a.risk) * (1.0 - b.risk),
        a.distance + b.distance,
    };
}

}  // namespace orca
