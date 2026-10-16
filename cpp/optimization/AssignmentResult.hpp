#pragma once

/// @file AssignmentResult.hpp
/// Runs Hungarian (or, for comparison, greedy) on an `AssignmentMatrix`, validates the
/// result against real feasibility (Phase 6 brief §13's explicit requirement — never trust
/// the padding design alone), and materialises the final, small set of real assignments
/// with their actual `MissionPlan`s.
///
/// ## Status is about emergencies served, not vehicles used
///
/// `Success` means every emergency received a real, feasible vehicle — the mission's
/// actual goal. A vehicle ending up idle (more vehicles than emergencies, or some vehicles
/// simply not needed) is an expected, unremarkable outcome, not a partial failure.
/// `PartiallyAssignable` means some but not all emergencies were served; `Infeasible`
/// means none were — distinguishing "nothing this cycle could do" from "did what it could,
/// some emergencies remain."

#include <vector>

#include "../core/Types.hpp"
#include "../domain/MissionPlan.hpp"
#include "AssignmentMatrix.hpp"
#include "MissionCandidate.hpp"

namespace orca {

enum class AssignmentStatus { Success, PartiallyAssignable, Infeasible };

[[nodiscard]] constexpr std::string_view toString(AssignmentStatus status) noexcept {
    switch (status) {
        case AssignmentStatus::Success: return "SUCCESS";
        case AssignmentStatus::PartiallyAssignable: return "PARTIALLY_ASSIGNABLE";
        case AssignmentStatus::Infeasible: return "INFEASIBLE";
    }
    return "UNKNOWN";
}

struct Assignment {
    VehicleId vehicle;
    EmergencyId emergency;
    PlanId plan;
    double cost = 0.0;
    MissionPlan missionPlan;  ///< the actual selected plan -- copied here deliberately:
                              ///< final assignments number at most min(vehicles,
                              ///< emergencies), far fewer than the full candidate matrix,
                              ///< so this copy is not the one Phase 5's "avoid copying
                              ///< MissionPlans unnecessarily" guidance was about.
};

struct AssignmentMetrics {
    int vehicleCount = 0;
    int emergencyCount = 0;
    int candidatePairsEvaluated = 0;
    int feasibleCandidatePairs = 0;
    int infeasibleCandidatePairs = 0;
    int assignedPairs = 0;
    int unassignedEmergencyCount = 0;
    int unusedVehicleCount = 0;
    double totalAssignmentCost = 0.0;
    std::size_t solverMatrixDimension = 0;
    long solverOperationCount = 0;  ///< 0 for greedy (it has no comparable notion; see
                                    ///< `GreedyResult`, which does not report one)
    double executionTimeMs = 0.0;
};

struct AssignmentResult {
    AssignmentStatus status = AssignmentStatus::Infeasible;
    std::vector<Assignment> assignments;
    double totalCost = 0.0;
    int assignedVehicleCount = 0;
    int assignedEmergencyCount = 0;
    int unassignedEmergencyCount = 0;
    int unusedVehicleCount = 0;
    AssignmentMetrics metrics;
};

/// Solves `matrix` with the exact Hungarian solver, validates every real assignment
/// against `matrix.feasibleReal` (an infeasible real pair is never trusted even if the
/// solver somehow returned one — Phase 6 brief §13), and materialises the result using
/// `fronts`/`scored` (same `vehicles`-major order as `matrix` was built from) to attach
/// each winning pair's actual `MissionPlan`.
[[nodiscard]] AssignmentResult solveAssignmentHungarian(
    const AssignmentMatrix& matrix, const std::vector<CandidateFront>& fronts,
    const std::vector<ScoredCandidate>& scored);

/// Same contract, using the greedy baseline instead — for direct comparison against
/// `solveAssignmentHungarian` on the identical padded matrix.
[[nodiscard]] AssignmentResult solveAssignmentGreedy(
    const AssignmentMatrix& matrix, const std::vector<CandidateFront>& fronts,
    const std::vector<ScoredCandidate>& scored);

}  // namespace orca
