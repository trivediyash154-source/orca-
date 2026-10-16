/// `serializeVisualConstellation()` tests — Phase 12 brief §5/§19: real generated-scenario
/// positions, serialized without ever constructing a `SimulationEngine`/`MissionGraph`.
/// Checked against the same "trivial orbit -> exact position" technique
/// `tests/io/view_projection_test.cpp` already established, and against
/// `ScenarioGenerator`'s own already-validated object counts.

#include "../../cpp/io/JsonParser.hpp"
#include "../../cpp/io/VisualConstellation.hpp"
#include "../../cpp/simulation/ScenarioGenerator.hpp"
#include "../TestFramework.hpp"

using namespace orca;
using namespace orca::io;

ORCA_TEST(VisualConstellation, ObjectCountsMatchTheRealGeneratedScenario) {
    const ScenarioGeneratorConfig config = scenario_family::dense(50);
    const Scenario scenario = generateScenario("test-dense-50", Seed{7}, config);

    const std::string json = serializeVisualConstellation(scenario, 0.0, 1.23);
    const JsonValue parsed = parseJson(json);

    CHECK(parsed.find("type") != nullptr);
    CHECK_EQ(parsed.find("type")->asString(), std::string("visualConstellation"));
    CHECK_EQ(static_cast<std::size_t>(parsed.find("satelliteCount")->asNumber()), scenario.satellites().size());
    CHECK_EQ(static_cast<std::size_t>(parsed.find("vehicleCount")->asNumber()), scenario.vehicles().size());
    CHECK_EQ(static_cast<std::size_t>(parsed.find("debrisCount")->asNumber()), scenario.debris().size());
    CHECK_EQ(parsed.find("satellites")->asArray().size(), scenario.satellites().size());
    CHECK_EQ(parsed.find("vehicles")->asArray().size(), scenario.vehicles().size());
    CHECK_EQ(parsed.find("debris")->asArray().size(), scenario.debris().size());
}

ORCA_TEST(VisualConstellation, PositionsMatchTheEntitysOwnPositionAtKm) {
    const ScenarioGeneratorConfig config = scenario_family::sparse(10);
    const Scenario scenario = generateScenario("test-sparse-10", Seed{3}, config);
    constexpr Seconds t = 4200.0;

    const std::string json = serializeVisualConstellation(scenario, t, 0.5);
    const JsonValue parsed = parseJson(json);

    const JsonArray& satellites = parsed.find("satellites")->asArray();
    CHECK(!satellites.empty());
    const Vector3 expected = scenario.satellites()[0].positionAtKm(t);
    const JsonValue& firstPos = *satellites[0].find("position");
    CHECK_NEAR(firstPos.find("x")->asNumber(), expected.x, 1e-9);
    CHECK_NEAR(firstPos.find("y")->asNumber(), expected.y, 1e-9);
    CHECK_NEAR(firstPos.find("z")->asNumber(), expected.z, 1e-9);
}

ORCA_TEST(VisualConstellation, ReportsTheRealTimeAndGenerationCostBackVerbatim) {
    const ScenarioGeneratorConfig config = scenario_family::sparse(5);
    const Scenario scenario = generateScenario("test-sparse-5", Seed{1}, config);

    const std::string json = serializeVisualConstellation(scenario, 999.5, 7.75);
    const JsonValue parsed = parseJson(json);

    CHECK_NEAR(parsed.find("timeSec")->asNumber(), 999.5, 1e-9);
    CHECK_NEAR(parsed.find("generationMs")->asNumber(), 7.75, 1e-9);
    CHECK_EQ(parsed.find("scenarioName")->asString(), scenario.name());
}

ORCA_TEST(VisualConstellation, ScalesToThousandsOfObjectsWithoutConstructingAGraph) {
    // The whole point of this file (`VisualConstellation.hpp`'s own reasoning): a
    // SimulationEngine/GraphBuilder at this size would be prohibitively expensive
    // (O(n^2) candidate edges). This must stay fast BECAUSE it never builds one.
    const ScenarioGeneratorConfig config = scenario_family::dense(2000);
    const Scenario scenario = generateScenario("test-dense-2000", Seed{1}, config);
    const std::string json = serializeVisualConstellation(scenario, 0.0, 0.0);
    const JsonValue parsed = parseJson(json);
    CHECK_EQ(parsed.find("satellites")->asArray().size(), std::size_t{2000});
}
