#include "AssignmentResult.hpp"

#include <chrono>

#include "../algorithms/GreedyAssignment.hpp"
#include "../algorithms/Hungarian.hpp"
#include "../core/Validation.hpp"

namespace orca {

namespace {

/// `fronts`/`scored` are indexed as `row*matrix.emergencyCount+col` throughout this file --
/// correct only if they are the exact `vehicles`-major-order pair `buildAssignmentCosts()`
/// (and, transitively, `buildAssignmentMatrix()`) was built from. Checked explicitly at
/// both public entry points below rather than trusted, the same discipline
/// `AssignmentMatrix.cpp`'s own entries/vehicles/emergencies order check uses -- a caller
/// passing a mismatched or differently-sized `fronts`/`scored` would otherwise either
/// silently attach the wrong `MissionPlan` to a real assignment or read out of bounds.
void requireFrontsMatchMatrixShape(const AssignmentMatrix& matrix,
                                   const std::vector<CandidateFront>& fronts,
                                   const std::vector<ScoredCandidate>& scored) {
    const std::size_t expected = matrix.vehicleCount * matrix.emergencyCount;
    requireThat(fronts.size() == expected && scored.size() == expected,
                "solveAssignment*: fronts/scored must have exactly vehicleCount * "
                "emergencyCount entries, matching the matrix they were built from");
}

/// Appends one real assignment (row < vehicleCount, col < emergencyCount, already
/// confirmed feasible by the caller) to `result`, attaching its actual `MissionPlan`.
void appendRealAssignment(AssignmentResult& result, const AssignmentMatrix& matrix,
                          const std::vector<CandidateFront>& fronts,
                          const std::vector<ScoredCandidate>& scored, std::size_t row,
                          std::size_t col, double cost) {
    const std::size_t candidateIndex = row * matrix.emergencyCount + col;
    const ScoredCandidate& candidate = scored[candidateIndex];
    const CandidateFront& front = fronts[candidateIndex];
    const MissionPlan& plan = front.front.solutions[*candidate.selectedPlanIndex];

    Assignment assignment{.vehicle = matrix.vehicleIds[row],
                          .emergency = matrix.emergencyIds[col],
                          .plan = plan.id(),
                          .cost = cost,
                          .missionPlan = plan};
    result.totalCost += assignment.cost;
    result.assignments.push_back(std::move(assignment));
}

/// Shared tail: fill in counts, per-candidate feasibility metrics, and status, once
/// `result.assignments` already holds every real assignment. Common to both solvers so the
/// status policy (`AssignmentResult.hpp`'s file comment: success means every emergency
/// served) cannot drift between them.
void finalizeMetricsAndStatus(AssignmentResult& result, const AssignmentMatrix& matrix,
                              std::size_t solverMatrixDimension, long solverOperationCount,
                              double executionTimeMs) {
    result.metrics.vehicleCount = static_cast<int>(matrix.vehicleCount);
    result.metrics.emergencyCount = static_cast<int>(matrix.emergencyCount);
    result.metrics.candidatePairsEvaluated =
        static_cast<int>(matrix.vehicleCount * matrix.emergencyCount);
    result.metrics.solverMatrixDimension = solverMatrixDimension;
    result.metrics.solverOperationCount = solverOperationCount;
    result.metrics.executionTimeMs = executionTimeMs;

    for (const std::vector<bool>& row : matrix.feasibleReal) {
        for (bool feasible : row) {
            if (feasible) {
                ++result.metrics.feasibleCandidatePairs;
            } else {
                ++result.metrics.infeasibleCandidatePairs;
            }
        }
    }

    result.assignedVehicleCount = static_cast<int>(result.assignments.size());
    result.assignedEmergencyCount = static_cast<int>(result.assignments.size());
    result.unassignedEmergencyCount =
        static_cast<int>(matrix.emergencyCount) - result.assignedEmergencyCount;
    result.unusedVehicleCount =
        static_cast<int>(matrix.vehicleCount) - result.assignedVehicleCount;

    result.metrics.assignedPairs = result.assignedVehicleCount;
    result.metrics.unassignedEmergencyCount = result.unassignedEmergencyCount;
    result.metrics.unusedVehicleCount = result.unusedVehicleCount;
    result.metrics.totalAssignmentCost = result.totalCost;

    if (result.assignedEmergencyCount == 0) {
        result.status = AssignmentStatus::Infeasible;
    } else if (result.unassignedEmergencyCount == 0) {
        result.status = AssignmentStatus::Success;
    } else {
        result.status = AssignmentStatus::PartiallyAssignable;
    }
}

}  // namespace

AssignmentResult solveAssignmentHungarian(const AssignmentMatrix& matrix,
                                          const std::vector<CandidateFront>& fronts,
                                          const std::vector<ScoredCandidate>& scored) {
    requireFrontsMatchMatrixShape(matrix, fronts, scored);
    const auto startedAt = std::chrono::steady_clock::now();
    const HungarianResult solved = solveSquareAssignment(matrix.costMatrix);
    const auto finishedAt = std::chrono::steady_clock::now();
    const double executionTimeMs =
        std::chrono::duration<double, std::milli>(finishedAt - startedAt).count();

    AssignmentResult result;
    for (std::size_t row = 0; row < matrix.vehicleCount; ++row) {
        const std::size_t col = solved.rowToColumn[row];
        if (matrix.classify(row, col) != AssignmentCellKind::RealPair) {
            continue;  // vehicle landed in its idle block -- not a real assignment
        }
        if (!matrix.feasibleReal[row][col]) {
            // Defensive: should be structurally unreachable given
            // kInfeasibleAssignmentCost's magnitude relative to every escape hatch
            // (AssignmentMatrix.hpp's file comment) -- verified, not just assumed, by
            // assignment_matrix_test.cpp. Never report a real assignment this codebase's
            // own feasibility check disagrees with.
            continue;
        }
        appendRealAssignment(result, matrix, fronts, scored, row, col,
                            matrix.costMatrix[row][col]);
    }

    finalizeMetricsAndStatus(result, matrix, matrix.dimension, solved.operationCount,
                            executionTimeMs);
    return result;
}

AssignmentResult solveAssignmentGreedy(const AssignmentMatrix& matrix,
                                       const std::vector<CandidateFront>& fronts,
                                       const std::vector<ScoredCandidate>& scored) {
    requireFrontsMatchMatrixShape(matrix, fronts, scored);
    // Greedy operates on the natural RECTANGULAR real matrix, not Hungarian's padded
    // square one -- GreedyAssignment.hpp's file comment explains why the padded version
    // is actively wrong for a cheapest-first strategy (it trivially prefers the padded
    // matrix's free "idle" cells over any real assignment).
    std::vector<std::vector<double>> realCosts(matrix.vehicleCount);
    for (std::size_t row = 0; row < matrix.vehicleCount; ++row) {
        realCosts[row].assign(matrix.costMatrix[row].begin(),
                             matrix.costMatrix[row].begin() +
                                 static_cast<std::ptrdiff_t>(matrix.emergencyCount));
    }

    const auto startedAt = std::chrono::steady_clock::now();
    const GreedyResult solved = solveGreedyRectangularAssignment(realCosts, matrix.feasibleReal);
    const auto finishedAt = std::chrono::steady_clock::now();
    const double executionTimeMs =
        std::chrono::duration<double, std::milli>(finishedAt - startedAt).count();

    AssignmentResult result;
    for (std::size_t row = 0; row < matrix.vehicleCount; ++row) {
        if (!solved.rowToColumn[row].has_value()) {
            continue;
        }
        const std::size_t col = *solved.rowToColumn[row];
        appendRealAssignment(result, matrix, fronts, scored, row, col,
                            matrix.costMatrix[row][col]);
    }

    // Greedy reports no comparable "operation count" (Hungarian's potential-update
    // comparisons have no greedy analogue) and no comparable "solver matrix dimension"
    // (it never pads) -- 0 and the rectangular vehicle count are recorded honestly rather
    // than borrowing Hungarian's padded dimension, which greedy never constructs.
    finalizeMetricsAndStatus(result, matrix, matrix.vehicleCount, 0, executionTimeMs);
    return result;
}

}  // namespace orca
