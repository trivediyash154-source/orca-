#pragma once

/// @file GlobalReplanner.hpp
/// "Should the fleet's entire idle-vehicle-to-pending-emergency assignment be
/// reconsidered?" -- Phase 7 brief §22's GLOBAL question, distinct from
/// `DynamicReplanner.hpp`'s LOCAL one (is *this* mission's route still good).
///
/// ## This file adds no new optimisation -- it is a named seam onto Phase 6
///
/// `evaluateGlobalReplan()` calls `generateAllCandidateFronts()` ->
/// `scoreCandidates()` -> `buildAssignmentCosts()` -> `buildAssignmentMatrix()` ->
/// `solveAssignmentHungarian()`, in that order, exactly as `assignment_scenarios_test.cpp`
/// already does in Phase 6 -- unmodified. Phase 7 brief §3, §45: "Do NOT duplicate ...
/// Hungarian assignment ... Use the existing Phase 6 architecture." The only thing this
/// file contributes is the name and the scope decision below.
///
/// ## Scoped to IDLE vehicles and PENDING emergencies, deliberately
///
/// A vehicle already flying an active mission is not reconsidered by a global replan --
/// `Mission::emergency()` has no setter (`Mission.hpp`), so Phase 6's assignment machinery
/// has no way to "retarget" an in-flight mission even if it wanted to. Whether an active
/// mission's vehicle should be FREED for reassignment is `DynamicReplanner::
/// evaluatePreemption()`'s question, not this file's; a vehicle that call frees becomes
/// available and is picked up by a SUBSEQUENT call to `evaluateGlobalReplan()`, not the
/// same one. This keeps the two concerns (should THIS mission be interrupted; given
/// what's available, what is the best fleet-wide assignment) in the two places
/// Phase 7 brief §22-23 separately describes them.

#include <vector>

#include "../core/Types.hpp"
#include "../domain/Emergency.hpp"
#include "../domain/RescueVehicle.hpp"
#include "../graph/MissionGraph.hpp"
#include "../optimization/AssignmentResult.hpp"
#include "../optimization/EmergencyPriority.hpp"
#include "../optimization/OptimizationProfile.hpp"
#include "../transfer/TransferModel.hpp"

namespace orca {

/// Runs Phase 6's full candidate-generation -> assignment pipeline over
/// `availableVehicles` and `pendingEmergencies`, and returns the result exactly as Phase 6
/// produces it. `nextPlanId` advances by however many `MissionPlan`s were generated.
///
/// @throws std::invalid_argument if `availableVehicles` or `pendingEmergencies` is empty
///         (`buildAssignmentMatrix()`'s own precondition -- there is nothing to assign
///         with no vehicles or nothing to assign to with no emergencies; a caller should
///         check both are non-empty before calling this, the same way it would check
///         before calling Phase 6's pipeline directly).
[[nodiscard]] AssignmentResult evaluateGlobalReplan(
    const MissionGraph& graph, const TransferModel& model,
    const std::vector<RescueVehicle>& availableVehicles,
    const std::vector<Emergency>& pendingEmergencies, Seconds nowSec,
    const OptimizationProfile& profile, PlanId& nextPlanId,
    const PriorityWeights& priorityWeights = defaultPriorityWeights());

}  // namespace orca
