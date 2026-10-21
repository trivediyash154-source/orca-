// E2: search cost as the mission graph grows (25 to 500 objects).
// Per (family, size, seed): first vehicle to the first 2 satellites. Dijkstra, A*, Martins,
// NAMOA*, and Martins with epsilon-dominance (0.01, 0.05, 0.10). Each timed with one warm-up
// and `reps` measured runs; the median is reported. Effort counters come from one run
// (they are deterministic). NAMOA*'s front is checked against Martins' on every query.
// Usage: e2_scaling <seeds> <reps> <queries.csv> <fronts.csv> <size> [size ...]
#include <cstdlib>
#include <chrono>
#include <functional>

#include "common.hpp"

using namespace orca;
using namespace exp_common;

namespace {
double medianOf(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    const std::size_t n = v.size();
    return n % 2 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}
}  // namespace

int main(int argc, char** argv) {
    const int seeds = std::atoi(argv[1]);
    const int reps = std::atoi(argv[2]);
    FILE* q = std::fopen(argv[3], "w");
    FILE* f = std::fopen(argv[4], "w");
    std::vector<int> sizes;
    for (int i = 5; i < argc; ++i) sizes.push_back(std::atoi(argv[i]));
    const std::vector<double> epsilons = {0.01, 0.05, 0.10};

    std::fprintf(q, "qid,family,size,seed,target,nodes,edges,build_ms,"
                    "dij_ms,dij_expanded,dij_found,astar_ms,astar_expanded,astar_found,"
                    "martins_ms,martins_generated,martins_expanded,martins_front,martins_truncated,"
                    "namoa_ms,namoa_generated,namoa_expanded,namoa_filtered,namoa_front,namoa_equal,"
                    "eps01_ms,eps01_front,eps05_ms,eps05_front,eps10_ms,eps10_front\n");
    std::fprintf(f, "qid,method,time,fuel,risk,distance\n");
    int qid = 0;
    for (int size : sizes) {
        for (const auto& [name, fn] : families()) {
            for (int seed = 1; seed <= seeds; ++seed) {
                const Scenario sc = generateScenario(name, static_cast<Seed>(seed), fn(size));
                const TransferModel model;
                const auto b0 = std::chrono::steady_clock::now();
                const MissionGraph graph = GraphBuilder::build(sc, model).graph;
                const double buildMs = std::chrono::duration<double, std::milli>(
                                           std::chrono::steady_clock::now() - b0)
                                           .count();
                const EntityNodeIndex idx = buildEntityNodeIndex(graph);
                const auto& veh = sc.vehicles().front();
                const NodeId s = idx.vehicleNodes.at(veh.id().value());
                const int ns = std::min<int>(2, static_cast<int>(sc.satellites().size()));
                for (int si = 0; si < ns; ++si) {
                    const NodeId t = idx.satelliteNodes.at(
                        sc.satellites()[static_cast<std::size_t>(si)].id().value());
                    SearchConstraints c;
                    c.vehicleDeltaVBudgetKms = veh.deltaVBudgetKms();
                    c.maxWaitSec = 1.0e13;

                    auto timeIt = [&](const std::function<double()>& run) {
                        run();  // warm-up
                        std::vector<double> ts;
                        for (int r = 0; r < reps; ++r) ts.push_back(run());
                        return medianOf(ts);
                    };

                    PathResult d = findEarliestArrival(graph, model, s, t, 0.0, c);
                    const double dMs = timeIt([&] {
                        return findEarliestArrival(graph, model, s, t, 0.0, c).metrics().executionTimeMs;
                    });
                    PathResult a = findEarliestArrivalAStar(graph, model, s, t, 0.0, c);
                    const double aMs = timeIt([&] {
                        return findEarliestArrivalAStar(graph, model, s, t, 0.0, c).metrics().executionTimeMs;
                    });
                    const ParetoSearchResult m = findParetoRoutes(graph, model, s, t, 0.0, c);
                    const double mMs = timeIt([&] {
                        return findParetoRoutes(graph, model, s, t, 0.0, c).metrics.executionTimeMs;
                    });
                    const NamoaResult nm = findParetoRoutesNamoa(graph, model, s, t, 0.0, c);
                    const double nMs = timeIt([&] {
                        return findParetoRoutesNamoa(graph, model, s, t, 0.0, c).metrics.executionTimeMs;
                    });
                    const bool equal = sameFront(costsOf(m.solutions), costsOf(nm.solutions));

                    for (const auto& r : m.solutions) {
                        std::fprintf(f, "%d,exact,%.6f,%.9f,%.9f,%.6f\n", qid, r.totalCost.time,
                                     r.totalCost.fuel, r.totalCost.risk, r.totalCost.distance);
                    }
                    double epsMs[3];
                    std::size_t epsFront[3];
                    for (int k = 0; k < 3; ++k) {
                        ParetoSearchOptions opt;
                        opt.epsilonDominance = epsilons[static_cast<std::size_t>(k)];
                        const ParetoSearchResult e = findParetoRoutes(graph, model, s, t, 0.0, c, opt);
                        epsMs[k] = timeIt([&] {
                            return findParetoRoutes(graph, model, s, t, 0.0, c, opt).metrics.executionTimeMs;
                        });
                        epsFront[k] = e.solutions.size();
                        for (const auto& r : e.solutions) {
                            std::fprintf(f, "%d,eps%.2f,%.6f,%.9f,%.9f,%.6f\n", qid,
                                         epsilons[static_cast<std::size_t>(k)], r.totalCost.time,
                                         r.totalCost.fuel, r.totalCost.risk, r.totalCost.distance);
                        }
                    }
                    std::fprintf(q,
                                 "%d,%s,%d,%d,%d,%zu,%zu,%.4f,%.6f,%d,%d,%.6f,%d,%d,%.6f,%d,%d,%zu,%d,"
                                 "%.6f,%d,%d,%d,%zu,%d,%.6f,%zu,%.6f,%zu,%.6f,%zu\n",
                                 qid, name.c_str(), size, seed, si, graph.nodeCount(),
                                 graph.edgeCount(), buildMs, dMs, d.metrics().nodesExpanded,
                                 d.isFound() ? 1 : 0, aMs, a.metrics().nodesExpanded,
                                 a.isFound() ? 1 : 0, mMs, m.metrics.labelsGenerated,
                                 m.metrics.labelsExpanded, m.solutions.size(),
                                 m.metrics.truncated ? 1 : 0, nMs, nm.metrics.labelsGenerated,
                                 nm.metrics.labelsExpanded, nm.metrics.labelsFilteredBySolutions,
                                 nm.solutions.size(), equal ? 1 : 0, epsMs[0], epsFront[0],
                                 epsMs[1], epsFront[1], epsMs[2], epsFront[2]);
                    ++qid;
                }
                std::fflush(q);
                std::fflush(f);
                std::fprintf(stderr, "[e2] size=%d %s seed=%d (q=%d)\n", size, name.c_str(), seed, qid);
            }
        }
    }
    std::fclose(q);
    std::fclose(f);
}
