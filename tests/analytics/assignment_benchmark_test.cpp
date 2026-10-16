/// `runAssignmentBenchmark()` tests — Phase 11 brief §31: Greedy vs. Hungarian at
/// controlled square matrix sizes, candidate-generation time kept separate from solver time.

#include "../../cpp/analytics/AssignmentBenchmark.hpp"
#include "../TestFramework.hpp"

using namespace orca::analytics;

ORCA_TEST(AssignmentBenchmark, ProducesTheDocumentedRowCount) {
    const AssignmentBenchmarkConfig config{
        .familyName = "normal", .matrixSize = 5, .seed = orca::Seed(1), .warmupRuns = 1, .repetitions = 3,
    };
    const std::vector<BenchmarkRow> rows = runAssignmentBenchmark(config);
    CHECK_EQ(rows.size(), std::size_t{2 * (1 + 3)});  // GREEDY + HUNGARIAN per run
}

ORCA_TEST(AssignmentBenchmark, CandidateGenerationTimeIsReportedSeparatelyFromSolverTime) {
    const AssignmentBenchmarkConfig config{
        .familyName = "normal", .matrixSize = 5, .seed = orca::Seed(1), .warmupRuns = 0, .repetitions = 1,
    };
    for (const BenchmarkRow& r : runAssignmentBenchmark(config)) {
        CHECK(r.candidateGenerationTimeMs.has_value());
        CHECK(r.solverTimeMs.has_value());
        CHECK(*r.candidateGenerationTimeMs >= 0.0);
        CHECK(*r.solverTimeMs >= 0.0);
        CHECK(r.assignment.has_value());
    }
}

ORCA_TEST(AssignmentBenchmark, SupportsTheDocumentedMatrixSizes) {
    for (int n : {2, 5, 10, 25}) {
        const AssignmentBenchmarkConfig config{
            .familyName = "normal", .matrixSize = n, .seed = orca::Seed(3), .warmupRuns = 0,
            .repetitions = 1,
        };
        const std::vector<BenchmarkRow> rows = runAssignmentBenchmark(config);
        for (const BenchmarkRow& r : rows) {
            CHECK_EQ(r.objectCount, n);
            CHECK(r.assignment->solverMatrixDimension >= static_cast<std::size_t>(n));
        }
    }
}

ORCA_TEST(AssignmentBenchmark, GreedyAndHungarianSolveTheIdenticalMatrix) {
    // Both solvers report the same vehicle/emergency counts and candidate-pair counts,
    // because they ran on the SAME matrix built once per repetition -- only the assignment
    // itself (and its cost/operation count) may differ between them.
    const AssignmentBenchmarkConfig config{
        .familyName = "normal", .matrixSize = 8, .seed = orca::Seed(9), .warmupRuns = 0, .repetitions = 1,
    };
    const std::vector<BenchmarkRow> rows = runAssignmentBenchmark(config);
    CHECK_EQ(rows.size(), std::size_t{2});
    CHECK_EQ(rows[0].assignment->vehicleCount, rows[1].assignment->vehicleCount);
    CHECK_EQ(rows[0].assignment->candidatePairsEvaluated, rows[1].assignment->candidatePairsEvaluated);
}
