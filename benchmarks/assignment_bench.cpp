/// @file assignment_bench.cpp
/// Phase 6 micro-benchmark: (1) `solveSquareAssignment()`'s raw scaling from 2x2 to
/// 100x100, in isolation from any domain code, and (2) the full integrated pipeline
/// (candidate generation -> scoring -> cost matrix -> Hungarian) timed stage by stage, so
/// it can be reported honestly whether Hungarian is actually the bottleneck (Phase 6 brief
/// §25) rather than assumed.
///
/// Same rationale and build pattern as `pareto_search_bench.cpp` (Phase 5): a standalone
/// tool, `scripts/run_assignment_benchmark.sh` (release flags: -O2 -DNDEBUG). Every number
/// printed here comes from running the real production code against a real (if
/// synthetically generated) scenario -- nothing is estimated or hand-computed.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <vector>

#include "../cpp/algorithms/Hungarian.hpp"
#include "../cpp/core/RandomGenerator.hpp"
#include "../cpp/domain/RescueVehicle.hpp"
#include "../cpp/domain/Emergency.hpp"
#include "../cpp/graph/MissionGraph.hpp"
#include "../cpp/optimization/AssignmentCost.hpp"
#include "../cpp/optimization/AssignmentMatrix.hpp"
#include "../cpp/optimization/AssignmentResult.hpp"
#include "../cpp/optimization/CandidateGeneration.hpp"

using namespace orca;

namespace {

constexpr int kWarmupRuns = 2;
constexpr int kMeasuredRuns = 7;

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

double timeOnceMs(const std::vector<std::vector<double>>& cost) {
    const auto start = std::chrono::steady_clock::now();
    const HungarianResult r = solveSquareAssignment(cost);
    const auto end = std::chrono::steady_clock::now();
    (void)r;
    return std::chrono::duration<double, std::milli>(end - start).count();
}

std::vector<std::vector<double>> randomSquareMatrix(std::size_t n, RandomGenerator& rng) {
    std::vector<std::vector<double>> m(n, std::vector<double>(n, 0.0));
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = 0; j < n; ++j) {
            m[i][j] = rng.nextDoubleInRange(0.0, 100.0);
        }
    }
    return m;
}

// ---------------------------------------------------------------------------
// Stage 2: integrated pipeline, timed stage by stage.
// ---------------------------------------------------------------------------

TransferConstraints permissive() {
    TransferConstraints c;
    c.maxDeltaVKms = 1.0e6;
    c.deltaVBudgetKms = 1.0e6;
    c.availableFuelFraction = 1.0;
    c.maxWaitSec = 1.0e13;
    c.latestArrivalSec = 1.0e18;
    return c;
}

struct IntegratedScenario {
    MissionGraph graph;
    TransferModel model;
    std::vector<RescueVehicle> vehicles;
    std::vector<Emergency> emergencies;
};

/// `vehicleCount` vehicles and `emergencyCount` emergencies, each with an independently
/// randomised (but deterministic, seeded) orbit, fully cross-connected -- every vehicle can
/// at least attempt every emergency (some pairs may still be infeasible on delta-v or
/// deadline grounds, same as any real scenario).
IntegratedScenario buildScenario(int vehicleCount, int emergencyCount, Seed seed) {
    IntegratedScenario s;
    RandomGenerator rng(seed);
    const TransferConstraints c = permissive();

    std::vector<NodeId> vehicleNodes;
    std::vector<NodeId> emergencyNodes;

    for (int i = 0; i < vehicleCount; ++i) {
        const Kilometres alt = rng.nextDoubleInRange(450.0, 1500.0);
        const Radians incl = degToRad(rng.nextDoubleInRange(0.0, 40.0));
        const Radians raan = degToRad(rng.nextDoubleInRange(0.0, 360.0));
        const Radians phase = rng.nextDoubleInRange(0.0, 6.283185307179586);
        const Orbit orbit = Orbit::fromAltitude(alt, incl, raan, phase);
        const NodeId node = s.graph.addNode(
            GraphNode{NodeId{0}, VehicleId(static_cast<std::uint32_t>(i + 1)), orbit});
        vehicleNodes.push_back(node);
        s.vehicles.emplace_back(VehicleId(static_cast<std::uint32_t>(i + 1)),
                                "RV-" + std::to_string(i + 1), orbit, 5.0);
    }
    for (int j = 0; j < emergencyCount; ++j) {
        const Kilometres alt = rng.nextDoubleInRange(450.0, 1500.0);
        const Radians incl = degToRad(rng.nextDoubleInRange(0.0, 40.0));
        const Radians raan = degToRad(rng.nextDoubleInRange(0.0, 360.0));
        const Radians phase = rng.nextDoubleInRange(0.0, 6.283185307179586);
        const Orbit orbit = Orbit::fromAltitude(alt, incl, raan, phase);
        const NodeId node = s.graph.addNode(
            GraphNode{NodeId{0}, SatelliteId(static_cast<std::uint32_t>(100 + j)), orbit});
        emergencyNodes.push_back(node);
        s.emergencies.emplace_back(EmergencyId(static_cast<std::uint32_t>(100 + j)),
                                   SatelliteId(static_cast<std::uint32_t>(100 + j)),
                                   EmergencyKind::PowerFailure, Severity::High, 0.0, 5.0e7);
    }
    for (NodeId vn : vehicleNodes) {
        for (NodeId en : emergencyNodes) {
            const TransferResult r =
                s.model.evaluate(s.graph.node(vn).orbit(), s.graph.node(en).orbit(), 0.0, c);
            s.graph.addEdge(vn, en, r, 0.0);
        }
    }
    return s;
}

struct StageTimingsMs {
    double candidateGeneration = 0.0;
    double scoring = 0.0;
    double costAndMatrix = 0.0;
    double hungarianSolve = 0.0;
};

StageTimingsMs runOnceTimed(IntegratedScenario& s) {
    StageTimingsMs t;

    auto t0 = std::chrono::steady_clock::now();
    const auto fronts =
        generateAllCandidateFronts(s.graph, s.model, s.vehicles, s.emergencies, 0.0, PlanId(1));
    auto t1 = std::chrono::steady_clock::now();
    t.candidateGeneration = std::chrono::duration<double, std::milli>(t1 - t0).count();

    const auto scored = scoreCandidates(fronts, OptimizationProfile::balanced());
    auto t2 = std::chrono::steady_clock::now();
    t.scoring = std::chrono::duration<double, std::milli>(t2 - t1).count();

    const auto costs = buildAssignmentCosts(fronts, scored, s.emergencies, 0.0,
                                            OptimizationProfile::balanced());
    std::vector<VehicleId> vids;
    std::vector<EmergencyId> eids;
    for (const auto& v : s.vehicles) vids.push_back(v.id());
    for (const auto& e : s.emergencies) eids.push_back(e.id());
    const auto matrix = buildAssignmentMatrix(costs, vids, eids);
    auto t3 = std::chrono::steady_clock::now();
    t.costAndMatrix = std::chrono::duration<double, std::milli>(t3 - t2).count();

    const auto result = solveAssignmentHungarian(matrix, fronts, scored);
    (void)result;
    auto t4 = std::chrono::steady_clock::now();
    t.hungarianSolve = std::chrono::duration<double, std::milli>(t4 - t3).count();

    return t;
}

}  // namespace

int main() {
    std::printf("ORCA Phase 6 micro-benchmark: Hungarian assignment\n\n");

    // ---- Part 1: raw Hungarian scaling, isolated from any domain code. ----
    std::printf("Part 1: solveSquareAssignment() raw scaling (random cost matrices)\n");
    std::printf("%-8s %10s %14s %10s\n", "N", "MedianMs", "OperationCnt", "MeanMs");
    const std::size_t sizes[] = {2, 5, 10, 20, 50, 100};
    for (std::size_t n : sizes) {
        RandomGenerator rng(Seed{0xA55E7000ULL + n});
        const auto cost = randomSquareMatrix(n, rng);
        for (int i = 0; i < kWarmupRuns; ++i) (void)timeOnceMs(cost);
        std::vector<double> timings;
        HungarianResult last;
        for (int i = 0; i < kMeasuredRuns; ++i) {
            const auto start = std::chrono::steady_clock::now();
            last = solveSquareAssignment(cost);
            const auto end = std::chrono::steady_clock::now();
            timings.push_back(std::chrono::duration<double, std::milli>(end - start).count());
        }
        const TimingStats stats = summarize(std::move(timings));
        std::printf("%-8zu %10.5f %14ld %10.5f\n", n, stats.medianMs, last.operationCount,
                   stats.meanMs);
    }

    // ---- Part 2: integrated pipeline, timed stage by stage. ----
    std::printf(
        "\nPart 2: integrated pipeline, timed stage by stage (median of %d runs, %d "
        "warm-up)\n",
        kMeasuredRuns, kWarmupRuns);
    std::printf("%-10s %14s %10s %14s %14s %10s\n", "VxE", "CandidateGen", "Scoring",
               "CostAndMatrix", "Hungarian", "TotalMs");

    struct Size {
        int vehicles;
        int emergencies;
    };
    const Size scenarioSizes[] = {{2, 2}, {3, 3}, {5, 5}, {8, 8}, {10, 10}};

    for (const Size& sz : scenarioSizes) {
        IntegratedScenario s =
            buildScenario(sz.vehicles, sz.emergencies,
                         Seed{0xB16A5000ULL + static_cast<std::uint64_t>(sz.vehicles)});
        for (int i = 0; i < kWarmupRuns; ++i) (void)runOnceTimed(s);

        std::vector<double> cand, score, cost, hung;
        for (int i = 0; i < kMeasuredRuns; ++i) {
            const StageTimingsMs t = runOnceTimed(s);
            cand.push_back(t.candidateGeneration);
            score.push_back(t.scoring);
            cost.push_back(t.costAndMatrix);
            hung.push_back(t.hungarianSolve);
        }
        const TimingStats candStats = summarize(std::move(cand));
        const TimingStats scoreStats = summarize(std::move(score));
        const TimingStats costStats = summarize(std::move(cost));
        const TimingStats hungStats = summarize(std::move(hung));
        const double totalMs =
            candStats.medianMs + scoreStats.medianMs + costStats.medianMs + hungStats.medianMs;

        char label[16];
        std::snprintf(label, sizeof(label), "%dx%d", sz.vehicles, sz.emergencies);
        std::printf("%-10s %14.5f %10.5f %14.5f %14.5f %10.5f\n", label, candStats.medianMs,
                   scoreStats.medianMs, costStats.medianMs, hungStats.medianMs, totalMs);
    }

    std::printf(
        "\nNote: candidate generation runs one Pareto search per (vehicle, emergency) pair "
        "-- O(V*E) searches, each independently O(graph size); Hungarian on the padded "
        "(V+E)x(V+E) matrix is O((V+E)^3) but operates on a tiny matrix at these scenario "
        "sizes. Whichever stage actually dominates at each size is read directly from the "
        "columns above, not assumed in advance.\n");

    return 0;
}
