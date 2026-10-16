/// `generateScenario()` tests — Phase 11 brief §6-13: deterministic N-object generation,
/// explicit validation, and reproducibility across all supported sizes.

#include "../../cpp/simulation/ScenarioGenerator.hpp"
#include "../TestFramework.hpp"

using namespace orca;

ORCA_TEST(ScenarioGenerator, GeneratesTheRequestedObjectCounts) {
    const ScenarioGeneratorConfig config = scenario_family::normal(25);
    const Scenario s = generateScenario("test-25", Seed(1), config);
    CHECK_EQ(s.satellites().size(), static_cast<std::size_t>(config.satelliteCount));
    CHECK_EQ(s.vehicles().size(), static_cast<std::size_t>(config.vehicleCount));
    CHECK_EQ(s.debris().size(), static_cast<std::size_t>(config.debrisCount));
}

ORCA_TEST(ScenarioGenerator, SameSeedAndConfigProducesIdenticalScenario) {
    const ScenarioGeneratorConfig config = scenario_family::normal(50);
    const Scenario a = generateScenario("repro", Seed(928371), config);
    const Scenario b = generateScenario("repro", Seed(928371), config);
    CHECK_EQ(a.satellites().size(), b.satellites().size());
    for (std::size_t i = 0; i < a.satellites().size(); ++i) {
        CHECK_EQ(a.satellites()[i].id().value(), b.satellites()[i].id().value());
        CHECK_NEAR(a.satellites()[i].orbit().altitudeKm(), b.satellites()[i].orbit().altitudeKm(), 1e-12);
        CHECK_NEAR(a.satellites()[i].orbit().inclinationRad(), b.satellites()[i].orbit().inclinationRad(),
                  1e-12);
        CHECK_NEAR(a.satellites()[i].orbit().raanRad(), b.satellites()[i].orbit().raanRad(), 1e-12);
        CHECK_NEAR(a.satellites()[i].orbit().phaseAtEpochRad(), b.satellites()[i].orbit().phaseAtEpochRad(),
                  1e-12);
    }
    for (std::size_t i = 0; i < a.vehicles().size(); ++i) {
        CHECK_NEAR(a.vehicles()[i].deltaVBudgetKms(), b.vehicles()[i].deltaVBudgetKms(), 1e-12);
    }
}

ORCA_TEST(ScenarioGenerator, DifferentSeedsProduceDifferentScenarios) {
    const ScenarioGeneratorConfig config = scenario_family::normal(25);
    const Scenario a = generateScenario("a", Seed(1), config);
    const Scenario b = generateScenario("b", Seed(2), config);
    bool anyDifferent = false;
    for (std::size_t i = 0; i < a.satellites().size(); ++i) {
        if (std::fabs(a.satellites()[i].orbit().altitudeKm() - b.satellites()[i].orbit().altitudeKm()) >
            1e-9) {
            anyDifferent = true;
        }
    }
    CHECK(anyDifferent);
}

ORCA_TEST(ScenarioGenerator, GeneratedScenarioPassesValidation) {
    for (int n : {25, 50, 100}) {
        const ScenarioGeneratorConfig config = scenario_family::normal(n);
        const Scenario s = generateScenario("valid", Seed(42), config);
        validateGeneratedScenario(s, config);  // must not throw
    }
}

ORCA_TEST(ScenarioGenerator, RejectsAnInvalidConfigBeforeConstructingAnyEntity) {
    ScenarioGeneratorConfig bad;
    bad.satelliteCount = 0;
    CHECK_THROWS(generateScenario("bad", Seed(1), bad));

    ScenarioGeneratorConfig badBand;
    badBand.leoAltitudeMinKm = 2000.0;
    badBand.leoAltitudeMaxKm = 500.0;  // inverted
    CHECK_THROWS(generateScenario("bad-band", Seed(1), badBand));
}

ORCA_TEST(ScenarioGenerator, AllEightFamiliesGenerateValidScenariosAt25) {
    const std::vector<ScenarioGeneratorConfig> families = {
        scenario_family::sparse(25),         scenario_family::normal(25),
        scenario_family::dense(25),          scenario_family::multiEmergency(25),
        scenario_family::fuelConstrained(25), scenario_family::highDebris(25),
        scenario_family::tradeoffRich(25),   scenario_family::stress(25),
    };
    for (const auto& config : families) {
        const Scenario s = generateScenario("family", Seed(7), config);
        validateGeneratedScenario(s, config);
        CHECK_EQ(s.satellites().size(), std::size_t{25});
    }
}

ORCA_TEST(ScenarioGenerator, SupportsAllRequiredScales) {
    for (int n : {25, 50, 100, 250, 500}) {
        const ScenarioGeneratorConfig config = scenario_family::normal(n);
        const Scenario s = generateScenario("scale", Seed(11), config);
        CHECK_EQ(s.satellites().size(), static_cast<std::size_t>(n));
        validateGeneratedScenario(s, config);
    }
}

ORCA_TEST(ScenarioGenerator, DenseFamilyProducesTightlyClusteredAltitudes) {
    const ScenarioGeneratorConfig config = scenario_family::dense(50);
    const Scenario s = generateScenario("dense", Seed(3), config);
    for (const Satellite& sat : s.satellites()) {
        CHECK(sat.orbit().altitudeKm() >= 500.0 && sat.orbit().altitudeKm() <= 700.0);
    }
}

ORCA_TEST(ScenarioGenerator, FuelConstrainedFamilyProducesLowDeltaVBudgets) {
    const ScenarioGeneratorConfig config = scenario_family::fuelConstrained(25);
    const Scenario s = generateScenario("fuel", Seed(3), config);
    for (const RescueVehicle& v : s.vehicles()) {
        CHECK(v.deltaVBudgetKms() <= 2.0);
    }
}
