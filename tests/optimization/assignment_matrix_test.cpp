/// Tests for `buildAssignmentMatrix()` / `AssignmentMatrix::classify()`
/// (`AssignmentMatrix.hpp`) — the `(N+M) x (N+M)` block-padding scheme, verified directly
/// against hand-built `AssignmentCostEntry` lists (no Pareto search involved; that
/// integration is covered separately in `tests/integration/`).

#include "../../cpp/optimization/AssignmentMatrix.hpp"
#include "../TestFramework.hpp"

using namespace orca;

namespace {
// Mirrors AssignmentMatrix.cpp's private kUnassignedEmergencyBasePenalty/unassignedPenalty
// (its file comment documents the base as 10.0 and the multiplier as `(1.0 + priority)`,
// increasing with priority so it is more costly to abandon a higher-priority emergency) --
// not exposed via the header, so this is deliberately re-derived from the documented
// formula rather than imported, matching the same against-the-doc-comment discipline other
// numeric-constant tests in this codebase use.
constexpr double kExpectedBasePenalty = 10.0;
double expectedUnassignedPenalty(double priority) { return kExpectedBasePenalty * (1.0 + priority); }

std::vector<AssignmentCostEntry> twoByTwoEntries(double c00, bool f00, double c01, bool f01,
                                                 double c10, bool f10, double c11, bool f11,
                                                 double priorityE1, double priorityE2) {
    return {
        AssignmentCostEntry{.vehicle = VehicleId(1), .emergency = EmergencyId(101),
                           .feasible = f00, .cost = c00, .priority = priorityE1},
        AssignmentCostEntry{.vehicle = VehicleId(1), .emergency = EmergencyId(102),
                           .feasible = f01, .cost = c01, .priority = priorityE2},
        AssignmentCostEntry{.vehicle = VehicleId(2), .emergency = EmergencyId(101),
                           .feasible = f10, .cost = c10, .priority = priorityE1},
        AssignmentCostEntry{.vehicle = VehicleId(2), .emergency = EmergencyId(102),
                           .feasible = f11, .cost = c11, .priority = priorityE2},
    };
}
}  // namespace

ORCA_TEST(AssignmentMatrix, DimensionIsSumNotMax) {
    const auto entries = twoByTwoEntries(1.0, true, 2.0, true, 3.0, true, 4.0, true, 0.5, 0.5);
    const std::vector<VehicleId> vehicles{VehicleId(1), VehicleId(2)};
    const std::vector<EmergencyId> emergencies{EmergencyId(101), EmergencyId(102)};
    const AssignmentMatrix m = buildAssignmentMatrix(entries, vehicles, emergencies);
    CHECK_EQ(m.dimension, std::size_t{4});
    CHECK_EQ(m.costMatrix.size(), std::size_t{4});
    for (const auto& row : m.costMatrix) CHECK_EQ(row.size(), std::size_t{4});
}

ORCA_TEST(AssignmentMatrix, ClassifyIdentifiesAllFourBlocks) {
    const auto entries = twoByTwoEntries(1.0, true, 2.0, true, 3.0, true, 4.0, true, 0.5, 0.5);
    const std::vector<VehicleId> vehicles{VehicleId(1), VehicleId(2)};
    const std::vector<EmergencyId> emergencies{EmergencyId(101), EmergencyId(102)};
    const AssignmentMatrix m = buildAssignmentMatrix(entries, vehicles, emergencies);
    CHECK(m.classify(0, 0) == AssignmentCellKind::RealPair);
    CHECK(m.classify(1, 1) == AssignmentCellKind::RealPair);
    CHECK(m.classify(0, 2) == AssignmentCellKind::VehicleIdle);
    CHECK(m.classify(1, 3) == AssignmentCellKind::VehicleIdle);
    CHECK(m.classify(2, 0) == AssignmentCellKind::EmergencyUnassigned);
    CHECK(m.classify(3, 1) == AssignmentCellKind::EmergencyUnassigned);
    CHECK(m.classify(2, 2) == AssignmentCellKind::Unused);
    CHECK(m.classify(3, 3) == AssignmentCellKind::Unused);
}

ORCA_TEST(AssignmentMatrix, RealBlockCarriesEntryCostsAndFeasibilityExactly) {
    const auto entries =
        twoByTwoEntries(1.5, true, 2.5, true, 3.5, false, 4.5, true, 0.5, 0.5);
    const std::vector<VehicleId> vehicles{VehicleId(1), VehicleId(2)};
    const std::vector<EmergencyId> emergencies{EmergencyId(101), EmergencyId(102)};
    const AssignmentMatrix m = buildAssignmentMatrix(entries, vehicles, emergencies);
    CHECK_NEAR(m.costMatrix[0][0], 1.5, 1e-12);
    CHECK_NEAR(m.costMatrix[0][1], 2.5, 1e-12);
    CHECK_NEAR(m.costMatrix[1][0], 3.5, 1e-12);
    CHECK_NEAR(m.costMatrix[1][1], 4.5, 1e-12);
    CHECK(m.feasibleReal[0][0]);
    CHECK(m.feasibleReal[0][1]);
    CHECK(!m.feasibleReal[1][0]);
    CHECK(m.feasibleReal[1][1]);
}

ORCA_TEST(AssignmentMatrix, VehicleIdleBlockIsUniformlyZero) {
    const auto entries = twoByTwoEntries(1.0, true, 2.0, true, 3.0, true, 4.0, true, 0.5, 0.5);
    const std::vector<VehicleId> vehicles{VehicleId(1), VehicleId(2)};
    const std::vector<EmergencyId> emergencies{EmergencyId(101), EmergencyId(102)};
    const AssignmentMatrix m = buildAssignmentMatrix(entries, vehicles, emergencies);
    for (std::size_t row = 0; row < 2; ++row) {
        for (std::size_t col = 2; col < 4; ++col) {
            CHECK_NEAR(m.costMatrix[row][col], 0.0, 1e-12);
        }
    }
}

ORCA_TEST(AssignmentMatrix, DummyRowPenaltyMatchesDocumentedFormulaAndIsIdenticalAcrossRows) {
    const auto entries =
        twoByTwoEntries(1.0, true, 2.0, true, 3.0, true, 4.0, true, 0.8, 0.1);
    const std::vector<VehicleId> vehicles{VehicleId(1), VehicleId(2)};
    const std::vector<EmergencyId> emergencies{EmergencyId(101), EmergencyId(102)};
    const AssignmentMatrix m = buildAssignmentMatrix(entries, vehicles, emergencies);
    const double expectedE1 = expectedUnassignedPenalty(0.8);
    const double expectedE2 = expectedUnassignedPenalty(0.1);
    // Both dummy rows (2 and 3) must carry the identical per-column penalty.
    CHECK_NEAR(m.costMatrix[2][0], expectedE1, 1e-9);
    CHECK_NEAR(m.costMatrix[3][0], expectedE1, 1e-9);
    CHECK_NEAR(m.costMatrix[2][1], expectedE2, 1e-9);
    CHECK_NEAR(m.costMatrix[3][1], expectedE2, 1e-9);
    // Higher priority -> HIGHER penalty for leaving that emergency unassigned (E1's
    // priority 0.8 > E2's 0.1) -- abandoning the more urgent/severe emergency must cost
    // more, not less.
    CHECK(expectedE1 > expectedE2);
}

ORCA_TEST(AssignmentMatrix, UnusedCornerBlockIsFree) {
    const auto entries = twoByTwoEntries(1.0, true, 2.0, true, 3.0, true, 4.0, true, 0.5, 0.5);
    const std::vector<VehicleId> vehicles{VehicleId(1), VehicleId(2)};
    const std::vector<EmergencyId> emergencies{EmergencyId(101), EmergencyId(102)};
    const AssignmentMatrix m = buildAssignmentMatrix(entries, vehicles, emergencies);
    for (std::size_t row = 2; row < 4; ++row) {
        for (std::size_t col = 2; col < 4; ++col) {
            CHECK_NEAR(m.costMatrix[row][col], 0.0, 1e-12);
        }
    }
}

ORCA_TEST(AssignmentMatrix, InfeasiblePairUsesTheDocumentedSentinel) {
    const auto entries = twoByTwoEntries(kInfeasibleAssignmentCost, false, 2.0, true, 3.0,
                                         true, 4.0, true, 0.5, 0.5);
    const std::vector<VehicleId> vehicles{VehicleId(1), VehicleId(2)};
    const std::vector<EmergencyId> emergencies{EmergencyId(101), EmergencyId(102)};
    const AssignmentMatrix m = buildAssignmentMatrix(entries, vehicles, emergencies);
    CHECK_NEAR(m.costMatrix[0][0], kInfeasibleAssignmentCost, 1.0);
    CHECK(!m.feasibleReal[0][0]);
    // The sentinel dwarfs both escape hatches (0 for idle, ~10-20 for unassigned).
    CHECK(m.costMatrix[0][0] > expectedUnassignedPenalty(0.0) * 1000.0);
}

ORCA_TEST(AssignmentMatrix, RectangularMoreVehiclesThanEmergencies) {
    const std::vector<AssignmentCostEntry> entries{
        AssignmentCostEntry{.vehicle = VehicleId(1), .emergency = EmergencyId(101),
                           .feasible = true, .cost = 1.0, .priority = 0.5},
        AssignmentCostEntry{.vehicle = VehicleId(2), .emergency = EmergencyId(101),
                           .feasible = true, .cost = 2.0, .priority = 0.5},
        AssignmentCostEntry{.vehicle = VehicleId(3), .emergency = EmergencyId(101),
                           .feasible = true, .cost = 3.0, .priority = 0.5},
    };
    const std::vector<VehicleId> vehicles{VehicleId(1), VehicleId(2), VehicleId(3)};
    const std::vector<EmergencyId> emergencies{EmergencyId(101)};
    const AssignmentMatrix m = buildAssignmentMatrix(entries, vehicles, emergencies);
    CHECK_EQ(m.vehicleCount, std::size_t{3});
    CHECK_EQ(m.emergencyCount, std::size_t{1});
    CHECK_EQ(m.dimension, std::size_t{4});
    CHECK(m.classify(0, 0) == AssignmentCellKind::RealPair);
    CHECK(m.classify(1, 1) == AssignmentCellKind::VehicleIdle);
    CHECK(m.classify(3, 0) == AssignmentCellKind::EmergencyUnassigned);
}

ORCA_TEST(AssignmentMatrix, EqualCountsStillGetsFullPaddingNotZero) {
    // The specific correctness-motivated design decision (file comment): N==M must still
    // pad to 2N, not skip padding.
    const auto entries = twoByTwoEntries(1.0, true, 2.0, true, 3.0, true, 4.0, true, 0.5, 0.5);
    const std::vector<VehicleId> vehicles{VehicleId(1), VehicleId(2)};
    const std::vector<EmergencyId> emergencies{EmergencyId(101), EmergencyId(102)};
    const AssignmentMatrix m = buildAssignmentMatrix(entries, vehicles, emergencies);
    CHECK_EQ(m.dimension, std::size_t{4});  // not 2
}

ORCA_TEST(AssignmentMatrix, RejectsMismatchedEntryCount) {
    const auto entries = twoByTwoEntries(1.0, true, 2.0, true, 3.0, true, 4.0, true, 0.5, 0.5);
    std::vector<AssignmentCostEntry> tooFew(entries.begin(), entries.end() - 1);
    const std::vector<VehicleId> vehicles{VehicleId(1), VehicleId(2)};
    const std::vector<EmergencyId> emergencies{EmergencyId(101), EmergencyId(102)};
    CHECK_THROWS(buildAssignmentMatrix(tooFew, vehicles, emergencies));
}

ORCA_TEST(AssignmentMatrix, RejectsEmptyVehicleOrEmergencyList) {
    const std::vector<AssignmentCostEntry> entries;
    const std::vector<VehicleId> noVehicles;
    const std::vector<EmergencyId> emergencies{EmergencyId(101)};
    CHECK_THROWS(buildAssignmentMatrix(entries, noVehicles, emergencies));
}

ORCA_TEST(AssignmentMatrix, RejectsVehicleOrEmergencyOrderMismatchedWithEntries) {
    // `entries` is vehicles-major order for (VehicleId(1), VehicleId(2)) x
    // (EmergencyId(101), EmergencyId(102)) -- passing the vehicle list in the OPPOSITE
    // order must be rejected, not silently mislabel every real assignment (found during
    // Phase 6's adversarial self-review: nothing else in the type system catches this).
    const auto entries = twoByTwoEntries(1.0, true, 2.0, true, 3.0, true, 4.0, true, 0.5, 0.5);
    const std::vector<VehicleId> reorderedVehicles{VehicleId(2), VehicleId(1)};
    const std::vector<EmergencyId> emergencies{EmergencyId(101), EmergencyId(102)};
    CHECK_THROWS(buildAssignmentMatrix(entries, reorderedVehicles, emergencies));

    const std::vector<VehicleId> vehicles{VehicleId(1), VehicleId(2)};
    const std::vector<EmergencyId> reorderedEmergencies{EmergencyId(102), EmergencyId(101)};
    CHECK_THROWS(buildAssignmentMatrix(entries, vehicles, reorderedEmergencies));
}
