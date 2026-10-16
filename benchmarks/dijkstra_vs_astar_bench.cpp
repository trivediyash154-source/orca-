/// @file dijkstra_vs_astar_bench.cpp
/// Deterministic micro-benchmark comparing time-dependent Dijkstra against A* on the same
/// graphs, at four sizes.
///
/// This is Phase 4's own tool, deliberately separate from `orca_engine`'s `bench` verb
/// (cpp/app/main.cpp documents that verb as arriving in Phase 13, once the full
/// scenario/simulation stack it is specified against — BENCHMARK_PLAN.md's 88,200-run
/// matrix over Pareto search, Hungarian assignment, and outcome metrics — actually exists).
/// Building a fraction of that CLI now, ahead of the subsystems its options name, would be
/// exactly the kind of premature interface this project's phases have consistently avoided
/// elsewhere. What Phase 4 needs is much narrower: does A* expand fewer nodes than
/// Dijkstra on the two algorithms that exist, and at what wall-clock cost for the
/// heuristic evaluations it pays for that. This program answers only that, standalone.
///
/// Every number this program prints comes from actually running the real
/// `findEarliestArrival` / `findEarliestArrivalAStar` against a real, constructed
/// `MissionGraph` -- nothing here is a placeholder or a hand-computed estimate.
///
/// ## Protocol (follows BENCHMARK_PLAN.md §2.3's discipline at Phase 4's smaller scope)
///
/// 3 warm-up runs (discarded) + 10 measured runs per (scenario, algorithm) pair. Timing
/// reports median/mean/stddev/min/max, median is the headline number. Algorithmic counters
/// (nodesExpanded, edgesEvaluated, ...) are deterministic for a fixed graph and query --
/// this program verifies they are byte-identical across all 10 repetitions and reports
/// them once, exactly as BENCHMARK_PLAN.md §3.2 argues counters should be treated (more
/// valuable evidence than timings, because they need no environment disclosure to be
/// reproduced by a reader).
///
/// ## Build
///
/// `scripts/run_astar_benchmark.sh` (release flags: -O2 -DNDEBUG, matching
/// BENCHMARK_PLAN.md §2.4's requirement for every reported measurement).
///
/// ## Graph generation
///
/// Each scenario draws `nodeCount` orbits (altitude, phase, inclination) from a seeded
/// `RandomGenerator` (RandomGenerator.hpp -- portable, not `std::uniform_real_distribution`
/// per that file's determinism guarantee), sorts them by altitude, and builds a graph that
/// is connected by construction: a "backbone" edge between each altitude-sorted node and
/// the next, plus a fixed number of "skip" edges reaching further ahead, so genuine
/// alternative routes exist rather than a single forced chain. Source is the lowest-
/// altitude node, target the highest -- a "climb from a low parking orbit toward a high
/// one" query, which is both a realistic ORCA rescue shape and guaranteed reachable by the
/// backbone alone.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "../cpp/algorithms/AStar.hpp"
#include "../cpp/algorithms/AStarHeuristic.hpp"
#include "../cpp/algorithms/Dijkstra.hpp"
#include "../cpp/core/RandomGenerator.hpp"
#include "../cpp/graph/MissionGraph.hpp"

using namespace orca;

namespace {

struct ScenarioSpec {
    std::string name;
    int nodeCount;
    int skipEdgesPerNode;
    Seed seed;
};

// Seeds distinct from configs/benchmark-seeds.json: that list is committed for the full
// future benchmark protocol (BENCHMARK_PLAN.md §2.2) across every strategy and scenario
// this project will eventually have. This program is a narrower, Phase-4-only tool with
// its own fixed, arbitrary, documented seeds -- reproducible on its own terms without
// implying it participates in the larger protocol.
const ScenarioSpec kScenarios[] = {
    {"tiny", 10, 2, Seed{0x0A1000ULL}},
    {"small", 25, 3, Seed{0x0A2500ULL}},
    {"medium", 50, 4, Seed{0x0A5000ULL}},
    {"larger", 150, 5, Seed{0x0A1500ULL}},
};

constexpr int kWarmupRuns = 3;
constexpr int kMeasuredRuns = 10;

struct BuiltGraph {
    MissionGraph graph;
    NodeId source{0};
    NodeId target{0};
    int edgeCount = 0;
};

TransferConstraints permissiveBuildConstraints() {
    TransferConstraints c;
    c.maxDeltaVKms = 1.0e6;
    c.deltaVBudgetKms = 1.0e6;
    c.availableFuelFraction = 1.0;
    c.maxWaitSec = 1.0e13;
    c.latestArrivalSec = 1.0e18;
    return c;
}

SearchConstraints permissiveSearchConstraints() {
    SearchConstraints o;
    o.vehicleDeltaVBudgetKms = 1.0e6;
    o.maxWaitSec = 1.0e13;
    o.latestArrivalSec = 1.0e18;
    return o;
}

BuiltGraph buildScenario(const ScenarioSpec& spec, const TransferModel& model) {
    RandomGenerator rng(spec.seed);
    const TransferConstraints c = permissiveBuildConstraints();

    struct Draw {
        Kilometres altitudeKm;
        Radians phaseRad;
        Radians inclRad;
    };
    std::vector<Draw> draws;
    draws.reserve(static_cast<std::size_t>(spec.nodeCount));
    for (int i = 0; i < spec.nodeCount; ++i) {
        draws.push_back(Draw{rng.nextDoubleInRange(300.0, 40000.0),
                             rng.nextDoubleInRange(0.0, 6.283185307179586),
                             rng.nextDoubleInRange(0.0, 1.5707963267948966)});
    }
    std::sort(draws.begin(), draws.end(),
             [](const Draw& a, const Draw& b) { return a.altitudeKm < b.altitudeKm; });

    BuiltGraph built;
    std::vector<NodeId> nodes;
    nodes.reserve(draws.size());
    for (std::size_t i = 0; i < draws.size(); ++i) {
        const Orbit orbit =
            Orbit::fromAltitude(draws[i].altitudeKm, draws[i].inclRad, 0.0, draws[i].phaseRad);
        nodes.push_back(built.graph.addNode(
            GraphNode{NodeId{0}, SatelliteId{static_cast<std::uint32_t>(i)}, orbit}));
    }

    const auto tryAddEdge = [&](std::size_t ui, std::size_t vi) {
        const NodeId u = nodes[ui];
        const NodeId v = nodes[vi];
        const TransferResult r =
            model.evaluate(built.graph.node(u).orbit(), built.graph.node(v).orbit(), 0.0, c);
        if (r.feasible) {
            built.graph.addEdge(u, v, r, 0.0);
            ++built.edgeCount;
        }
    };

    for (std::size_t i = 0; i + 1 < nodes.size(); ++i) {
        tryAddEdge(i, i + 1);  // backbone: guarantees connectivity end-to-end
    }
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        for (int k = 2; k <= spec.skipEdgesPerNode + 1; ++k) {
            const std::size_t j = i + static_cast<std::size_t>(k);
            if (j < nodes.size()) {
                tryAddEdge(i, j);
            }
        }
    }

    built.source = nodes.front();
    built.target = nodes.back();
    return built;
}

struct TimingStats {
    double medianMs = 0.0;
    double meanMs = 0.0;
    double stddevMs = 0.0;
    double minMs = 0.0;
    double maxMs = 0.0;
};

TimingStats summarize(std::vector<double> samplesMs) {
    std::sort(samplesMs.begin(), samplesMs.end());
    TimingStats stats;
    const std::size_t n = samplesMs.size();
    stats.minMs = samplesMs.front();
    stats.maxMs = samplesMs.back();
    stats.medianMs = (n % 2 == 0) ? (samplesMs[n / 2 - 1] + samplesMs[n / 2]) / 2.0
                                  : samplesMs[n / 2];
    double sum = 0.0;
    for (double v : samplesMs) {
        sum += v;
    }
    stats.meanMs = sum / static_cast<double>(n);
    double sqSum = 0.0;
    for (double v : samplesMs) {
        sqSum += (v - stats.meanMs) * (v - stats.meanMs);
    }
    stats.stddevMs = std::sqrt(sqSum / static_cast<double>(n));
    return stats;
}

struct AlgorithmRunSummary {
    bool found = false;
    Seconds arrivalTimeSec = 0.0;
    SearchMetrics metrics;
    TimingStats timing;
    bool countersStableAcrossReps = true;
};

template <typename SearchFn>
AlgorithmRunSummary runBenchmarked(SearchFn&& search) {
    for (int i = 0; i < kWarmupRuns; ++i) {
        (void)search();
    }

    AlgorithmRunSummary summary;
    std::vector<double> timingsMs;
    timingsMs.reserve(kMeasuredRuns);

    bool haveReference = false;
    SearchMetrics referenceMetrics;
    bool referenceFound = false;
    Seconds referenceArrival = 0.0;

    for (int i = 0; i < kMeasuredRuns; ++i) {
        const PathResult result = search();
        timingsMs.push_back(result.metrics().executionTimeMs);

        if (!haveReference) {
            haveReference = true;
            referenceMetrics = result.metrics();
            referenceFound = result.isFound();
            referenceArrival = referenceFound ? result.arrivalTimeSec() : 0.0;
        } else {
            const bool sameFound = (result.isFound() == referenceFound);
            const bool sameArrival =
                !referenceFound || std::fabs(result.arrivalTimeSec() - referenceArrival) < 1e-9;
            const SearchMetrics& m = result.metrics();
            const bool sameCounters =
                m.nodesExpanded == referenceMetrics.nodesExpanded &&
                m.edgesEvaluated == referenceMetrics.edgesEvaluated &&
                m.relaxationsSucceeded == referenceMetrics.relaxationsSucceeded &&
                m.relaxationsRejected == referenceMetrics.relaxationsRejected &&
                m.priorityQueuePushes == referenceMetrics.priorityQueuePushes &&
                m.priorityQueuePops == referenceMetrics.priorityQueuePops &&
                m.staleEntriesDiscarded == referenceMetrics.staleEntriesDiscarded &&
                m.heuristicEvaluations == referenceMetrics.heuristicEvaluations;
            if (!sameFound || !sameArrival || !sameCounters) {
                summary.countersStableAcrossReps = false;
            }
        }
    }

    summary.found = referenceFound;
    summary.arrivalTimeSec = referenceArrival;
    summary.metrics = referenceMetrics;
    summary.timing = summarize(std::move(timingsMs));
    return summary;
}

void printRow(const char* algorithm, const ScenarioSpec& spec, const BuiltGraph& built,
             const AlgorithmRunSummary& s) {
    std::printf(
        "%-9s %-8s %6d %6d %8s %14.3f %10d %10d %10d %10d %12.5f %12.5f\n", algorithm,
        spec.name.c_str(), spec.nodeCount, built.edgeCount, s.found ? "yes" : "no",
        s.found ? s.arrivalTimeSec : 0.0, s.metrics.nodesExpanded, s.metrics.edgesEvaluated,
        s.metrics.priorityQueuePushes + s.metrics.priorityQueuePops,
        s.metrics.heuristicEvaluations, s.timing.medianMs, s.timing.meanMs);
}

}  // namespace

int main() {
    const TransferModel model;

    std::printf("ORCA Phase 4 micro-benchmark: Dijkstra vs A*\n");
    std::printf(
        "Protocol: %d warm-up (discarded) + %d measured runs per (scenario, algorithm); "
        "median is the headline statistic (BENCHMARK_PLAN.md %s2.3).\n\n",
        kWarmupRuns, kMeasuredRuns, "\xC2\xA7");

    std::printf("%-9s %-8s %6s %6s %8s %14s %10s %10s %10s %10s %12s %12s\n", "Algorithm",
               "Scenario", "Nodes", "Edges", "Found", "ArrivalSec", "NodesExp", "EdgesEval",
               "QueueOps", "HeurEval", "MedianMs", "MeanMs");

    bool anyInstability = false;

    for (const ScenarioSpec& spec : kScenarios) {
        const BuiltGraph built = buildScenario(spec, model);
        const SearchConstraints sc = permissiveSearchConstraints();

        const AlgorithmRunSummary dijkstra = runBenchmarked([&]() {
            return findEarliestArrival(built.graph, model, built.source, built.target, 0.0,
                                       sc);
        });
        const AlgorithmRunSummary astar = runBenchmarked([&]() {
            return findEarliestArrivalAStar(built.graph, model, built.source, built.target,
                                            0.0, sc);
        });

        printRow("dijkstra", spec, built, dijkstra);
        printRow("astar", spec, built, astar);

        if (!dijkstra.countersStableAcrossReps || !astar.countersStableAcrossReps) {
            anyInstability = true;
            std::fprintf(stderr,
                        "WARNING: %s scenario's counters were NOT identical across all %d "
                        "repetitions -- investigate before trusting this row.\n",
                        spec.name.c_str(), kMeasuredRuns);
        }

        if (dijkstra.found != astar.found) {
            std::fprintf(stderr,
                        "WARNING: %s scenario -- Dijkstra and A* disagree on "
                        "found/not-found. This should never happen; investigate.\n",
                        spec.name.c_str());
        } else if (dijkstra.found && astar.found &&
                  std::fabs(dijkstra.arrivalTimeSec - astar.arrivalTimeSec) > 1e-6) {
            std::fprintf(stderr,
                        "WARNING: %s scenario -- Dijkstra and A* disagree on arrival time "
                        "(%.6f vs %.6f). This should never happen; investigate.\n",
                        spec.name.c_str(), dijkstra.arrivalTimeSec, astar.arrivalTimeSec);
        }
    }

    std::printf(
        "\nNote: A* is not shown as universally faster here, by design -- see "
        "docs/BENCHMARK_PLAN.md's Phase 4 addendum for the honest reading of these "
        "results.\n");

    // Heuristic-quality-ratio diagnostic: h(source,target) / trueCost, where trueCost is
    // Dijkstra's real (independently-computed, not A*-derived) optimal arrival time minus
    // the search's start time (0.0 throughout this program). A ratio near 1.0 means the
    // heuristic was nearly exact for that scenario's source/target pair; near 0.0 means it
    // provided little guidance. trueCost == 0 (only possible if source == target, which
    // never happens in this program's scenarios) is guarded explicitly rather than
    // dividing silently.
    std::printf("\nHeuristic quality ratio (h(source,target) / trueCost):\n");
    std::printf("%-9s %12s %14s %10s\n", "Scenario", "h(sec)", "trueCost(sec)", "ratio");
    for (const ScenarioSpec& spec : kScenarios) {
        const BuiltGraph built = buildScenario(spec, model);
        const SearchConstraints sc = permissiveSearchConstraints();
        const PathResult truth =
            findEarliestArrival(built.graph, model, built.source, built.target, 0.0, sc);
        const Seconds h = astar_heuristic::estimateRemainingTimeSec(
            built.graph.node(built.source).orbit(), built.graph.node(built.target).orbit());
        if (!truth.isFound()) {
            std::printf("%-9s %12.3f %14s %10s\n", spec.name.c_str(), h, "n/a (unreached)",
                       "n/a");
            continue;
        }
        const Seconds trueCost = truth.arrivalTimeSec();  // startTimeSec was 0.0
        if (trueCost == 0.0) {
            std::printf("%-9s %12.3f %14.3f %10s\n", spec.name.c_str(), h, trueCost,
                       "n/a (trueCost=0)");
        } else {
            std::printf("%-9s %12.3f %14.3f %10.6f\n", spec.name.c_str(), h, trueCost,
                       h / trueCost);
        }
    }

    return anyInstability ? 1 : 0;
}
