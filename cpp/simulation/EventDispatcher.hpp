#pragma once

/// @file EventDispatcher.hpp
/// Turns one fired `Event` into world-state changes and, where warranted, a call into
/// Phase 7's replanner — Phase 8 brief §35-38, §42-43. Kept as free functions operating on
/// `SimulationEngine&` (`SimulationEngine.hpp`'s own file comment explains why: this stays
/// a separate, independently-sized file rather than a member function, brief §72).
///
/// ## The one rule this file exists to enforce
///
/// No function here calls `findParetoRoutes()`, `solveSquareAssignment()`, or any other
/// Phase 4-6 algorithm directly, and none re-derives transfer physics. Every decision
/// routes through `DynamicReplanner::evaluate()` / `evaluatePreemption()` or
/// `evaluateGlobalReplan()` (Phase 7); every physical fact comes from `MissionExecutor`
/// or a direct, single `TransferModel::evaluate()` call for the debris-proximity check
/// alone (which is geometry, not planning).

#include "../simulation/Event.hpp"
#include "SimulationEngine.hpp"

namespace orca {

/// The single entry point `SimulationEngine::tick()` calls for each due event. Dispatches
/// on `event.kind`; unhandled kinds are a no-op (not an error -- an event kind reserved
/// for future use, e.g. a manually-injected `VehicleStateChange` a test wants logged but
/// not acted on, should not crash a run).
void dispatchEvent(SimulationEngine& engine, const Event& event);

/// Runs one global assignment over every pending emergency and available vehicle. Called by
/// the tick loop once per `SimulationEngineConfig::dispatchCycleSec` when cycling is enabled.
void dispatchPendingEmergencies(SimulationEngine& engine);

}  // namespace orca
