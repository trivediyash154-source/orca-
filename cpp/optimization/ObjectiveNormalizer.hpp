#pragma once

/// @file ObjectiveNormalizer.hpp
/// Front-relative normalisation — `OPTIMIZATION_MODEL.md` §5's formula, implemented
/// exactly as specified there, not reinvented.
///
///     norm[i] = (cost[i] - ideal[i]) / (nadir[i] - ideal[i])        -> [0,1]
///     if nadir[i] - ideal[i] < epsilon:  norm[i] = 0                 // constant objective
///
/// ## Why front-relative, not globally bounded
///
/// `OPTIMIZATION_MODEL.md` §5.1 gives the full argument: normalising against the front's
/// own ideal/nadir means weights describe relative position within the options that
/// actually exist. The cost, stated there and repeated here rather than silently
/// inherited: scores are **not comparable across different planning problems** — a
/// `normalizedTime = 0.3` for one search and `0.3` for another do not mean the same thing.
/// `MissionScorer.hpp` never compares raw scores across different `ParetoFront`s for this
/// reason.
///
/// ## The degenerate case is handled, not divided-by-zero
///
/// When every solution on the front has the same value for an objective (including the
/// single-solution front, where this holds for all four), `nadir[i] - ideal[i] == 0`, and
/// the formula above would divide by zero. `OPTIMIZATION_MODEL.md` §5.2 specifies the
/// answer: that objective contributes `0` to normalisation for every plan, correctly
/// rendering it decision-irrelevant (every plan ties on it) rather than crashing or
/// producing `NaN`.

#include <vector>

#include "../core/ObjectiveVector.hpp"

namespace orca {

/// Same magnitude and role as `kDominanceNoiseEpsilon` (`Dominance.hpp`): a floating-point
/// noise floor, not a tunable. `nadir[i] - ideal[i]` below this is treated as exactly
/// constant.
inline constexpr double kNormalizationDegenerateEpsilon = 1.0e-9;

/// Four dimensionless values in `[0,1]`, deliberately a distinct type from
/// `ObjectiveVector`: an `ObjectiveVector`'s fields carry specific physical units
/// (`DATA_MODEL.md` §0.1) and this does not — reusing the same type would let a normalised
/// score be passed somewhere expecting seconds or kilometres with no compiler warning.
struct NormalizedObjectives {
    double time = 0.0;
    double fuel = 0.0;
    double risk = 0.0;
    double distance = 0.0;
};

/// Componentwise minimum across `front`. Returns the zero vector for an empty `front` —
/// callers only use the result when `!front.empty()`.
[[nodiscard]] ObjectiveVector computeIdeal(const std::vector<ObjectiveVector>& front);

/// Componentwise maximum across `front`.
[[nodiscard]] ObjectiveVector computeNadir(const std::vector<ObjectiveVector>& front);

/// Normalises `cost` against `ideal`/`nadir`, per the file comment's formula.
///
/// @throws std::invalid_argument if any `nadir[i] < ideal[i]` — `ideal` must be
///         componentwise no worse than `nadir` by construction (both are extrema of the
///         same front), so a violation here means the caller passed mismatched bounds.
[[nodiscard]] NormalizedObjectives normalize(const ObjectiveVector& cost,
                                             const ObjectiveVector& ideal,
                                             const ObjectiveVector& nadir);

}  // namespace orca
