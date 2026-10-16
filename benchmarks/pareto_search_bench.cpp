/// @file pareto_search_bench.cpp
/// Deterministic micro-benchmark for `findParetoRoutes()`: label growth at increasing
/// scenario size, and an exact-vs-epsilon-dominance comparison on a scenario deliberately
/// built to produce many trade-offs.
///
/// Same rationale as `dijkstra_vs_astar_bench.cpp` (Phase 4): a standalone tool, separate
/// from `orca_engine`'s `bench` verb (deferred to Phase 13, `cpp/app/main.cpp`), scoped to
/// what actually exists. Every number this program prints comes from running the real
/// `findParetoRoutes()` against a real `MissionGraph` — nothing is estimated.
///
/// ## Build
///
/// `scripts/run_pareto_benchmark.sh` (release flags: -O2 -DNDEBUG).
///
/// ## Scenario generation, and why it differs from Phase 4's
///
/// Phase 4's Dijkstra-vs-A* benchmark used an altitude-sorted backbone specifically for
/// guaranteed connectivity on a *single-objective* search. Label explosion is a
/// *multi-objective* phenomenon: it needs genuine trade-offs to retain more than one
/// label per node, which (per `pareto_search_test.cpp`'s file comment) turned out to
/// require deliberately varied altitude/phase/inclination combinations rather than a
/// simple chain — realistic orbital parameters are more correlated across objectives than
/// the textbook Pareto picture suggests. This benchmark's "fan" scenarios reflect that:
/// each stage offers several structurally distinct intermediate orbits rather than one.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <optional>
#include <vector>

#include "../cpp/algorithms/ParetoSearch.hpp"
#include "../cpp/core/RandomGenerator.hpp"
#include "../cpp/graph/MissionGraph.hpp"

using namespace orca;

namespace {

struct ScenarioSpec {
    std::string name;
    int stageCount;      ///< number of "layers" between source and target
    int branchingPerStage;  ///< candidate nodes per layer
    Seed seed;
};

const ScenarioSpec kScenarios[] = {
    {"tiny", 2, 3, Seed{0x0B1000ULL}},
    {"small", 3, 4, Seed{0x0B2000ULL}},
    {"medium", 4, 5, Seed{0x0B3000ULL}},
    {"larger", 5, 6, Seed{0x0B4000ULL}},
};

constexpr int kWarmupRuns = 3;
constexpr int kMeasuredRuns = 10;

TransferConstraints permissiveBuildConstraints() {
    TransferConstraints c;
    c.maxDeltaVKms = 1.0e6;
    c.deltaVBudgetKms = 1.0e6;
    c.availableFuelFraction = 1.0;
    c.maxWaitSec = 1.0e13;
    c.latestArrivalSec = 1.0e18;
    return c;
}

SearchConstraints realisticSearchConstraints() {
    SearchConstraints o;
    o.vehicleDeltaVBudgetKms = 2.5;  // realistic vehicle budget, not the permissive build
                                     // constant -- makes fuel a genuinely varying, non-
                                     // negligible objective (see Phase 5 report).
    o.maxWaitSec = 1.0e13;
    o.latestArrivalSec = 1.0e18;
    return o;
}

struct BuiltGraph {
    MissionGraph graph;
    NodeId source{0};
    NodeId target{0};
    int edgeCount = 0;
};

/// A layered "fan" graph: source -> stage 1 (branchingPerStage nodes) -> stage 2 -> ... ->
/// target, fully connected between consecutive stages. Each node's (altitude, phase,
/// inclination) is drawn independently, giving every route through the fan a genuinely
/// different combination -- unlike a single backbone, this creates many opportunities for
/// incomparable trade-offs to survive.
BuiltGraph buildFanScenario(const ScenarioSpec& spec, const TransferModel& model) {
    RandomGenerator rng(spec.seed);
    const TransferConstraints c = permissiveBuildConstraints();

    BuiltGraph built;
    const Orbit sourceOrbit = Orbit::fromAltitude(500.0, 0.0, 0.0, 0.0);
    built.source = built.graph.addNode(GraphNode{NodeId{0}, SatelliteId{0}, sourceOrbit});

    std::uint32_t satCounter = 1;
    std::vector<NodeId> previousStage{built.source};

    for (int stage = 0; stage < spec.stageCount; ++stage) {
        std::vector<NodeId> currentStage;
        for (int branch = 0; branch < spec.branchingPerStage; ++branch) {
            const Kilometres altitudeKm =
                500.0 + static_cast<double>(stage + 1) * rng.nextDoubleInRange(150.0, 500.0);
            const Radians phaseRad = rng.nextDoubleInRange(0.0, 6.283185307179586);
            const Radians inclRad = degToRad(rng.nextDoubleInRange(0.5, 18.0));
            const Orbit orbit = Orbit::fromAltitude(altitudeKm, inclRad, 0.0, phaseRad);
            const NodeId node =
                built.graph.addNode(GraphNode{NodeId{0}, SatelliteId{satCounter++}, orbit});
            currentStage.push_back(node);

            for (NodeId prev : previousStage) {
                const TransferResult r = model.evaluate(built.graph.node(prev).orbit(),
                                                         orbit, 0.0, c);
                if (r.feasible) {
                    built.graph.addEdge(prev, node, r, 0.0);
                    ++built.edgeCount;
                }
            }
        }
        previousStage = currentStage;
    }

    // Target: one more real node, connected from every node in the final stage.
    const Kilometres targetAltitudeKm =
        500.0 + static_cast<double>(spec.stageCount + 1) * rng.nextDoubleInRange(150.0, 500.0);
    const Orbit targetOrbit = Orbit::fromAltitude(
        targetAltitudeKm, degToRad(rng.nextDoubleInRange(0.5, 18.0)), 0.0,
        rng.nextDoubleInRange(0.0, 6.283185307179586));
    built.target =
        built.graph.addNode(GraphNode{NodeId{0}, SatelliteId{satCounter++}, targetOrbit});
    for (NodeId prev : previousStage) {
        const TransferResult r =
            model.evaluate(built.graph.node(prev).orbit(), targetOrbit, 0.0, c);
        if (r.feasible) {
            built.graph.addEdge(prev, built.target, r, 0.0);
            ++built.edgeCount;
        }
    }

    return built;
}

struct TimingStats {
    double medianMs = 0.0;
    double meanMs = 0.0;
};

TimingStats summarize(std::vector<double> samplesMs) {
    std::sort(samplesMs.begin(), samplesMs.end());
    TimingStats stats;
    const std::size_t n = samplesMs.size();
    stats.medianMs = (n % 2 == 0) ? (samplesMs[n / 2 - 1] + samplesMs[n / 2]) / 2.0
                                  : samplesMs[n / 2];
    double sum = 0.0;
    for (double v : samplesMs) sum += v;
    stats.meanMs = sum / static_cast<double>(n);
    return stats;
}

ParetoSearchResult runOnce(const BuiltGraph& built, const TransferModel& model,
                           const SearchConstraints& sc, const ParetoSearchOptions& opts) {
    return findParetoRoutes(built.graph, model, built.source, built.target, 0.0, sc, opts);
}

}  // namespace

int main() {
    const TransferModel model;
    const SearchConstraints sc = realisticSearchConstraints();

    std::printf("ORCA Phase 5 micro-benchmark: Pareto search label growth\n");
    std::printf(
        "Protocol: %d warm-up (discarded) + %d measured runs per scenario; median is the "
        "headline statistic (BENCHMARK_PLAN.md %s2.3).\n\n",
        kWarmupRuns, kMeasuredRuns, "\xC2\xA7");

    std::printf("%-8s %6s %6s %10s %10s %10s %10s %10s %10s %10s %12s\n", "Scenario",
               "Nodes", "Edges", "Frontier", "LabelGen", "LabelIns", "LabelDom",
               "LabelDup", "DomCmp", "MaxAtNode", "MedianMs");

    bool anyInstability = false;

    for (const ScenarioSpec& spec : kScenarios) {
        const BuiltGraph built = buildFanScenario(spec, model);
        const ParetoSearchOptions exactOpts;  // epsilonDominance = nullopt: exact mode

        for (int i = 0; i < kWarmupRuns; ++i) {
            (void)runOnce(built, model, sc, exactOpts);
        }

        std::vector<double> timingsMs;
        ParetoSearchMetrics referenceMetrics;
        std::size_t referenceFrontierSize = 0;
        bool haveReference = false;
        bool countersStable = true;

        for (int i = 0; i < kMeasuredRuns; ++i) {
            const ParetoSearchResult result = runOnce(built, model, sc, exactOpts);
            timingsMs.push_back(result.metrics.executionTimeMs);
            if (!haveReference) {
                haveReference = true;
                referenceMetrics = result.metrics;
                referenceFrontierSize = result.solutions.size();
            } else if (result.solutions.size() != referenceFrontierSize ||
                      result.metrics.labelsGenerated != referenceMetrics.labelsGenerated ||
                      result.metrics.labelsInserted != referenceMetrics.labelsInserted ||
                      result.metrics.dominanceComparisons !=
                          referenceMetrics.dominanceComparisons) {
                countersStable = false;
            }
        }

        const TimingStats timing = summarize(std::move(timingsMs));
        std::printf("%-8s %6zu %6d %10zu %10d %10d %10d %10d %10d %10d %12.5f\n",
                   spec.name.c_str(), built.graph.nodeCount(), built.edgeCount,
                   referenceFrontierSize, referenceMetrics.labelsGenerated,
                   referenceMetrics.labelsInserted, referenceMetrics.labelsDominated,
                   referenceMetrics.labelsDuplicate, referenceMetrics.dominanceComparisons,
                   referenceMetrics.maximumLabelsAtNode, timing.medianMs);

        if (!countersStable) {
            anyInstability = true;
            std::fprintf(stderr,
                        "WARNING: %s scenario's counters were NOT identical across all "
                        "%d repetitions.\n",
                        spec.name.c_str(), kMeasuredRuns);
        }
    }

    // ---- Exact vs epsilon-dominance, on the scenario with the richest real frontier. ---
    // Not necessarily the largest scenario by node count: 'larger' (5 stages) turned out
    // fully unreachable under a realistic 2.5 km/s budget (every route's cumulative
    // delta-v exceeds it -- itself a real, reportable finding, not a benchmark bug), which
    // gives an empty frontier to compare against regardless of epsilon. 'medium' has the
    // richest actual frontier (19 routes) of the four scenarios measured above, so that is
    // used here instead.
    std::printf(
        "\nExact vs epsilon-dominance comparison (richest real frontier, 'medium'):\n");
    const BuiltGraph largest = buildFanScenario(kScenarios[2], model);
    const ParetoSearchOptions exactOpts;
    const ParetoSearchResult exactResult = runOnce(largest, model, sc, exactOpts);

    std::printf("%-14s %10s %14s %10s\n", "Mode", "Frontier", "DomComparisons", "MedianMs");
    {
        std::vector<double> timings;
        for (int i = 0; i < kWarmupRuns; ++i) (void)runOnce(largest, model, sc, exactOpts);
        for (int i = 0; i < kMeasuredRuns; ++i)
            timings.push_back(runOnce(largest, model, sc, exactOpts).metrics.executionTimeMs);
        const TimingStats t = summarize(std::move(timings));
        std::printf("%-14s %10zu %14d %10.5f\n", "exact", exactResult.solutions.size(),
                   exactResult.metrics.dominanceComparisons, t.medianMs);
    }

    for (double epsilon : {0.02, 0.05, 0.10, 0.20}) {
        ParetoSearchOptions epsOpts;
        epsOpts.epsilonDominance = epsilon;
        const ParetoSearchResult epsResult = runOnce(largest, model, sc, epsOpts);
        std::vector<double> timings;
        for (int i = 0; i < kWarmupRuns; ++i) (void)runOnce(largest, model, sc, epsOpts);
        for (int i = 0; i < kMeasuredRuns; ++i)
            timings.push_back(runOnce(largest, model, sc, epsOpts).metrics.executionTimeMs);
        const TimingStats t = summarize(std::move(timings));
        char label[32];
        std::snprintf(label, sizeof(label), "eps=%.2f", epsilon);
        std::printf("%-14s %10zu %14d %10.5f\n", label, epsResult.solutions.size(),
                   epsResult.metrics.dominanceComparisons, t.medianMs);
    }

    std::printf(
        "\nNote: frontier size is expected to be non-increasing as epsilon grows (more "
        "labels collapse as duplicates-within-tolerance) -- see "
        "docs/BENCHMARK_PLAN.md's Phase 5 addendum for the honest reading of these "
        "results, including whether it actually decreased here.\n");

    return anyInstability ? 1 : 0;
}
