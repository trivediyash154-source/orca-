#include "TransferRiskModel.hpp"

#include <cassert>

#include "../core/Math.hpp"

namespace orca {

double estimateTransferRisk(const TransferRiskInputs& inputs,
                            const TransferRiskWeights& weights) noexcept {
    assert(inputs.deltaVBudgetKms > 0.0 && "risk model requires a positive delta-v budget");
    assert(weights.sum() > 0.0 && "risk weights must not all be zero");

    // Manoeuvre severity: how much of the vehicle's total capability this burn consumes.
    // Clamped because a transfer can be priced above the budget -- that is an
    // infeasibility for the feasibility layer to judge, not a reason for risk to exceed 1.
    const double severity = clampUnit(inputs.totalDeltaVKms / inputs.deltaVBudgetKms);

    // Plane-change share: what proportion of the burn is spent rotating the plane.
    // Guarded against a zero-delta-v transfer, where the ratio is undefined and the
    // honest answer is "no plane-change risk".
    const double planeShare =
        inputs.totalDeltaVKms > kEpsilon
            ? clampUnit(inputs.planeChangeDeltaVKms / inputs.totalDeltaVKms)
            : 0.0;

    // Arrival margin: 0 when the vehicle arrives with a full budget still in reserve,
    // 1 when it arrives empty.
    const double remainingAfter = inputs.deltaVRemainingKms - inputs.totalDeltaVKms;
    const double marginRisk = 1.0 - clampUnit(remainingAfter / inputs.deltaVBudgetKms);

    const double weighted = weights.manoeuvreSeverity * severity +
                            weights.planeChangeShare * planeShare +
                            weights.arrivalFuelMargin * marginRisk;

    // Normalise by the weight sum so callers may supply unnormalised weights and still
    // receive a score in [0,1].
    return clampUnit(weighted / weights.sum());
}

}  // namespace orca
