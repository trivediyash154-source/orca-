#include "BenchmarkRunner.hpp"

#include "../algorithms/AStar.hpp"
#include "../algorithms/Dijkstra.hpp"
#include "../algorithms/ParetoSearch.hpp"
#include "../core/Validation.hpp"
#include "../graph/GraphBuilder.hpp"
#include "../optimization/CandidateGeneration.hpp"
#include "../transfer/TransferModel.hpp"

namespace orca::analytics {

std::vector<BenchmarkRow> runSearchComparisonBenchmark(const SearchBenchmarkConfig& config) {
    const Scenario scenario = generateScenario(config.familyName, config.seed, config.scenarioConfig);
    requireThat(!scenario.vehicles().empty() && !scenario.satellites().empty(),
                "runSearchComparisonBenchmark: scenario has no vehicle or no satellite");

    const TransferModel model;
    const MissionGraph graph = GraphBuilder::build(scenario, model).graph;
    const EntityNodeIndex index = buildEntityNodeIndex(graph);
    const NodeId source = index.vehicleNodes.at(scenario.vehicles().front().id().value());
    const NodeId target = index.satelliteNodes.at(scenario.satellites().front().id().value());

    SearchConstraints constraints;
    constraints.vehicleDeltaVBudgetKms = scenario.vehicles().front().deltaVBudgetKms();
    constraints.maxWaitSec = 1.0e13;

    std::vector<BenchmarkRow> rows;
    const int totalRuns = config.warmupRuns + config.repetitions;
    rows.reserve(static_cast<std::size_t>(totalRuns) * 3);

    for (int i = 0; i < totalRuns; ++i) {
        const int repetition = i - config.warmupRuns;  // negative during warm-up

        const PathResult dijkstra = findEarliestArrival(graph, model, source, target, 0.0, constraints);
        rows.push_back(BenchmarkRow{
            .scenarioFamily = config.familyName,
            .objectCount = config.scenarioConfig.satelliteCount,
            .seed = config.seed,
            .algorithm = "DIJKSTRA",
            .repetition = repetition,
            .found = dijkstra.isFound(),
            .executionTimeMs = dijkstra.metrics().executionTimeMs,
            .search = dijkstra.metrics(),
        });

        const PathResult aStar = findEarliestArrivalAStar(graph, model, source, target, 0.0, constraints);
        rows.push_back(BenchmarkRow{
            .scenarioFamily = config.familyName,
            .objectCount = config.scenarioConfig.satelliteCount,
            .seed = config.seed,
            .algorithm = "ASTAR",
            .repetition = repetition,
            .found = aStar.isFound(),
            .executionTimeMs = aStar.metrics().executionTimeMs,
            .search = aStar.metrics(),
        });

        const ParetoSearchResult pareto = findParetoRoutes(graph, model, source, target, 0.0, constraints);
        rows.push_back(BenchmarkRow{
            .scenarioFamily = config.familyName,
            .objectCount = config.scenarioConfig.satelliteCount,
            .seed = config.seed,
            .algorithm = "PARETO",
            .repetition = repetition,
            .found = !pareto.isEmpty(),
            .executionTimeMs = pareto.metrics.executionTimeMs,
            .pareto = pareto.metrics,
        });
    }

    return rows;
}

}  // namespace orca::analytics
