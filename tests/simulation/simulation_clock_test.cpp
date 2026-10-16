/// `SimulationClock` tests — Phase 8 brief §53's clock category: start/pause/resume/step/
/// reset, plus the fixed-timestep/whole-multiple invariants `SimulationClock.hpp` documents.

#include "../../cpp/simulation/SimulationClock.hpp"
#include "../TestFramework.hpp"

using namespace orca;

ORCA_TEST(SimulationClock, StartsAtZeroAndNotRunning) {
    SimulationClock clock(1.0);
    CHECK_NEAR(clock.currentTimeSec(), 0.0, 1e-12);
    CHECK(!clock.isRunning());
}

ORCA_TEST(SimulationClock, StepIsANoOpWhilePaused) {
    SimulationClock clock(1.0);
    clock.step();
    CHECK_NEAR(clock.currentTimeSec(), 0.0, 1e-12);
}

ORCA_TEST(SimulationClock, StartThenStepAdvancesByExactlyOneTimestep) {
    SimulationClock clock(2.5);
    clock.start();
    clock.step();
    CHECK_NEAR(clock.currentTimeSec(), 2.5, 1e-12);
    clock.step();
    CHECK_NEAR(clock.currentTimeSec(), 5.0, 1e-12);
}

ORCA_TEST(SimulationClock, PauseStopsAdvancingResumeContinues) {
    SimulationClock clock(1.0);
    clock.start();
    clock.step();
    clock.pause();
    clock.step();
    clock.step();
    CHECK_NEAR(clock.currentTimeSec(), 1.0, 1e-12);  // the two paused steps did nothing
    clock.resume();
    clock.step();
    CHECK_NEAR(clock.currentTimeSec(), 2.0, 1e-12);
}

ORCA_TEST(SimulationClock, ResetReturnsToZeroAndPaused) {
    SimulationClock clock(1.0);
    clock.start();
    clock.step();
    clock.step();
    clock.reset();
    CHECK_NEAR(clock.currentTimeSec(), 0.0, 1e-12);
    CHECK(!clock.isRunning());
}

ORCA_TEST(SimulationClock, ResetPreservesTimestepAndSpeedConfiguration) {
    SimulationClock clock(2.0);
    clock.setSpeed(4.0);
    clock.start();
    clock.step();
    clock.reset();
    CHECK_NEAR(clock.timeStepSec(), 2.0, 1e-12);
    CHECK_NEAR(clock.speedMultiplier(), 4.0, 1e-12);
}

ORCA_TEST(SimulationClock, AdvanceMatchesRepeatedStep) {
    SimulationClock a(1.0);
    SimulationClock b(1.0);
    a.start();
    b.start();
    for (int i = 0; i < 1000; ++i) a.step();
    b.advance(1000.0);
    CHECK_NEAR(a.currentTimeSec(), b.currentTimeSec(), 1e-9);
    CHECK_NEAR(b.currentTimeSec(), 1000.0, 1e-9);
}

ORCA_TEST(SimulationClock, AdvanceIsANoOpWhilePaused) {
    SimulationClock clock(1.0);
    clock.advance(50.0);
    CHECK_NEAR(clock.currentTimeSec(), 0.0, 1e-12);
}

ORCA_TEST(SimulationClock, AdvanceRejectsNonWholeMultipleOfTimestep) {
    SimulationClock clock(2.0);
    clock.start();
    CHECK_THROWS(clock.advance(3.0));
}

ORCA_TEST(SimulationClock, AdvanceRejectsNegative) {
    SimulationClock clock(1.0);
    clock.start();
    CHECK_THROWS(clock.advance(-5.0));
}

ORCA_TEST(SimulationClock, SetTimeOverridesDirectlyRegardlessOfRunState) {
    SimulationClock clock(1.0);
    clock.setTime(500.0);
    CHECK_NEAR(clock.currentTimeSec(), 500.0, 1e-12);
}

ORCA_TEST(SimulationClock, SetTimeRejectsNegative) {
    SimulationClock clock(1.0);
    CHECK_THROWS(clock.setTime(-1.0));
}

ORCA_TEST(SimulationClock, RejectsNonPositiveTimestep) {
    CHECK_THROWS(SimulationClock(0.0));
    CHECK_THROWS(SimulationClock(-1.0));
}

ORCA_TEST(SimulationClock, RejectsNonPositiveSpeed) {
    SimulationClock clock(1.0);
    CHECK_THROWS(clock.setSpeed(0.0));
    CHECK_THROWS(clock.setSpeed(-2.0));
}

ORCA_TEST(SimulationClock, SpeedDefaultsToOne) {
    SimulationClock clock(1.0);
    CHECK_NEAR(clock.speedMultiplier(), 1.0, 1e-12);
}

ORCA_TEST(SimulationClock, StartIsIdempotent) {
    SimulationClock clock(1.0);
    clock.start();
    clock.start();
    CHECK(clock.isRunning());
    clock.step();
    CHECK_NEAR(clock.currentTimeSec(), 1.0, 1e-12);
}
