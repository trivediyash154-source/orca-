#include "AssignmentMatrix.hpp"

#include "../core/Validation.hpp"

namespace orca {

namespace {

/// Base cost of leaving one emergency completely unserved, before the priority-based
/// multiplier. Comfortably larger than any real feasible cost (bounded in `[0,2]` --
/// `AssignmentCost.hpp`'s file comment) so a solver always prefers a real, feasible
/// assignment when one exists, and comfortably smaller than `kInfeasibleAssignmentCost`
/// (`1e9`) so an infeasible real pair is never preferred over leaving the emergency
/// unassigned.
constexpr double kUnassignedEmergencyBasePenalty = 10.0;

/// `(1.0 + priority)` ranges over `[1,2]`, INCREASING with priority — deliberately the
/// mirror image of `AssignmentCost.hpp`'s real-pair multiplier `(2 - priority)`, which
/// DECREASES with priority (making a high-priority emergency cheaper, hence more
/// attractive, to actually serve). Abandoning a high-priority emergency must cost *more*,
/// not less: an earlier version of this function used `(2.0 - priority)` here too, which
/// made it strictly *cheaper* to leave a Critical-severity, near-deadline emergency
/// unserved than a Low-severity, distant one — the opposite of what "priority" is supposed
/// to mean. Caught by constructing a real scarcity scenario (one vehicle, two emergencies)
/// where the old formula actually chose to abandon the higher-priority emergency
/// (`assignment_scenarios_test.cpp`'s `ScenarioE_PriorityUnderScarcity`).
double unassignedPenalty(double priority) noexcept {
    return kUnassignedEmergencyBasePenalty * (1.0 + priority);
}

}  // namespace

AssignmentCellKind AssignmentMatrix::classify(std::size_t row, std::size_t col) const noexcept {
    const bool realRow = row < vehicleCount;
    const bool realCol = col < emergencyCount;
    if (realRow && realCol) {
        return AssignmentCellKind::RealPair;
    }
    if (realRow && !realCol) {
        return AssignmentCellKind::VehicleIdle;
    }
    if (!realRow && realCol) {
        return AssignmentCellKind::EmergencyUnassigned;
    }
    return AssignmentCellKind::Unused;
}

AssignmentMatrix buildAssignmentMatrix(const std::vector<AssignmentCostEntry>& entries,
                                       const std::vector<VehicleId>& vehicles,
                                       const std::vector<EmergencyId>& emergencies) {
    const std::size_t vehicleCount = vehicles.size();
    const std::size_t emergencyCount = emergencies.size();
    requireThat(vehicleCount > 0 && emergencyCount > 0,
                "buildAssignmentMatrix: vehicles and emergencies must both be non-empty");
    requireThat(entries.size() == vehicleCount * emergencyCount,
                "buildAssignmentMatrix: entries must be vehicles x emergencies, "
                "vehicles-major order");
    // `entries` and `vehicles`/`emergencies` must describe the SAME pairing order --
    // `entries[v*emergencyCount+e]` is trusted below (and by every downstream consumer
    // that indexes `fronts`/`scored` the same way, e.g. AssignmentResult.cpp) to be
    // exactly (vehicles[v], emergencies[e]). A caller passing `vehicles`/`emergencies` in a
    // different order than the one `generateAllCandidateFronts()` used to build `entries`
    // would otherwise corrupt every label in the matrix silently -- checked explicitly
    // rather than trusted, since nothing else in the type system enforces it.
    for (std::size_t v = 0; v < vehicleCount; ++v) {
        for (std::size_t e = 0; e < emergencyCount; ++e) {
            const AssignmentCostEntry& entry = entries[v * emergencyCount + e];
            requireThat(entry.vehicle == vehicles[v] && entry.emergency == emergencies[e],
                        "buildAssignmentMatrix: entries order does not match the given "
                        "vehicles/emergencies order");
        }
    }

    AssignmentMatrix matrix;
    matrix.vehicleCount = vehicleCount;
    matrix.emergencyCount = emergencyCount;
    matrix.dimension = vehicleCount + emergencyCount;
    matrix.vehicleIds = vehicles;
    matrix.emergencyIds = emergencies;
    matrix.costMatrix.assign(matrix.dimension,
                             std::vector<double>(matrix.dimension, 0.0));
    matrix.feasibleReal.assign(vehicleCount, std::vector<bool>(emergencyCount, false));

    // Per-emergency priority, read once from the entries (every vehicle's entry for a
    // given emergency carries the same priority value -- AssignmentCost.hpp computes it
    // once per emergency, not per pair).
    std::vector<double> priorityByEmergencyIndex(emergencyCount, 0.0);
    for (std::size_t e = 0; e < emergencyCount; ++e) {
        priorityByEmergencyIndex[e] = entries[e].priority;  // vehicle 0's row has every
                                                             // emergency's entry
    }

    // Top-left N x M block: real pairs.
    for (std::size_t v = 0; v < vehicleCount; ++v) {
        for (std::size_t e = 0; e < emergencyCount; ++e) {
            const AssignmentCostEntry& entry = entries[v * emergencyCount + e];
            matrix.costMatrix[v][e] = entry.cost;
            matrix.feasibleReal[v][e] = entry.feasible;
        }
    }

    // Top-right N x N block: vehicle-idle, uniformly 0 -- which specific idle column a
    // vehicle "uses" carries no meaning (AssignmentMatrix.hpp's file comment), only
    // whether it lands in this block at all.
    for (std::size_t v = 0; v < vehicleCount; ++v) {
        for (std::size_t idleCol = 0; idleCol < vehicleCount; ++idleCol) {
            matrix.costMatrix[v][emergencyCount + idleCol] = 0.0;
        }
    }

    // Bottom-left M x M block: dummy-vehicle rows, one per emergency column's unassigned
    // penalty, identical across all M dummy rows for a given column (which dummy row is
    // "used" carries no meaning either).
    for (std::size_t dummyRow = 0; dummyRow < emergencyCount; ++dummyRow) {
        for (std::size_t e = 0; e < emergencyCount; ++e) {
            matrix.costMatrix[vehicleCount + dummyRow][e] =
                unassignedPenalty(priorityByEmergencyIndex[e]);
        }
    }

    // Bottom-right M x N block: dummy-vehicle to dummy-idle-slot -- never a real outcome,
    // free, exists only to keep the matrix square.
    for (std::size_t dummyRow = 0; dummyRow < emergencyCount; ++dummyRow) {
        for (std::size_t idleCol = 0; idleCol < vehicleCount; ++idleCol) {
            matrix.costMatrix[vehicleCount + dummyRow][emergencyCount + idleCol] = 0.0;
        }
    }

    return matrix;
}

}  // namespace orca
