#pragma once

/// @file DemoScenario.hpp
/// The canonical ORCA demo scenario — Phase 8's end-to-end scripted narrative (satellite
/// failure -> global replan dispatch -> debris reroute -> rescue complete), extracted here
/// so Phase 9's `--serve` CLI and `tests/simulation/end_to_end_demo_test.cpp` share exactly
/// one definition rather than two copies that could silently drift apart. Every number in
/// this scenario was hand-verified by compiling and running it during Phase 8
/// (`SIMULATION_ENGINE.md` §16); nothing here is new to Phase 9.

#include "../domain/Scenario.hpp"
#include "EmergencyGenerator.hpp"
#include "EventQueue.hpp"

namespace orca {

/// V1 and E1 (SAT-101) share the same altitude but different phase -- a permanent
/// same-period lock, so no direct route exists and the search must route through an
/// intermediate. Two intermediates (MID-A, MID-B) at different altitudes each offer a
/// working path, so a debris hazard threatening MID-A's approach leaves MID-B as a real,
/// verified alternative.
[[nodiscard]] Scenario buildDemoScenario();

/// The scripted failure/debris timeline this scenario is built to exercise:
/// t=5 SAT-101 propulsion failure, t=25 debris detected near MID-A. Returned separately
/// from the scenario (rather than embedded in `Scenario`, which has no `scheduledFailures`
/// field -- `DATA_MODEL.md` §8) so a caller decides how to introduce them: directly via
/// `scheduleScriptedFailures`/`scheduleEvent`, as this file's own tests and the Phase 9 CLI
/// both do.
[[nodiscard]] std::vector<ScriptedFailure> demoScenarioScriptedFailures();

/// The debris alert event for this scenario's single `DebrisObject` (id 1), at t=25.
[[nodiscard]] Event demoScenarioDebrisAlertEvent();

}  // namespace orca
