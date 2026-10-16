#pragma once

/// @file VisualConstellation.hpp
/// Phase 12 brief §5/§19: a large-scale VISUAL constellation is a genuinely different layer
/// from the computational planning network `SimulationSnapshot`/`ViewProjection.hpp` serve --
/// serving thousands of objects through the normal pipeline would force
/// `GraphBuilder::build()` to construct an all-pairs candidate graph (`O(n^2)` edges;
/// `docs/FINAL_BENCHMARK_REPORT.md` already measured 315,282 edges at just 562 nodes), which
/// is real, useful work for a *planning* scenario but pure waste for a screen that only ever
/// draws satellite positions. This file is a deliberately separate, minimal, one-shot
/// projection: real `ScenarioGenerator` output (same generator P1 already validated), real
/// `Orbit::positionAtKm()` math, NO `SimulationEngine`, NO `MissionGraph`, NO algorithms --
/// so it stays cheap at sizes the planning pipeline was never meant to run at.

#include <string>

#include "../core/Units.hpp"
#include "../domain/Scenario.hpp"

namespace orca::io {

/// Serializes `scenario`'s satellites/vehicles/debris at `timeSec`, real positions via each
/// entity's own `positionAtKm()`, to a single compact JSON object (not NDJSON framing -- this
/// is a one-shot request/response, not a persistent protocol session). `generationMs` is
/// reported back verbatim (the caller already measured it) so the response is self-describing
/// about its own real cost, never a number the frontend would have to estimate.
[[nodiscard]] std::string serializeVisualConstellation(const Scenario& scenario, Seconds timeSec,
                                                        double generationMs);

}  // namespace orca::io
