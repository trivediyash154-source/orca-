/// Tests for `OptimizationProfile`: weight validation and the preset values, checked
/// against `configs/profiles.json`'s literal numbers (`OptimizationProfile.hpp`'s file
/// comment explains why they are hardcoded rather than loaded, and that this test exists
/// so the two cannot silently drift apart).

#include "../../cpp/optimization/OptimizationProfile.hpp"
#include "../TestFramework.hpp"

using namespace orca;

// ---------------------------------------------------------------------------
// Preset values -- must match configs/profiles.json exactly.
// ---------------------------------------------------------------------------

ORCA_TEST(OptimizationProfile, FastestMatchesConfigsProfilesJson) {
    const OptimizationProfile p = OptimizationProfile::fastest();
    CHECK(p.kind == ProfileKind::Fastest);
    CHECK_NEAR(p.wTime, 0.70, 1e-12);
    CHECK_NEAR(p.wFuel, 0.10, 1e-12);
    CHECK_NEAR(p.wRisk, 0.15, 1e-12);
    CHECK_NEAR(p.wDistance, 0.05, 1e-12);
}

ORCA_TEST(OptimizationProfile, FuelSavingMatchesConfigsProfilesJson) {
    const OptimizationProfile p = OptimizationProfile::fuelSaving();
    CHECK(p.kind == ProfileKind::FuelSaving);
    CHECK_NEAR(p.wTime, 0.10, 1e-12);
    CHECK_NEAR(p.wFuel, 0.70, 1e-12);
    CHECK_NEAR(p.wRisk, 0.15, 1e-12);
    CHECK_NEAR(p.wDistance, 0.05, 1e-12);
}

ORCA_TEST(OptimizationProfile, SafetyFirstMatchesConfigsProfilesJson) {
    const OptimizationProfile p = OptimizationProfile::safetyFirst();
    CHECK(p.kind == ProfileKind::SafetyFirst);
    CHECK_NEAR(p.wTime, 0.15, 1e-12);
    CHECK_NEAR(p.wFuel, 0.10, 1e-12);
    CHECK_NEAR(p.wRisk, 0.70, 1e-12);
    CHECK_NEAR(p.wDistance, 0.05, 1e-12);
}

ORCA_TEST(OptimizationProfile, BalancedMatchesConfigsProfilesJson) {
    const OptimizationProfile p = OptimizationProfile::balanced();
    CHECK(p.kind == ProfileKind::Balanced);
    CHECK_NEAR(p.wTime, 0.30, 1e-12);
    CHECK_NEAR(p.wFuel, 0.30, 1e-12);
    CHECK_NEAR(p.wRisk, 0.30, 1e-12);
    CHECK_NEAR(p.wDistance, 0.10, 1e-12);
}

ORCA_TEST(OptimizationProfile, EveryPresetWeightsSumToOne) {
    // configs/profiles.json's weights are already normalised at the source; confirmed
    // here so a future edit to either file is caught by this test, not just eyeballed.
    for (const OptimizationProfile& p :
        {OptimizationProfile::fastest(), OptimizationProfile::fuelSaving(),
         OptimizationProfile::safetyFirst(), OptimizationProfile::balanced()}) {
        CHECK_NEAR(p.wTime + p.wFuel + p.wRisk + p.wDistance, 1.0, 1e-9);
    }
}

// ---------------------------------------------------------------------------
// Weight validation (Phase 5 brief §7, §30-31, §61)
// ---------------------------------------------------------------------------

ORCA_TEST(OptimizationProfile, CustomAcceptsValidWeights) {
    const OptimizationProfile p = OptimizationProfile::custom(0.4, 0.3, 0.2, 0.1);
    CHECK(p.kind == ProfileKind::Custom);
    CHECK_NEAR(p.wTime, 0.4, 1e-12);
    CHECK_NEAR(p.wFuel, 0.3, 1e-12);
    CHECK_NEAR(p.wRisk, 0.2, 1e-12);
    CHECK_NEAR(p.wDistance, 0.1, 1e-12);
}

ORCA_TEST(OptimizationProfile, CustomAcceptsWeightsNotSummingToOne) {
    // Weights are stored as given; normalisation to sum 1 happens at point of use
    // (MissionScorer.hpp), not here -- 0.92 total is a legitimate stored profile.
    const OptimizationProfile p = OptimizationProfile::custom(0.5, 0.3, 0.1, 0.02);
    CHECK_NEAR(p.wTime + p.wFuel + p.wRisk + p.wDistance, 0.92, 1e-12);
}

ORCA_TEST(OptimizationProfile, RejectsAllZeroWeights) {
    CHECK_THROWS(OptimizationProfile::custom(0.0, 0.0, 0.0, 0.0));
}

ORCA_TEST(OptimizationProfile, RejectsAnyNegativeWeight) {
    CHECK_THROWS(OptimizationProfile::custom(-0.1, 0.4, 0.4, 0.3));
    CHECK_THROWS(OptimizationProfile::custom(0.4, -0.1, 0.4, 0.3));
    CHECK_THROWS(OptimizationProfile::custom(0.4, 0.4, -0.1, 0.3));
    CHECK_THROWS(OptimizationProfile::custom(0.4, 0.4, 0.3, -0.1));
}

ORCA_TEST(OptimizationProfile, AcceptsOneObjectiveWeightedToZeroAsLongAsSumIsPositive) {
    // Zero on a SINGLE objective (not all four) is legitimate -- it means "ignore this
    // objective entirely," a valid (if perhaps unwise) operator choice, distinct from the
    // all-zero case which has no meaningful score at all.
    const OptimizationProfile p = OptimizationProfile::custom(0.5, 0.5, 0.0, 0.0);
    CHECK_NEAR(p.wRisk, 0.0, 1e-12);
    CHECK_NEAR(p.wDistance, 0.0, 1e-12);
}

ORCA_TEST(OptimizationProfile, ToStringMatchesWireNames) {
    CHECK_EQ(std::string(toString(ProfileKind::Fastest)), std::string("FASTEST"));
    CHECK_EQ(std::string(toString(ProfileKind::FuelSaving)), std::string("FUEL_SAVING"));
    CHECK_EQ(std::string(toString(ProfileKind::SafetyFirst)), std::string("SAFETY_FIRST"));
    CHECK_EQ(std::string(toString(ProfileKind::Balanced)), std::string("BALANCED"));
    CHECK_EQ(std::string(toString(ProfileKind::Custom)), std::string("CUSTOM"));
}
