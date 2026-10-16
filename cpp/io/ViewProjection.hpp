#pragma once

/// @file ViewProjection.hpp
/// Builds `SimulationView.hpp`'s external types from a `SimulationEngine`'s actual internal
/// state — Phase 9 brief §7-9. This is the one place internal representation (`unordered_map`
/// storage, `Id<Tag>` wrappers, domain objects) is read; everything downstream of this file
/// only ever sees the flattened view types.

#include "../simulation/SimulationEngine.hpp"
#include "SimulationView.hpp"

namespace orca::io {

/// Projects `engine`'s complete current state into one `SimulationSnapshot`.
[[nodiscard]] SimulationSnapshot buildSnapshot(const SimulationEngine& engine);

}  // namespace orca::io
