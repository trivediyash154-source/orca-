#include "Dominance.hpp"

#include "../core/Validation.hpp"

namespace orca {

bool dominates(const ObjectiveVector& a, const ObjectiveVector& b) noexcept {
    bool strictlyBetterSomewhere = false;
    for (std::size_t i = 0; i < kObjectiveCount; ++i) {
        if (a[i] > b[i] + kDominanceNoiseEpsilon) {
            return false;  // worse somewhere -> cannot dominate
        }
        if (a[i] < b[i] - kDominanceNoiseEpsilon) {
            strictlyBetterSomewhere = true;
        }
    }
    return strictlyBetterSomewhere;
}

bool isEquivalent(const ObjectiveVector& a, const ObjectiveVector& b) noexcept {
    for (std::size_t i = 0; i < kObjectiveCount; ++i) {
        if (a[i] > b[i] + kDominanceNoiseEpsilon || a[i] < b[i] - kDominanceNoiseEpsilon) {
            return false;
        }
    }
    return true;
}

bool dominatesWithEpsilon(const ObjectiveVector& a, const ObjectiveVector& b,
                          double epsilon) {
    requireThat(epsilon >= 0.0, "dominatesWithEpsilon: epsilon must be >= 0");

    // Design choice, stated explicitly: the "strictly better somewhere" requirement below
    // uses the TIGHT (kDominanceNoiseEpsilon) comparison, not the relaxed one, so that
    // dominatesWithEpsilon(a, a, epsilon) is always false -- irreflexive, matching
    // dominates()'s property (ALGORITHMS.md §6.4), for any epsilon >= 0. Some literature
    // defines epsilon-dominance without a strict-improvement requirement at all (making it
    // reflexive by construction); that definition is deliberately not used here, so every
    // function in this file's "dominance family" behaves consistently under the same
    // property tests.
    bool strictlyBetterSomewhere = false;
    for (std::size_t i = 0; i < kObjectiveCount; ++i) {
        // a[i] must be no worse than b[i] scaled up by (1+epsilon) -- the "relaxed" bound
        // ε-dominance grants. kDominanceNoiseEpsilon is still applied underneath so this
        // degrades to exact-mode behaviour as epsilon -> 0, rather than becoming stricter
        // than exact mode at epsilon = 0.
        const double relaxedBound = b[i] * (1.0 + epsilon) + kDominanceNoiseEpsilon;
        if (a[i] > relaxedBound) {
            return false;
        }
        if (a[i] < b[i] - kDominanceNoiseEpsilon) {
            strictlyBetterSomewhere = true;
        }
    }
    return strictlyBetterSomewhere;
}

}  // namespace orca
