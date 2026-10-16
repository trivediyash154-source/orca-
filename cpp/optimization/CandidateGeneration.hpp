#pragma once

/// @file CandidateGeneration.hpp
/// Runs Phase 5's Pareto search once per (vehicle, emergency) pair, and separately scores
/// the results — the two stages `MissionCandidate.hpp`'s file comment explains are kept
/// structurally apart.
///
/// ## Vehicle -> NodeId, emergency -> NodeId
///
/// `MissionGraph` has no entity-to-`NodeId` lookup (`GraphBuilder.hpp`, `MissionGraph.hpp`
/// — checked directly, not assumed absent). Building one is genuinely this layer's
/// responsibility, not `graph/`'s: only the assignment layer needs "which node is vehicle
/// V" and "which node is emergency E's subject satellite," and adding it to `MissionGraph`
/// itself would grow that class for a need only Phase 6 has. `buildEntityNodeIndex()`
/// below is a small, one-time `O(V)` scan producing two hash maps, built once per planning
/// pass and reused across every candidate pair.
///
/// ## The deadline and delta-v budget, precisely
///
/// `SearchConstraints::latestArrivalSec` is set to `emergency.deadlineSec()` — reusing
/// Group 3's existing per-relaxation deadline check (`GraphBuilder.hpp`) rather than
/// building a second one. `SearchConstraints::vehicleDeltaVBudgetKms` is set to
/// `vehicle.deltaVRemainingKms()`, not `deltaVBudgetKms()` — the *currently affordable*
/// amount, matching `RescueVehicle::canAfford()`'s own semantics elsewhere in the
/// codebase, and correct even for a vehicle that has already spent some fuel (never
/// overestimates what it can still do). In every scenario this phase's own tests build,
/// vehicles start fresh, so `deltaVRemainingKms() == deltaVBudgetKms()` and this
/// distinction does not change any test's numbers -- documented here so it is a deliberate
/// choice, not an unexamined one, should a future phase feed in a partially-spent vehicle.
///
/// ## Infeasibility diagnosis -- honestly bounded, not exhaustive
///
/// An empty front does not say *why*. `ConstraintKind::NoAsset` is assigned before any
/// search even runs, from `RescueVehicle::isAvailable()`. Otherwise, up to two additional
/// searches are run *only on the infeasible path* (never the common case): relaxing the
/// deadline alone, then relaxing the delta-v budget alone. If exactly one relaxation makes
/// the pair feasible, that is reported as the binding constraint. If *neither* single
/// relaxation alone succeeds (both are simultaneously binding, or the pair is genuinely
/// unreachable regardless of resources), the reason is reported as
/// `ConstraintKind::Unreachable` — an honest simplification, not a claim that no single
/// resource would help if both were relaxed together, which is not checked.

#include <unordered_map>
#include <vector>

#include "../core/Types.hpp"
#include "../domain/Emergency.hpp"
#include "../domain/RescueVehicle.hpp"
#include "../graph/MissionGraph.hpp"
#include "../transfer/TransferModel.hpp"
#include "MissionCandidate.hpp"
#include "OptimizationProfile.hpp"

namespace orca {

struct EntityNodeIndex {
    std::unordered_map<std::uint32_t, NodeId> vehicleNodes;
    std::unordered_map<std::uint32_t, NodeId> satelliteNodes;
};

/// One-time scan of `graph.nodes()`, keyed by each ID type's raw `.value()` (a plain
/// `uint32_t` key sidesteps needing a custom hash-map specialisation here beyond the one
/// `Id<Tag>` already provides for other containers elsewhere in the codebase).
[[nodiscard]] EntityNodeIndex buildEntityNodeIndex(const MissionGraph& graph);

/// Runs Pareto search for one (vehicle, emergency) pair. `firstPlanId` seeds this
/// candidate's `MissionPlan` IDs; the caller is responsible for advancing it between
/// calls so IDs stay globally unique across an entire planning pass (`ParetoFront.hpp`'s
/// "deterministic generation order" policy, extended across pairs).
///
/// @throws std::invalid_argument if `vehicle`'s or `emergency`'s node is not in `index`
///         (a caller error -- every vehicle/emergency passed to a planning pass must have
///         been part of the scenario `graph` was built from).
[[nodiscard]] CandidateFront generateCandidateFront(const MissionGraph& graph,
                                                    const TransferModel& transferModel,
                                                    const EntityNodeIndex& index,
                                                    const RescueVehicle& vehicle,
                                                    const Emergency& emergency,
                                                    Seconds nowSec, PlanId& nextPlanId);

/// Every (vehicle, emergency) pair, in `vehicles`-major order (vehicle 0 against every
/// emergency, then vehicle 1, ...) -- a fixed, deterministic order, not incidental.
[[nodiscard]] std::vector<CandidateFront> generateAllCandidateFronts(
    const MissionGraph& graph, const TransferModel& transferModel,
    const std::vector<RescueVehicle>& vehicles, const std::vector<Emergency>& emergencies,
    Seconds nowSec, PlanId firstPlanId);

/// Scores every candidate under `profile`. Calls `findParetoRoutes()` for none of them --
/// reads only `fronts`, already computed. This is the function a UI weight-slider calls
/// repeatedly; `generateAllCandidateFronts()` is called exactly once per planning pass.
[[nodiscard]] std::vector<ScoredCandidate> scoreCandidates(
    const std::vector<CandidateFront>& fronts, const OptimizationProfile& profile);

}  // namespace orca
