/// Direct tests of `solveGreedyRectangularAssignment()` (`GreedyAssignment.hpp`) — the
/// deliberately simple, deterministic baseline Hungarian is compared against
/// (Phase 6 brief §16, §18). The real-ORCA-data counterexample showing Hungarian strictly
/// beating this baseline lives in `tests/integration/assignment_scenarios_test.cpp`, since
/// it needs the full candidate-generation pipeline, not just this raw solver.

#include <vector>

#include "../../cpp/algorithms/GreedyAssignment.hpp"
#include "../TestFramework.hpp"

using namespace orca;

ORCA_TEST(GreedyAssignment, RejectsEmptyMatrix) {
    const std::vector<std::vector<double>> cost;
    const std::vector<std::vector<bool>> feasible;
    CHECK_THROWS(solveGreedyRectangularAssignment(cost, feasible));
}

ORCA_TEST(GreedyAssignment, RejectsShapeMismatchBetweenCostAndFeasible) {
    const std::vector<std::vector<double>> cost{{1.0, 2.0}, {3.0, 4.0}};
    const std::vector<std::vector<bool>> feasible{{true}};
    CHECK_THROWS(solveGreedyRectangularAssignment(cost, feasible));
}

ORCA_TEST(GreedyAssignment, SquareMatrixPicksCheapestFeasiblePerStep) {
    const std::vector<std::vector<double>> cost{{5.0, 1.0}, {2.0, 8.0}};
    const std::vector<std::vector<bool>> feasible{{true, true}, {true, true}};
    const GreedyResult r = solveGreedyRectangularAssignment(cost, feasible);
    // Global cheapest cell is (0,1)=1.0 -- greedy takes it first, forcing row1 into its
    // only remaining column, (1,0)=2.0. Total 3.0 (this is NOT necessarily the jointly
    // optimal total -- that is Hungarian's job, not greedy's).
    CHECK_EQ(r.assignedCount, 2);
    CHECK(r.rowToColumn[0].has_value());
    CHECK_EQ(*r.rowToColumn[0], std::size_t{1});
    CHECK(r.rowToColumn[1].has_value());
    CHECK_EQ(*r.rowToColumn[1], std::size_t{0});
    CHECK_NEAR(r.totalCost, 3.0, 1e-9);
}

ORCA_TEST(GreedyAssignment, BriefsOwnCounterexampleMatrix) {
    // The brief's own [[1,2],[2,100]] example: greedy grabs (0,0)=1 first (globally
    // cheapest), forcing (1,1)=100, total 101 -- far worse than Hungarian's optimal 4
    // (verified in hungarian_test.cpp's BriefsOwnTwoByTwoExample). This is the
    // demonstration that greedy is a legitimately weaker baseline, not an artificially
    // crippled one -- it is simply myopic.
    const std::vector<std::vector<double>> cost{{1.0, 2.0}, {2.0, 100.0}};
    const std::vector<std::vector<bool>> feasible{{true, true}, {true, true}};
    const GreedyResult r = solveGreedyRectangularAssignment(cost, feasible);
    CHECK_NEAR(r.totalCost, 101.0, 1e-9);
    CHECK_EQ(*r.rowToColumn[0], std::size_t{0});
    CHECK_EQ(*r.rowToColumn[1], std::size_t{1});
}

ORCA_TEST(GreedyAssignment, InfeasibleCellsAreNeverChosenRegardlessOfCostValue) {
    // Cell (0,0) has the numerically smallest cost but is marked infeasible -- greedy must
    // skip it even though a naive "smallest value wins" scan would pick it.
    const std::vector<std::vector<double>> cost{{0.0, 5.0}, {3.0, 4.0}};
    const std::vector<std::vector<bool>> feasible{{false, true}, {true, true}};
    const GreedyResult r = solveGreedyRectangularAssignment(cost, feasible);
    CHECK_EQ(r.assignedCount, 2);
    CHECK_EQ(*r.rowToColumn[0], std::size_t{1});
    CHECK_EQ(*r.rowToColumn[1], std::size_t{0});
    CHECK_NEAR(r.totalCost, 8.0, 1e-9);
}

ORCA_TEST(GreedyAssignment, MoreRowsThanColumnsLeavesSomeRowsUnmatched) {
    const std::vector<std::vector<double>> cost{{1.0}, {2.0}, {3.0}};
    const std::vector<std::vector<bool>> feasible{{true}, {true}, {true}};
    const GreedyResult r = solveGreedyRectangularAssignment(cost, feasible);
    CHECK_EQ(r.assignedCount, 1);
    CHECK(r.rowToColumn[0].has_value());
    CHECK(!r.rowToColumn[1].has_value());
    CHECK(!r.rowToColumn[2].has_value());
    CHECK_NEAR(r.totalCost, 1.0, 1e-9);
}

ORCA_TEST(GreedyAssignment, MoreColumnsThanRowsLeavesSomeColumnsUnused) {
    const std::vector<std::vector<double>> cost{{3.0, 1.0, 2.0}};
    const std::vector<std::vector<bool>> feasible{{true, true, true}};
    const GreedyResult r = solveGreedyRectangularAssignment(cost, feasible);
    CHECK_EQ(r.assignedCount, 1);
    CHECK_EQ(*r.rowToColumn[0], std::size_t{1});
    CHECK_NEAR(r.totalCost, 1.0, 1e-9);
}

ORCA_TEST(GreedyAssignment, EntireRowInfeasibleLeavesThatRowUnmatched) {
    const std::vector<std::vector<double>> cost{{1.0, 2.0}, {3.0, 4.0}};
    const std::vector<std::vector<bool>> feasible{{false, false}, {true, true}};
    const GreedyResult r = solveGreedyRectangularAssignment(cost, feasible);
    CHECK_EQ(r.assignedCount, 1);
    CHECK(!r.rowToColumn[0].has_value());
    CHECK(r.rowToColumn[1].has_value());
    CHECK_EQ(*r.rowToColumn[1], std::size_t{0});
}

ORCA_TEST(GreedyAssignment, DeterministicTieBreakSmallestRowThenSmallestColumn) {
    // Every cell tied at the same cost -- the deterministic tie-break (smallest row, then
    // smallest column) must produce the same result every run, not an arbitrary one.
    const std::vector<std::vector<double>> cost{{5.0, 5.0}, {5.0, 5.0}};
    const std::vector<std::vector<bool>> feasible{{true, true}, {true, true}};
    const GreedyResult a = solveGreedyRectangularAssignment(cost, feasible);
    const GreedyResult b = solveGreedyRectangularAssignment(cost, feasible);
    CHECK(a.rowToColumn == b.rowToColumn);
    CHECK_EQ(*a.rowToColumn[0], std::size_t{0});
    CHECK_EQ(*a.rowToColumn[1], std::size_t{1});
}

ORCA_TEST(GreedyAssignment, AllCellsInfeasibleProducesNoAssignments) {
    const std::vector<std::vector<double>> cost{{1.0, 2.0}, {3.0, 4.0}};
    const std::vector<std::vector<bool>> feasible{{false, false}, {false, false}};
    const GreedyResult r = solveGreedyRectangularAssignment(cost, feasible);
    CHECK_EQ(r.assignedCount, 0);
    CHECK(!r.rowToColumn[0].has_value());
    CHECK(!r.rowToColumn[1].has_value());
    CHECK_NEAR(r.totalCost, 0.0, 1e-12);
}
