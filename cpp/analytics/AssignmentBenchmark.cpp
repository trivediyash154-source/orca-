#include "AssignmentBenchmark.hpp"

#include <chrono>

#include "../core/Validation.hpp"
#include "../graph/GraphBuilder.hpp"
#include "../optimization/AssignmentCost.hpp"
#include "../optimization/AssignmentMatrix.hpp"
#include "../optimization/CandidateGeneration.hpp"
#include "../optimization/OptimizationProfile.hpp"
#include "../planning/GlobalReplanner.hpp"
#include "../transfer/TransferModel.hpp"

namespace orca::analytics {

namespace {

std::vector<Emergency> buildSyntheticEmergencies(const std::vector<Satellite>& satellites) {
    std::vector<Emergency> emergencies;
    emergencies.reserve(satellites.size());
    for (std::size_t i = 0; i < satellites.size(); ++i) {
        emergencies.emplace_back(EmergencyId(static_cast<std::uint32_t>(i + 1)), satellites[i].id(),
                                 EmergencyKind::PropulsionFailure, Severity::High, 0.0, 5.0e7);
    }
    return emergencies;
}

}  // namespace

std::vector<BenchmarkRow> runAssignmentBenchmark(const AssignmentBenchmarkConfig& config) {
    requireThat(config.matrixSize > 0, "runAssignmentBenchmark: matrixSize must be positive");

    ScenarioGeneratorConfig scenarioConfig = scenario_family::normal(config.matrixSize);
    scenarioConfig.vehicleCount = config.matrixSize;  // square matrix, brief's stated sizes
    const Scenario scenario = generateScenario(config.familyName, config.seed, scenarioConfig);

    const TransferModel model;
    const MissionGraph graph = GraphBuilder::build(scenario, model).graph;
    const std::vector<Emergency> emergencies = buildSyntheticEmergencies(scenario.satellites());
    const OptimizationProfile profile = OptimizationProfile::balanced();

    std::vector<VehicleId> vehicleIds;
    for (const RescueVehicle& v : scenario.vehicles()) vehicleIds.push_back(v.id());
    std::vector<EmergencyId> emergencyIds;
    for (const Emergency& e : emergencies) emergencyIds.push_back(e.id());

    std::vector<BenchmarkRow> rows;
    const int totalRuns = config.warmupRuns + config.repetitions;
    rows.reserve(static_cast<std::size_t>(totalRuns) * 2);

    for (int i = 0; i < totalRuns; ++i) {
        const int repetition = i - config.warmupRuns;

        PlanId nextPlanId(1);
        const auto genStart = std::chrono::steady_clock::now();
        const std::vector<CandidateFront> fronts =
            generateAllCandidateFronts(graph, model, scenario.vehicles(), emergencies, 0.0, nextPlanId);
        const std::vector<ScoredCandidate> scored = scoreCandidates(fronts, profile);
        const std::vector<AssignmentCostEntry> costs =
            buildAssignmentCosts(fronts, scored, emergencies, 0.0, profile, defaultPriorityWeights());
        const AssignmentMatrix matrix = buildAssignmentMatrix(costs, vehicleIds, emergencyIds);
        const auto genEnd = std::chrono::steady_clock::now();
        const double candidateGenerationTimeMs =
            std::chrono::duration<double, std::milli>(genEnd - genStart).count();

        const AssignmentResult greedy = solveAssignmentGreedy(matrix, fronts, scored);
        rows.push_back(BenchmarkRow{
            .scenarioFamily = config.familyName,
            .objectCount = config.matrixSize,
            .seed = config.seed,
            .algorithm = "GREEDY",
            .repetition = repetition,
            .found = greedy.assignedVehicleCount > 0,
            .executionTimeMs = candidateGenerationTimeMs + greedy.metrics.executionTimeMs,
            .assignment = greedy.metrics,
            .candidateGenerationTimeMs = candidateGenerationTimeMs,
            .solverTimeMs = greedy.metrics.executionTimeMs,
        });

        const AssignmentResult hungarian = solveAssignmentHungarian(matrix, fronts, scored);
        rows.push_back(BenchmarkRow{
            .scenarioFamily = config.familyName,
            .objectCount = config.matrixSize,
            .seed = config.seed,
            .algorithm = "HUNGARIAN",
            .repetition = repetition,
            .found = hungarian.assignedVehicleCount > 0,
            .executionTimeMs = candidateGenerationTimeMs + hungarian.metrics.executionTimeMs,
            .assignment = hungarian.metrics,
            .candidateGenerationTimeMs = candidateGenerationTimeMs,
            .solverTimeMs = hungarian.metrics.executionTimeMs,
        });
    }

    return rows;
}

}  // namespace orca::analytics
