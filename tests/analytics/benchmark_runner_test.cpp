/// `runSearchComparisonBenchmark()` tests — Phase 11 brief §22-26: reproducible, warm-up
/// excluded, one row per (algorithm, run).

#include "../../cpp/analytics/BenchmarkRunner.hpp"
#include "../TestFramework.hpp"

using namespace orca;
using namespace orca::analytics;

ORCA_TEST(BenchmarkRunner, ProducesTheDocumentedRowCount) {
    const SearchBenchmarkConfig config{
        .familyName = "normal",
        .scenarioConfig = scenario_family::normal(25),
        .seed = Seed(1),
        .warmupRuns = 1,
        .repetitions = 3,
    };
    const std::vector<BenchmarkRow> rows = runSearchComparisonBenchmark(config);
    CHECK_EQ(rows.size(), std::size_t{3 * (1 + 3)});  // 3 algorithms x (warmup + repetitions)
}

ORCA_TEST(BenchmarkRunner, WarmupRowsAreMarkedWithNegativeRepetition) {
    const SearchBenchmarkConfig config{
        .familyName = "normal", .scenarioConfig = scenario_family::normal(25), .seed = Seed(1),
        .warmupRuns = 2, .repetitions = 2,
    };
    const std::vector<BenchmarkRow> rows = runSearchComparisonBenchmark(config);
    int warmupCount = 0, measuredCount = 0;
    for (const BenchmarkRow& r : rows) {
        if (r.repetition < 0) ++warmupCount; else ++measuredCount;
    }
    CHECK_EQ(warmupCount, 3 * 2);
    CHECK_EQ(measuredCount, 3 * 2);
}

ORCA_TEST(BenchmarkRunner, EachAlgorithmProducesOnlyItsOwnApplicableMetrics) {
    const SearchBenchmarkConfig config{
        .familyName = "normal", .scenarioConfig = scenario_family::normal(25), .seed = Seed(1),
        .warmupRuns = 0, .repetitions = 1,
    };
    for (const BenchmarkRow& r : runSearchComparisonBenchmark(config)) {
        if (r.algorithm == "DIJKSTRA" || r.algorithm == "ASTAR") {
            CHECK(r.search.has_value());
            CHECK(!r.pareto.has_value());
            CHECK(!r.assignment.has_value());
        } else if (r.algorithm == "PARETO") {
            CHECK(!r.search.has_value());
            CHECK(r.pareto.has_value());
        }
    }
}

ORCA_TEST(BenchmarkRunner, CountersAreIdenticalAcrossRepetitionsOnlyTimingVaries) {
    // SearchMetrics.hpp's own claim: counters are deterministic, only executionTimeMs
    // varies between repetitions of the identical search.
    const SearchBenchmarkConfig config{
        .familyName = "normal", .scenarioConfig = scenario_family::normal(25), .seed = Seed(1),
        .warmupRuns = 0, .repetitions = 5,
    };
    const std::vector<BenchmarkRow> rows = runSearchComparisonBenchmark(config);
    std::optional<int> firstNodesExpanded;
    for (const BenchmarkRow& r : rows) {
        if (r.algorithm != "DIJKSTRA") continue;
        if (!firstNodesExpanded) {
            firstNodesExpanded = r.search->nodesExpanded;
        } else {
            CHECK_EQ(r.search->nodesExpanded, *firstNodesExpanded);
        }
    }
}

ORCA_TEST(BenchmarkRunner, SameSeedProducesTheSameResultAcrossSeparateCalls) {
    const SearchBenchmarkConfig config{
        .familyName = "normal", .scenarioConfig = scenario_family::normal(50), .seed = Seed(928371),
        .warmupRuns = 0, .repetitions = 1,
    };
    const std::vector<BenchmarkRow> a = runSearchComparisonBenchmark(config);
    const std::vector<BenchmarkRow> b = runSearchComparisonBenchmark(config);
    for (std::size_t i = 0; i < a.size(); ++i) {
        CHECK_EQ(a[i].found, b[i].found);
        if (a[i].search) {
            CHECK_EQ(a[i].search->nodesExpanded, b[i].search->nodesExpanded);
        }
    }
}
