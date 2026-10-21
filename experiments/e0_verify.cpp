// E0: correctness checks that the paper's claims rest on.
//   (a) Martins front == exhaustive enumeration of every flyable simple route (small graphs)
//   (b) NAMOA* front == Martins front (every instance used later in E2)
//   (c) A* arrival == Dijkstra arrival
// Output: one CSV row per query on stdout.
#include <cstdlib>

#include "common.hpp"

using namespace orca;
using namespace exp_common;

namespace {
struct Dfs {
    const MissionGraph& g;
    const TransferModel& m;
    TransferConstraints tc;
    NodeId target;
    long budget;
    long expansions = 0;
    bool aborted = false;
    std::vector<char> onPath;
    std::vector<ObjectiveVector> found;

    void run(NodeId u, double time, const ObjectiveVector& cost) {
        if (aborted) return;
        if (++expansions > budget) {
            aborted = true;
            return;
        }
        if (u == target) {
            found.push_back(cost);
            return;
        }
        onPath[u.value()] = 1;
        const Orbit& from = g.node(u).orbit();
        for (EdgeId eid : g.outgoingEdges(u)) {
            const GraphEdge& e = g.edge(eid);
            if (onPath[e.to().value()]) continue;
            const TransferResult r = m.evaluate(from, g.node(e.to()).orbit(), time, tc);
            if (!r.feasible) continue;
            const ObjectiveVector c2 = compose(cost, r.objectives());
            if (c2.fuel > 1.0 + kDominanceNoiseEpsilon) continue;
            run(e.to(), r.geometry.arrivalTimeSec, c2);
        }
        onPath[u.value()] = 0;
    }
};
}  // namespace

int main(int argc, char** argv) {
    const int seeds = argc > 1 ? std::atoi(argv[1]) : 30;
    const std::string only = argc > 2 ? argv[2] : "";
    const long bruteBudget = argc > 3 ? std::atol(argv[3]) : 5'000'000;
    std::printf("family,size,seed,vehicle,target,martins_size,namoa_size,namoa_equal,"
                "brute_status,brute_size,brute_equal,dijkstra_found,astar_found,astar_equal\n");
    for (const auto& [name, fn] : families()) {
        if (!only.empty() && name != only) continue;
        for (int size : {25, 50}) {
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
                        const auto mart = findParetoRoutes(graph, model, s, t, 0.0, c);
                        const auto nam = findParetoRoutesNamoa(graph, model, s, t, 0.0, c);
                        const auto mc = costsOf(mart.solutions);
                        const auto nc = costsOf(nam.solutions);
                        const bool neq = sameFront(mc, nc);

                        std::string bstatus = "skipped";
                        std::size_t bsize = 0;
                        int beq = -1;
                        if (size == 25) {
                            Dfs dfs{graph, model, toTransferConstraints(c), t, bruteBudget};
                            dfs.onPath.assign(graph.nodeCount(), 0);
                            dfs.run(s, 0.0, ObjectiveVector{});
                            if (dfs.aborted) {
                                bstatus = "budget";
                            } else {
                                bstatus = "complete";
                                const auto bf = nonDominated(dfs.found);
                                bsize = bf.size();
                                beq = sameFront(mc, bf) ? 1 : 0;
                            }
                        }
                        const PathResult d = findEarliestArrival(graph, model, s, t, 0.0, c);
                        const PathResult a = findEarliestArrivalAStar(graph, model, s, t, 0.0, c);
                        const int aeq = (d.isFound() == a.isFound()) &&
                                        (!d.isFound() ||
                                         std::fabs(d.arrivalTimeSec() - a.arrivalTimeSec()) < 1e-6);
                        std::printf("%s,%d,%d,%d,%d,%zu,%zu,%d,%s,%zu,%d,%d,%d,%d\n", name.c_str(),
                                    size, seed, vi, si, mc.size(), nc.size(), neq ? 1 : 0,
                                    bstatus.c_str(), bsize, beq, d.isFound() ? 1 : 0,
                                    a.isFound() ? 1 : 0, aeq);
                        std::fflush(stdout);
                    }
                }
            }
        }
    }
}
