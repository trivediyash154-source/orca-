/// `EmergencyGenerator` tests — Phase 8 brief §23's scripted and seeded modes, §55's
/// replay/reproducibility requirement.

#include <algorithm>

#include "../../cpp/domain/Scenario.hpp"
#include "../../cpp/simulation/EmergencyGenerator.hpp"
#include "../../cpp/simulation/SimulationState.hpp"
#include "../TestFramework.hpp"

using namespace orca;

namespace {
Scenario buildScenario(int satelliteCount) {
    std::vector<Satellite> sats;
    for (int i = 0; i < satelliteCount; ++i) {
        const Orbit o = Orbit::fromAltitude(500.0 + static_cast<double>(i) * 10.0,
                                            degToRad(5.0), 0.0, static_cast<double>(i) * 0.1);
        sats.emplace_back(SatelliteId(static_cast<std::uint32_t>(i + 1)),
                         "SAT-" + std::to_string(i + 1), o);
    }
    std::vector<RescueVehicle> vehicles;
    vehicles.emplace_back(VehicleId(1), "RV-01", Orbit::fromAltitude(500.0, 0.0, 0.0, 0.0), 5.0);
    SimulationConfig config;
    return Scenario("gen-test", Seed(42), config, sats, vehicles, {});
}
}  // namespace

ORCA_TEST(EmergencyGenerator, ScriptedFailuresScheduleExactlyAsGiven) {
    EventQueue queue;
    const std::vector<ScriptedFailure> failures{
        {.atSec = 30.0,
         .satellite = SatelliteId(1),
         .kind = EmergencyKind::PropulsionFailure,
         .severity = Severity::Critical,
         .survivalWindowSec = 1.0e6},
        {.atSec = 90.0,
         .satellite = SatelliteId(2),
         .kind = EmergencyKind::CommsLoss,
         .severity = Severity::Moderate,
         .survivalWindowSec = 2.0e6},
    };
    scheduleScriptedFailures(queue, failures);
    CHECK_EQ(queue.size(), std::size_t{2});

    const Event first = queue.pop();
    CHECK_NEAR(first.timestampSec, 30.0, 1e-9);
    CHECK(first.satellite == SatelliteId(1));
    CHECK(first.emergencyKind == EmergencyKind::PropulsionFailure);
    CHECK(first.severity == Severity::Critical);
    CHECK_NEAR(*first.survivalWindowSec, 1.0e6, 1e-6);

    const Event second = queue.pop();
    CHECK_NEAR(second.timestampSec, 90.0, 1e-9);
    CHECK(second.satellite == SatelliteId(2));
}

ORCA_TEST(EmergencyGenerator, RandomFailuresDrawDistinctSatellites) {
    SimulationState state = makeInitialState(buildScenario(10));
    EventQueue queue;
    RandomGenerator rng(Seed(123));
    RandomFailureConfig config{.failureCount = 5, .durationSec = 3600.0, .survivalWindowSec = 1.0e6};
    scheduleRandomFailures(queue, state, config, rng);
    CHECK_EQ(queue.size(), std::size_t{5});

    std::vector<std::uint32_t> chosen;
    while (!queue.empty()) {
        const Event e = queue.pop();
        CHECK(e.kind == EventKind::SatelliteFailure);
        CHECK(e.satellite.has_value());
        CHECK(e.timestampSec >= 0.0 && e.timestampSec <= 3600.0);
        chosen.push_back(e.satellite->value());
    }
    std::sort(chosen.begin(), chosen.end());
    CHECK(std::adjacent_find(chosen.begin(), chosen.end()) == chosen.end());  // all distinct
}

ORCA_TEST(EmergencyGenerator, RandomFailuresAreReproducibleFromTheSameSeed) {
    SimulationState stateA = makeInitialState(buildScenario(10));
    SimulationState stateB = makeInitialState(buildScenario(10));
    EventQueue queueA, queueB;
    RandomGenerator rngA(Seed(456));
    RandomGenerator rngB(Seed(456));
    RandomFailureConfig config{.failureCount = 4, .durationSec = 1000.0, .survivalWindowSec = 5.0e5};
    scheduleRandomFailures(queueA, stateA, config, rngA);
    scheduleRandomFailures(queueB, stateB, config, rngB);

    CHECK_EQ(queueA.size(), queueB.size());
    while (!queueA.empty()) {
        const Event a = queueA.pop();
        const Event b = queueB.pop();
        CHECK_NEAR(a.timestampSec, b.timestampSec, 1e-12);
        CHECK(a.satellite == b.satellite);
        CHECK(a.emergencyKind == b.emergencyKind);
        CHECK(a.severity == b.severity);
    }
}

ORCA_TEST(EmergencyGenerator, RandomFailuresExcludeAlreadyFailedSatellites) {
    SimulationState state = makeInitialState(buildScenario(3));
    state.findSatellite(SatelliteId(1))->setStatus(SatelliteStatus::Warning);
    state.findSatellite(SatelliteId(1))->setStatus(SatelliteStatus::Critical);
    state.findSatellite(SatelliteId(1))->setStatus(SatelliteStatus::Failed);

    EventQueue queue;
    RandomGenerator rng(Seed(1));
    RandomFailureConfig config{.failureCount = 2, .durationSec = 100.0, .survivalWindowSec = 1.0e5};
    scheduleRandomFailures(queue, state, config, rng);
    while (!queue.empty()) {
        CHECK(*queue.pop().satellite != SatelliteId(1));
    }
}

ORCA_TEST(EmergencyGenerator, RejectsMoreFailuresThanEligibleSatellites) {
    SimulationState state = makeInitialState(buildScenario(2));
    EventQueue queue;
    RandomGenerator rng(Seed(1));
    RandomFailureConfig config{.failureCount = 5, .durationSec = 100.0, .survivalWindowSec = 1.0e5};
    CHECK_THROWS(scheduleRandomFailures(queue, state, config, rng));
}

ORCA_TEST(EmergencyGenerator, ZeroFailureCountSchedulesNothing) {
    SimulationState state = makeInitialState(buildScenario(3));
    EventQueue queue;
    RandomGenerator rng(Seed(1));
    RandomFailureConfig config{.failureCount = 0, .durationSec = 100.0, .survivalWindowSec = 1.0e5};
    scheduleRandomFailures(queue, state, config, rng);
    CHECK(queue.empty());
}
