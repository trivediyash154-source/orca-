// E1: what single-objective search returns, and what the exact front contains.
// For each (family, size, seed) graph and up to 2 vehicles x 3 target satellites:
//   Dijkstra (earliest arrival, per-leg budget only), budget-pruned single-label Dijkstra,
//   and Martins' exact front with the cumulative budget enforced.
// Deadlines are applied afterwards in analysis: a deadline only removes plans that arrive
// late, and a plan's dominator always arrives no later, so the constrained front is the
// unconstrained front filtered by arrival time.
// Writes queries to argv[2] and front points to argv[3].
#include <cstdlib>

#include "common.hpp"

using namespace orca;
using namespace exp_common;

int main(int argc, char** argv) {
    const int seeds = std::atoi(argv[1]);
    FILE* q = std::fopen(argv[2], "w");
    FILE* f = std::fopen(argv[3], "w");
    std::vector<int> sizes = {25, 50, 100, 200};
    if (argc > 4) {
        sizes.clear();
        for (int i = 4; i < argc; ++i) sizes.push_back(std::atoi(argv[i]));
    }
    std::fprintf(q, "qid,family,size,seed,vehicle,target,nodes,edges,budget_kms,"
                    "dij_found,dij_arrival,dij_fuel,dij_legs,rc_found,rc_arrival,rc_fuel,"
                    "front_size,front_min_time,martins_ms,labels_generated,truncated\n");
    std::fprintf(f, "qid,time,fuel,risk,distance,legs\n");
    int qid = 0;
    for (int size : sizes) {
        for (const auto& [name, fn] : families()) {
            for (int seed = 1; seed <= seeds; ++seed) {
                const Scenario sc = generateScenario(name, static_cast<Seed>(seed), fn(size));
                const TransferModel model;
                const MissionGraph graph = GraphBuilder::build(sc, model).graph;
                const EntityNodeIndex idx = buildEntityNodeIndex(graph);
                const int nv = std::min<int>(2, static_cast<int>(sc.vehicles().size()));
                const int ns = std::min<int>(3, static_cast<int>(sc.satellites().size()));
                for (int vi = 0; vi < nv; ++vi) {
                    for (int si = 0; si < ns; ++si) {
                        const auto& veh = sc.vehicles()[static_cast<std::size_t>(vi)];
                        const NodeId s = idx.vehicleNodes.at(veh.id().value());
                        const NodeId t = idx.satelliteNodes.at(
                            sc.satellites()[static_cast<std::size_t>(si)].id().value());
                        SearchConstraints c;
                        c.vehicleDeltaVBudgetKms = veh.deltaVBudgetKms();
                        c.maxWaitSec = 1.0e13;
                        const PathResult d = findEarliestArrival(graph, model, s, t, 0.0, c);
                        const RcResult rc = budgetPrunedDijkstra(graph, model, s, t, 0.0, c);
                        const ParetoSearchResult p = findParetoRoutes(graph, model, s, t, 0.0, c);
                        double minTime = -1.0;
                        for (const auto& r : p.solutions) {
                            if (minTime < 0 || r.arrivalTimeSec < minTime) minTime = r.arrivalTimeSec;
                            std::fprintf(f, "%d,%.6f,%.9f,%.9f,%.6f,%zu\n", qid, r.totalCost.time,
                                         r.totalCost.fuel, r.totalCost.risk, r.totalCost.distance,
                                         r.legs.size());
                        }
                        std::fprintf(q,
                                     "%d,%s,%d,%d,%d,%d,%zu,%zu,%.6f,%d,%.6f,%.9f,%zu,%d,%.6f,%.9f,"
                                     "%zu,%.6f,%.6f,%d,%d\n",
                                     qid, name.c_str(), size, seed, vi, si, graph.nodeCount(),
                                     graph.edgeCount(), veh.deltaVBudgetKms(), d.isFound() ? 1 : 0,
                                     d.isFound() ? d.arrivalTimeSec() : -1.0,
                                     d.isFound() ? d.totalObjectives().fuel : -1.0,
                                     d.isFound() ? d.legs().size() : 0, rc.found ? 1 : 0,
                                     rc.found ? rc.arrival : -1.0, rc.found ? rc.cost.fuel : -1.0,
                                     p.solutions.size(), minTime, p.metrics.executionTimeMs,
                                     p.metrics.labelsGenerated, p.metrics.truncated ? 1 : 0);
                        ++qid;
                    }
                }
                std::fflush(q);
                std::fflush(f);
                std::fprintf(stderr, "[e1] size=%d %s seed=%d done (q=%d)\n", size, name.c_str(),
                             seed, qid);
            }
        }
    }
    std::fclose(q);
    std::fclose(f);
}
