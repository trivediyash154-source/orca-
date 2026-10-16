#include "OptimizationProfile.hpp"

#include "../core/Validation.hpp"

namespace orca {

namespace {

void validateWeights(double wTime, double wFuel, double wRisk, double wDistance) {
    requireThat(wTime >= 0.0 && wFuel >= 0.0 && wRisk >= 0.0 && wDistance >= 0.0,
                "OptimizationProfile: all weights must be >= 0");
    requireThat(wTime + wFuel + wRisk + wDistance > 0.0,
                "OptimizationProfile: at least one weight must be > 0 (all-zero profile "
                "rejected, ARCHITECTURE.md §11)");
}

}  // namespace

OptimizationProfile OptimizationProfile::custom(double wTime, double wFuel, double wRisk,
                                                double wDistance) {
    validateWeights(wTime, wFuel, wRisk, wDistance);
    return OptimizationProfile{ProfileKind::Custom, wTime, wFuel, wRisk, wDistance};
}

// Values below are copied from configs/profiles.json verbatim -- see the file comment on
// why this duplication exists and how it is kept honest.

OptimizationProfile OptimizationProfile::fastest() noexcept {
    return OptimizationProfile{ProfileKind::Fastest, 0.70, 0.10, 0.15, 0.05};
}

OptimizationProfile OptimizationProfile::fuelSaving() noexcept {
    return OptimizationProfile{ProfileKind::FuelSaving, 0.10, 0.70, 0.15, 0.05};
}

OptimizationProfile OptimizationProfile::safetyFirst() noexcept {
    return OptimizationProfile{ProfileKind::SafetyFirst, 0.15, 0.10, 0.70, 0.05};
}

OptimizationProfile OptimizationProfile::balanced() noexcept {
    return OptimizationProfile{ProfileKind::Balanced, 0.30, 0.30, 0.30, 0.10};
}

}  // namespace orca
