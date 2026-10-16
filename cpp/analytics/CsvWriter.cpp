#include "CsvWriter.hpp"

#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <type_traits>

namespace orca::analytics {

namespace {

template <typename T>
std::string cell(const std::optional<T>& v) {
    if (!v) return "";
    if constexpr (std::is_same_v<T, bool>) {
        return *v ? "true" : "false";
    } else {
        return std::to_string(*v);
    }
}

constexpr const char* kHeader =
    "scenarioFamily,objectCount,seed,algorithm,repetition,found,executionTimeMs,"
    "nodesExpanded,edgesEvaluated,relaxationsSucceeded,relaxationsRejected,priorityQueuePushes,"
    "priorityQueuePops,staleEntriesDiscarded,heuristicEvaluations,"
    "labelsGenerated,labelsInserted,labelsDominated,labelsPruned,labelsExpanded,"
    "dominanceComparisons,queuePushes,queuePops,maximumLabelsAtNode,truncated,"
    "vehicleCount,emergencyCount,candidatePairsEvaluated,feasibleCandidatePairs,assignedPairs,"
    "solverMatrixDimension,solverOperationCount,totalAssignmentCost,"
    "candidateGenerationTimeMs,solverTimeMs";

}  // namespace

void writeBenchmarkCsv(std::ostream& out, const std::vector<BenchmarkRow>& rows, bool writeHeader) {
    if (writeHeader) {
        out << kHeader << "\n";
    }
    for (const BenchmarkRow& r : rows) {
        out << r.scenarioFamily << ',' << r.objectCount << ',' << r.seed << ',' << r.algorithm << ','
            << r.repetition << ',' << (r.found ? "true" : "false") << ',' << r.executionTimeMs << ',';

        out << cell(r.search ? std::optional(r.search->nodesExpanded) : std::nullopt) << ','
            << cell(r.search ? std::optional(r.search->edgesEvaluated) : std::nullopt) << ','
            << cell(r.search ? std::optional(r.search->relaxationsSucceeded) : std::nullopt) << ','
            << cell(r.search ? std::optional(r.search->relaxationsRejected) : std::nullopt) << ','
            << cell(r.search ? std::optional(r.search->priorityQueuePushes) : std::nullopt) << ','
            << cell(r.search ? std::optional(r.search->priorityQueuePops) : std::nullopt) << ','
            << cell(r.search ? std::optional(r.search->staleEntriesDiscarded) : std::nullopt) << ','
            << cell(r.search && r.algorithm == "ASTAR" ? std::optional(r.search->heuristicEvaluations)
                                                       : std::nullopt)
            << ',';

        out << cell(r.pareto ? std::optional(r.pareto->labelsGenerated) : std::nullopt) << ','
            << cell(r.pareto ? std::optional(r.pareto->labelsInserted) : std::nullopt) << ','
            << cell(r.pareto ? std::optional(r.pareto->labelsDominated) : std::nullopt) << ','
            << cell(r.pareto ? std::optional(r.pareto->labelsPruned) : std::nullopt) << ','
            << cell(r.pareto ? std::optional(r.pareto->labelsExpanded) : std::nullopt) << ','
            << cell(r.pareto ? std::optional(r.pareto->dominanceComparisons) : std::nullopt) << ','
            << cell(r.pareto ? std::optional(r.pareto->queuePushes) : std::nullopt) << ','
            << cell(r.pareto ? std::optional(r.pareto->queuePops) : std::nullopt) << ','
            << cell(r.pareto ? std::optional(r.pareto->maximumLabelsAtNode) : std::nullopt) << ','
            << cell(r.pareto ? std::optional(r.pareto->truncated) : std::nullopt) << ',';

        out << cell(r.assignment ? std::optional(r.assignment->vehicleCount) : std::nullopt) << ','
            << cell(r.assignment ? std::optional(r.assignment->emergencyCount) : std::nullopt) << ','
            << cell(r.assignment ? std::optional(r.assignment->candidatePairsEvaluated) : std::nullopt)
            << ','
            << cell(r.assignment ? std::optional(r.assignment->feasibleCandidatePairs) : std::nullopt)
            << ','
            << cell(r.assignment ? std::optional(r.assignment->assignedPairs) : std::nullopt) << ','
            << cell(r.assignment ? std::optional(static_cast<long>(r.assignment->solverMatrixDimension))
                                 : std::nullopt)
            << ','
            << cell(r.assignment ? std::optional(r.assignment->solverOperationCount) : std::nullopt)
            << ','
            << cell(r.assignment ? std::optional(r.assignment->totalAssignmentCost) : std::nullopt)
            << ',';

        out << cell(r.candidateGenerationTimeMs) << ',' << cell(r.solverTimeMs) << "\n";
    }
}

void appendBenchmarkCsv(const std::string& path, const std::vector<BenchmarkRow>& rows) {
    const bool needsHeader = !std::filesystem::exists(path) || std::filesystem::file_size(path) == 0;
    std::ofstream out(path, std::ios::app);
    if (!out) {
        throw std::runtime_error("appendBenchmarkCsv: could not open '" + path + "' for writing");
    }
    writeBenchmarkCsv(out, rows, needsHeader);
}

}  // namespace orca::analytics
