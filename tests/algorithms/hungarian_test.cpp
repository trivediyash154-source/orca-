/// Direct tests of `solveSquareAssignment()` (`Hungarian.hpp`) — Phase 6 brief §18's
/// "basic", "rectangular" (via the same padded-square shape `AssignmentMatrix` produces),
/// "infeasibility" (via `kInfeasibleAssignmentCost`-scale sentinels), and "numerical"
/// groups. The independent brute-force oracle lives separately in
/// `hungarian_oracle_test.cpp` — kept apart so a single file's bug cannot validate itself.
///
/// Every expected total below was obtained by actually running `solveSquareAssignment()`
/// (or, for the textbook/brief cases, cross-checked by hand against the well-known
/// published answer) — none are guessed.

#include <cmath>
#include <limits>
#include <vector>

#include "../../cpp/algorithms/Hungarian.hpp"
#include "../TestFramework.hpp"

using namespace orca;

namespace {
bool isPermutation(const std::vector<std::size_t>& rowToColumn, std::size_t n) {
    std::vector<bool> seen(n, false);
    for (std::size_t col : rowToColumn) {
        if (col >= n || seen[col]) return false;
        seen[col] = true;
    }
    return rowToColumn.size() == n;
}
}  // namespace

// ---------------------------------------------------------------------------
// Basic (5)
// ---------------------------------------------------------------------------

ORCA_TEST(Hungarian, TrivialOneByOne) {
    const std::vector<std::vector<double>> cost{{42.0}};
    const HungarianResult r = solveSquareAssignment(cost);
    CHECK_EQ(r.rowToColumn.size(), std::size_t{1});
    CHECK_EQ(r.rowToColumn[0], std::size_t{0});
    CHECK_NEAR(r.totalCost, 42.0, 1e-9);
}

ORCA_TEST(Hungarian, DiagonalIsAlreadyOptimal) {
    const std::vector<std::vector<double>> cost{
        {1.0, 9.0, 9.0}, {9.0, 1.0, 9.0}, {9.0, 9.0, 1.0}};
    const HungarianResult r = solveSquareAssignment(cost);
    CHECK(isPermutation(r.rowToColumn, 3));
    CHECK_NEAR(r.totalCost, 3.0, 1e-9);
    for (std::size_t i = 0; i < 3; ++i) CHECK_EQ(r.rowToColumn[i], i);
}

ORCA_TEST(Hungarian, TextbookThreeByThree) {
    // Verified by exhaustive enumeration of all 6 permutations (also cross-checked by
    // `HungarianOracle.AgreesOnHandPickedFourByFour`-style brute force in
    // hungarian_oracle_test.cpp): the minimum is 10 (row0->col1 + row1->col0 + row2->col2 =
    // 2+6+2), strictly better than the diagonal's 9+4+2 = 15.
    const std::vector<std::vector<double>> cost{
        {9.0, 2.0, 7.0}, {6.0, 4.0, 3.0}, {5.0, 8.0, 2.0}};
    const HungarianResult r = solveSquareAssignment(cost);
    CHECK(isPermutation(r.rowToColumn, 3));
    CHECK_NEAR(r.totalCost, 10.0, 1e-9);
}

ORCA_TEST(Hungarian, BriefsOwnTwoByTwoExample) {
    // Phase 6 brief §16's own conceptual counterexample: cheapest-first greedy grabs the
    // (0,0) cell (cost 1) and is then forced into (1,1) (cost 100), total 101. The true
    // optimum swaps to (0,1)+(1,0) = 2+2 = 4.
    const std::vector<std::vector<double>> cost{{1.0, 2.0}, {2.0, 100.0}};
    const HungarianResult r = solveSquareAssignment(cost);
    CHECK(isPermutation(r.rowToColumn, 2));
    CHECK_NEAR(r.totalCost, 4.0, 1e-9);
    CHECK_EQ(r.rowToColumn[0], std::size_t{1});
    CHECK_EQ(r.rowToColumn[1], std::size_t{0});
}

ORCA_TEST(Hungarian, DeterministicAcrossRepeatedRuns) {
    const std::vector<std::vector<double>> cost{
        {4.0, 1.0, 3.0, 8.0}, {2.0, 5.0, 9.0, 1.0}, {7.0, 3.0, 2.0, 6.0}, {5.0, 6.0, 4.0, 2.0}};
    const HungarianResult a = solveSquareAssignment(cost);
    const HungarianResult b = solveSquareAssignment(cost);
    CHECK_EQ(a.totalCost, b.totalCost);
    CHECK(a.rowToColumn == b.rowToColumn);
}

// ---------------------------------------------------------------------------
// Rectangular, via the same (N+M) x (N+M) padded-square shape AssignmentMatrix.hpp builds
// (Hungarian itself only accepts square input -- padding is the domain layer's job, but the
// solver must handle the shapes padding actually produces: 0-cost idle blocks, uniform
// per-column penalty blocks, and a free do-nothing corner). (4)
// ---------------------------------------------------------------------------

ORCA_TEST(Hungarian, RejectsNonSquareInput) {
    const std::vector<std::vector<double>> cost{{1.0, 2.0, 3.0}, {4.0, 5.0, 6.0}};
    CHECK_THROWS(solveSquareAssignment(cost));
}

ORCA_TEST(Hungarian, MorePotentialVehiclesThanEmergenciesPaddedShape) {
    // 3 vehicles, 1 emergency -> AssignmentMatrix's (N+M)x(N+M) = 4x4: 3x1 real block, a
    // 3x3 vehicle-idle block (all 0), a 1x1 dummy-row block, and a 1x3 free corner.
    const std::vector<std::vector<double>> cost{
        {5.0, 0.0, 0.0, 0.0},
        {2.0, 0.0, 0.0, 0.0},
        {8.0, 0.0, 0.0, 0.0},
        {20.0, 0.0, 0.0, 0.0}};
    const HungarianResult r = solveSquareAssignment(cost);
    CHECK(isPermutation(r.rowToColumn, 4));
    // Cheapest real vehicle (row 1, cost 2.0) should take the one real column; the other
    // two vehicles and the dummy row all land for free in the idle/corner blocks.
    CHECK_EQ(r.rowToColumn[1], std::size_t{0});
    CHECK_NEAR(r.totalCost, 2.0, 1e-9);
}

ORCA_TEST(Hungarian, MorePotentialEmergenciesThanVehiclesPaddedShape) {
    // 1 vehicle, 3 emergencies -> 4x4: 1x3 real block, 1x1 idle block, 3x3 dummy-penalty
    // block, 3x1 free corner. The vehicle should take its cheapest real column; the other
    // two emergencies fall to the penalty rows.
    const std::vector<std::vector<double>> cost{
        {9.0, 3.0, 6.0, 0.0},
        {10.0, 10.0, 10.0, 0.0},
        {10.0, 10.0, 10.0, 0.0},
        {10.0, 10.0, 10.0, 0.0}};
    const HungarianResult r = solveSquareAssignment(cost);
    CHECK(isPermutation(r.rowToColumn, 4));
    CHECK_EQ(r.rowToColumn[0], std::size_t{1});
    CHECK_NEAR(r.totalCost, 23.0, 1e-9);  // 3.0 (real) + 10.0 + 10.0 (two dummy rows)
}

ORCA_TEST(Hungarian, EqualCountsStillHasEscapeHatchViaPadding) {
    // Even with vehicleCount == emergencyCount, AssignmentMatrix pads to 2N x 2N so a
    // vehicle with no good options still has somewhere to go without being forced onto an
    // infeasible real cell. This reproduces that shape directly: 2 real, 2 idle, 2 dummy,
    // 2 corner rows/cols.
    const std::vector<std::vector<double>> cost{
        {1.0, 1000.0, 0.0, 0.0}, {1000.0, 1000.0, 0.0, 0.0}, {50.0, 50.0, 0.0, 0.0}, {50.0, 50.0, 0.0, 0.0}};
    const HungarianResult r = solveSquareAssignment(cost);
    CHECK(isPermutation(r.rowToColumn, 4));
    // Row 1 (vehicle 2) has no cheap real option (1000 either way) -- it should escape into
    // an idle column rather than the solver being forced to pick a real cell at all costs.
    CHECK(r.rowToColumn[1] == std::size_t{2} || r.rowToColumn[1] == std::size_t{3});
}

// ---------------------------------------------------------------------------
// Infeasibility, via a large finite sentinel (never actual infinity -- Hungarian.hpp's
// arithmetic must never see inf-inf). (4)
// ---------------------------------------------------------------------------

ORCA_TEST(Hungarian, LargeSentinelNeverSelectedWhenARealAlternativeExists) {
    constexpr double kSentinel = 1.0e9;
    const std::vector<std::vector<double>> cost{
        {kSentinel, 3.0}, {4.0, kSentinel}};
    const HungarianResult r = solveSquareAssignment(cost);
    CHECK_EQ(r.rowToColumn[0], std::size_t{1});
    CHECK_EQ(r.rowToColumn[1], std::size_t{0});
    CHECK_NEAR(r.totalCost, 7.0, 1e-6);
}

ORCA_TEST(Hungarian, BriefsThreeByThreeWithOneInfeasibleCell) {
    // A 3x3 matrix with one infeasible cell per row (priced at the sentinel). Of the two
    // permutations that avoid every sentinel cell (verified by exhaustive enumeration),
    // {row0->col0, row1->col2, row2->col1} = 10+11+12 = 33 beats
    // {row0->col1, row1->col0, row2->col2} = 19+10+8 = 37.
    constexpr double kSentinel = 1.0e9;
    const std::vector<std::vector<double>> cost{
        {10.0, 19.0, kSentinel}, {10.0, kSentinel, 11.0}, {kSentinel, 12.0, 8.0}};
    const HungarianResult r = solveSquareAssignment(cost);
    CHECK(isPermutation(r.rowToColumn, 3));
    for (std::size_t i = 0; i < 3; ++i) {
        CHECK(cost[i][r.rowToColumn[i]] < kSentinel);
    }
    CHECK_NEAR(r.totalCost, 33.0, 1e-6);
}

ORCA_TEST(Hungarian, NoArithmeticProducesNaNOrInfEvenWithMultipleSentinels) {
    constexpr double kSentinel = 1.0e9;
    const std::vector<std::vector<double>> cost{
        {kSentinel, kSentinel, 1.0}, {kSentinel, 2.0, kSentinel}, {3.0, kSentinel, kSentinel}};
    const HungarianResult r = solveSquareAssignment(cost);
    CHECK(std::isfinite(r.totalCost));
    for (std::size_t i = 0; i < 3; ++i) {
        CHECK(std::isfinite(cost[i][r.rowToColumn[i]]));
    }
}

ORCA_TEST(Hungarian, AllCellsInARowSentinelStillProducesAValidPermutation) {
    // Every option for row 0 is "infeasible" (a domain-level fact, not something the raw
    // solver rejects -- the raw solver has no notion of infeasible, only expensive); it
    // must still return a full permutation, letting the caller detect the sentinel in the
    // result rather than the solver throwing or hanging.
    constexpr double kSentinel = 1.0e9;
    const std::vector<std::vector<double>> cost{
        {kSentinel, kSentinel}, {1.0, 2.0}};
    const HungarianResult r = solveSquareAssignment(cost);
    CHECK(isPermutation(r.rowToColumn, 2));
    CHECK(cost[0][r.rowToColumn[0]] >= kSentinel);
}

// ---------------------------------------------------------------------------
// Numerical (4)
// ---------------------------------------------------------------------------

ORCA_TEST(Hungarian, TinyFloatingPointDifferencesResolveCorrectly) {
    const std::vector<std::vector<double>> cost{
        {1.0000001, 1.0000002}, {1.0000002, 1.0000001}};
    const HungarianResult r = solveSquareAssignment(cost);
    CHECK(isPermutation(r.rowToColumn, 2));
    CHECK_NEAR(r.totalCost, 2.0000002, 1e-6);
}

ORCA_TEST(Hungarian, AllEqualCostsStillReturnsValidPermutation) {
    const std::vector<std::vector<double>> cost{
        {5.0, 5.0, 5.0}, {5.0, 5.0, 5.0}, {5.0, 5.0, 5.0}};
    const HungarianResult r = solveSquareAssignment(cost);
    CHECK(isPermutation(r.rowToColumn, 3));
    CHECK_NEAR(r.totalCost, 15.0, 1e-9);
}

ORCA_TEST(Hungarian, ZeroCostMatrixIsOptimal) {
    const std::vector<std::vector<double>> cost{{0.0, 0.0}, {0.0, 0.0}};
    const HungarianResult r = solveSquareAssignment(cost);
    CHECK(isPermutation(r.rowToColumn, 2));
    CHECK_NEAR(r.totalCost, 0.0, 1e-12);
}

ORCA_TEST(Hungarian, LargerMagnitudeValuesStayNumericallyStable) {
    const std::vector<std::vector<double>> cost{
        {100000.0, 200000.5}, {150000.25, 50000.0}};
    const HungarianResult r = solveSquareAssignment(cost);
    CHECK(isPermutation(r.rowToColumn, 2));
    // Optimal: (0,0)+(1,1) = 150000.0 vs (0,1)+(1,0) = 350000.75 -- diagonal wins.
    CHECK_NEAR(r.totalCost, 150000.0, 1e-3);
}

// ---------------------------------------------------------------------------
// Operation count sanity (not part of the brief's numbered groups, but cheap to check: the
// counter this file's header promises actually moves and is non-negative).
// ---------------------------------------------------------------------------

ORCA_TEST(Hungarian, OperationCountIsPositiveAndGrowsWithSize) {
    const std::vector<std::vector<double>> small{{1.0, 2.0}, {2.0, 1.0}};
    const std::vector<std::vector<double>> larger{
        {4.0, 1.0, 3.0, 8.0, 6.0},
        {2.0, 5.0, 9.0, 1.0, 3.0},
        {7.0, 3.0, 2.0, 6.0, 4.0},
        {5.0, 6.0, 4.0, 2.0, 8.0},
        {1.0, 8.0, 5.0, 3.0, 2.0}};
    const HungarianResult a = solveSquareAssignment(small);
    const HungarianResult b = solveSquareAssignment(larger);
    CHECK(a.operationCount > 0);
    CHECK(b.operationCount > a.operationCount);
}
