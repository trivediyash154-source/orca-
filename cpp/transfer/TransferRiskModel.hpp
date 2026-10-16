#pragma once

/// @file TransferRiskModel.hpp
/// Baseline transfer risk.
///
/// > **This is a simulation risk model.** It produces a plausible, monotonic, tunable
/// > hazard score in [0,1]. It is **not** a collision probability and is not calibrated
/// > against any conjunction-assessment method. SIMULATION_MODEL.md §5.
///
/// ## Scope in Phase 2
///
/// Three factors, all computable from the transfer geometry alone:
///
///   * **manoeuvre severity** — total delta-v relative to the vehicle's budget. Bigger
///     burns mean more that can go wrong.
///   * **plane-change share** — what fraction of the delta-v goes into rotating the
///     orbital plane. Plane changes are the least forgiving part of the manoeuvre, and
///     this term is why an inclined transfer scores worse than an equally expensive
///     coplanar one.
///   * **arrival fuel margin** — how close to empty the vehicle arrives. Arriving with no
///     reserve is dangerous independently of how the delta-v was spent.
///
/// **Debris proximity is deliberately absent.** It needs the spatial index and a
/// populated environment, which arrive in Phase 9. Rather than ship a dead zero-weighted
/// term now, the fourth factor will be *added* then and the weights renormalised. A
/// weight of zero in the code would look like a feature and behave like nothing.
///
/// ## Determinism
///
/// Every factor is a pure function of the transfer geometry and the vehicle's budget.
/// There is no randomness here at all, so the same transfer always scores identically —
/// which is what lets risk participate in a reproducible objective vector.

#include "../core/Units.hpp"

namespace orca {

/// Weights for the baseline risk factors. Sum to 1.
///
/// These are **modelling choices, not physical facts**. They carry defaults here and move
/// to `configs/risk.json` when the loader lands in Phase 14, following the same pattern as
/// Phase 1's `HealthThresholds`.
struct TransferRiskWeights {
    double manoeuvreSeverity = 0.45;
    double planeChangeShare = 0.35;
    double arrivalFuelMargin = 0.20;

    [[nodiscard]] constexpr double sum() const noexcept {
        return manoeuvreSeverity + planeChangeShare + arrivalFuelMargin;
    }
};

/// Inputs the risk model needs. Passed explicitly rather than reaching into a vehicle, so
/// the model stays a pure function and can be tested without constructing entities.
struct TransferRiskInputs {
    KmPerSec totalDeltaVKms = 0.0;
    KmPerSec planeChangeDeltaVKms = 0.0;
    KmPerSec deltaVBudgetKms = 1.0;      ///< the vehicle's total capability
    KmPerSec deltaVRemainingKms = 1.0;   ///< before this transfer
};

/// Estimate transfer risk in [0,1].
///
/// Monotonic in each factor: more delta-v, a larger plane-change share, or a thinner
/// arrival margin never *lowers* the score. Phase 3's dominance pruning does not require
/// this of the risk function itself, but it makes the model explicable — "why is this
/// route riskier?" always has an answer.
[[nodiscard]] double estimateTransferRisk(const TransferRiskInputs& inputs,
                                          const TransferRiskWeights& weights = {}) noexcept;

}  // namespace orca
