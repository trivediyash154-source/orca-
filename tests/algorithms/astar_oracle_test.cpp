/// Independent brute-force verification of A*, and cross-validation against Dijkstra.
///
/// The oracle is the same kind dijkstra_oracle_test.cpp and
/// heuristic_admissibility_test.cpp use: enumerates every *simple* path from source to
/// target and evaluates each sequentially with the real `TransferModel`, sharing no logic
/// with either `Dijkstra.cpp` or `AStar.cpp` beyond that shared ground truth. Written fresh
/// here rather than shared across files, matching dijkstra_oracle_test.cpp's own stated
/// reasoning: that independence is what makes agreement meaningful.
///
/// This file explicitly does NOT validate the heuristic against A* itself (Phase 4 brief
/// §14) -- the heuristic's own correctness is established independently in
/// heuristic_admissibility_test.cpp and heuristic_consistency_test.cpp, using the oracle
/// and the real TransferModel, never A*. What this file checks is A* the *search
/// algorithm*: does it find what brute force proves is optimal, and does it agree with
/// Dijkstra -- a second, independently-implemented single-objective search solving the
/// identical problem.

#include <algorithm>
#include <vector>

#include "../../cpp/algorithms/AStar.hpp"
#include "../../cpp/algorithms/Dijkstra.hpp"
#include "../../cpp/graph/MissionGraph.hpp"
#include "../TestFramework.hpp"

using namespace orca;

namespace {

Orbit orbitAt(Kilometres altitudeKm, Radians phaseRad, Radians inclRad = 0.0) {
    return Orbit::fromAltitude(altitudeKm, inclRad, 0.0, phaseRad);
}

TransferConstraints permissive() {
    TransferConstraints c;
    c.maxDeltaVKms = 1.0e6;
    c.deltaVBudgetKms = 1.0e6;
    c.availableFuelFraction = 1.0;
    c.maxWaitSec = 1.0e13;
    c.latestArrivalSec = 1.0e18;
    return c;
}

SearchConstraints permissiveOptions() {
    SearchConstraints o;
    o.vehicleDeltaVBudgetKms = 1.0e6;
    o.maxWaitSec = 1.0e13;
    o.latestArrivalSec = 1.0e18;
    return o;
}

struct OracleResult {
    bool found = false;
    Seconds arrivalTimeSec = 0.0;
    std::vector<NodeId> path;
};

void enumerateSimplePaths(const MissionGraph& graph, const TransferModel& model,
                          const TransferConstraints& constraints, NodeId current,
                          NodeId target, Seconds arrivedAt, std::vector<NodeId>& visited,
                          std::vector<NodeId>& path, OracleResult& best) {
    path.push_back(current);
    if (current == target) {
        if (!best.found || arrivedAt < best.arrivalTimeSec) {
            best.found = true;
            best.arrivalTimeSec = arrivedAt;
            best.path = path;
        }
        path.pop_back();
        return;
    }
    for (const EdgeId edgeId : graph.outgoingEdges(current)) {
        const GraphEdge& edge = graph.edge(edgeId);
        if (std::find(visited.begin(), visited.end(), edge.to()) != visited.end()) {
            continue;
        }
        const TransferResult result = model.evaluate(
            graph.node(current).orbit(), graph.node(edge.to()).orbit(), arrivedAt,
            constraints);
        if (!result.feasible) {
            continue;
        }
        visited.push_back(edge.to());
        enumerateSimplePaths(graph, model, constraints, edge.to(), target,
                             result.geometry.arrivalTimeSec, visited, path, best);
        visited.pop_back();
    }
    path.pop_back();
}

[[nodiscard]] OracleResult bruteForceEarliestArrival(const MissionGraph& graph,
                                                     const TransferModel& model,
                                                     const TransferConstraints& constraints,
                                                     NodeId source, NodeId target,
                                                     Seconds startTimeSec) {
    OracleResult best;
    std::vector<NodeId> visited{source};
    std::vector<NodeId> path;
    enumerateSimplePaths(graph, model, constraints, source, target, startTimeSec, visited,
                        path, best);
    return best;
}

}  // namespace

// ---------------------------------------------------------------------------
// A* vs brute force: the same 7-node directed graph with a cycle from
// dijkstra_oracle_test.cpp, re-checked here against A* rather than Dijkstra.
// ---------------------------------------------------------------------------

ORCA_TEST(AStarOracle, AgreesWithBruteForceOnDirectedGraphWithACycle) {
    MissionGraph g;
    const NodeId n0 = g.addNode(GraphNode{NodeId{0}, SatelliteId{0}, orbitAt(500.0, 0.0)});
    const NodeId n1 =
        g.addNode(GraphNode{NodeId{0}, SatelliteId{1}, orbitAt(620.0, 0.8, 0.1)});
    const NodeId n2 =
        g.addNode(GraphNode{NodeId{0}, SatelliteId{2}, orbitAt(710.0, 2.1, 0.05)});
    const NodeId n3 =
        g.addNode(GraphNode{NodeId{0}, SatelliteId{3}, orbitAt(900.0, 3.5, 0.2)});
    const NodeId n4 =
        g.addNode(GraphNode{NodeId{0}, SatelliteId{4}, orbitAt(1200.0, 1.2, 0.15)});
    const NodeId n5 =
        g.addNode(GraphNode{NodeId{0}, SatelliteId{5}, orbitAt(1500.0, 4.4, 0.3)});

    const TransferModel model;
    const TransferConstraints c = permissive();
    const auto add = [&](NodeId u, NodeId v) {
        g.addEdge(u, v, model.evaluate(g.node(u).orbit(), g.node(v).orbit(), 0.0, c), 0.0);
    };
    add(n0, n1);
    add(n0, n2);
    add(n1, n3);
    add(n2, n3);
    add(n1, n4);
    add(n3, n5);
    add(n4, n5);
    add(n2, n5);
    add(n3, n1);  // cycle

    const PathResult astar =
        findEarliestArrivalAStar(g, model, n0, n5, 0.0, permissiveOptions());
    const OracleResult oracle = bruteForceEarliestArrival(g, model, c, n0, n5, 0.0);

    CHECK(astar.isFound());
    CHECK(oracle.found);
    CHECK_NEAR(astar.arrivalTimeSec(), oracle.arrivalTimeSec, 1e-6);

    const std::vector<NodeId> astarPath = astar.nodeSequence();
    CHECK_EQ(astarPath.size(), oracle.path.size());
    for (std::size_t i = 0; i < astarPath.size() && i < oracle.path.size(); ++i) {
        CHECK(astarPath[i] == oracle.path[i]);
    }
}

ORCA_TEST(AStarOracle, AgreesOnUnreachableTarget) {
    MissionGraph g;
    const NodeId n0 = g.addNode(GraphNode{NodeId{0}, SatelliteId{0}, orbitAt(500.0, 0.0)});
    const NodeId n1 =
        g.addNode(GraphNode{NodeId{0}, SatelliteId{1}, orbitAt(620.0, 0.8, 0.1)});
    const NodeId isolated =
        g.addNode(GraphNode{NodeId{0}, SatelliteId{2}, orbitAt(2200.0, 0.0)});

    const TransferModel model;
    const TransferConstraints c = permissive();
    g.addEdge(n0, n1, model.evaluate(g.node(n0).orbit(), g.node(n1).orbit(), 0.0, c), 0.0);

    const PathResult astar =
        findEarliestArrivalAStar(g, model, n0, isolated, 0.0, permissiveOptions());
    const OracleResult oracle = bruteForceEarliestArrival(g, model, c, n0, isolated, 0.0);

    CHECK(!astar.isFound());
    CHECK(!oracle.found);
}

ORCA_TEST(AStarOracle, AgreesOnIndirectRouteBeatingDirect) {
    MissionGraph g;
    const Orbit orbitA = orbitAt(500.0, 0.0);
    const Orbit orbitB = orbitAt(700.0, 1.0);
    const Orbit orbitC = orbitAt(505.0, 3.0);
    const NodeId a = g.addNode(GraphNode{NodeId{0}, SatelliteId{0}, orbitA});
    const NodeId b = g.addNode(GraphNode{NodeId{0}, SatelliteId{1}, orbitB});
    const NodeId cNode = g.addNode(GraphNode{NodeId{0}, SatelliteId{2}, orbitC});

    const TransferModel model;
    const TransferConstraints c = permissive();
    g.addEdge(a, b, model.evaluate(orbitA, orbitB, 0.0, c), 0.0);
    g.addEdge(b, cNode, model.evaluate(orbitB, orbitC, 0.0, c), 0.0);
    g.addEdge(a, cNode, model.evaluate(orbitA, orbitC, 0.0, c), 0.0);

    const PathResult astar =
        findEarliestArrivalAStar(g, model, a, cNode, 0.0, permissiveOptions());
    const OracleResult oracle = bruteForceEarliestArrival(g, model, c, a, cNode, 0.0);

    CHECK(astar.isFound());
    CHECK(oracle.found);
    CHECK_NEAR(astar.arrivalTimeSec(), oracle.arrivalTimeSec, 1e-6);
    CHECK_EQ(oracle.path.size(), std::size_t{3});
}

ORCA_TEST(AStarOracle, AgreesOnRandomisedSmallGraphs) {
    const TransferModel model;
    const TransferConstraints c = permissive();

    struct Case {
        std::vector<double> altitudesKm;
        std::vector<double> phasesRad;
        std::vector<std::pair<int, int>> edges;
    };

    const Case cases[] = {
        {{500, 600, 700, 800}, {0.0, 0.3, 0.9, 1.7}, {{0, 1}, {1, 2}, {2, 3}}},
        {{500, 650, 900, 1400}, {0.0, 1.1, 2.6, 0.4}, {{0, 1}, {0, 2}, {1, 3}, {2, 3}}},
        {{500, 700, 1000, 1300, 1600},
         {0.0, 0.5, 1.0, 1.5, 2.0},
         {{0, 1}, {0, 2}, {0, 3}, {3, 4}}},
        {{500, 800, 1100, 1600}, {0.0, 2.2, 0.7, 3.9}, {{0, 1}, {1, 2}, {2, 3}, {2, 1}}},
    };

    int casesChecked = 0;
    for (const Case& tc : cases) {
        MissionGraph g;
        std::vector<NodeId> nodes;
        for (std::size_t i = 0; i < tc.altitudesKm.size(); ++i) {
            nodes.push_back(g.addNode(GraphNode{
                NodeId{0}, SatelliteId{static_cast<std::uint32_t>(i)},
                orbitAt(tc.altitudesKm[i], tc.phasesRad[i])}));
        }
        for (const auto& [ui, vi] : tc.edges) {
            const NodeId u = nodes[static_cast<std::size_t>(ui)];
            const NodeId v = nodes[static_cast<std::size_t>(vi)];
            const TransferResult r =
                model.evaluate(g.node(u).orbit(), g.node(v).orbit(), 0.0, c);
            if (r.feasible) {
                g.addEdge(u, v, r, 0.0);
            }
        }

        const NodeId source = nodes.front();
        const NodeId target = nodes.back();
        const PathResult astar =
            findEarliestArrivalAStar(g, model, source, target, 0.0, permissiveOptions());
        const OracleResult oracle =
            bruteForceEarliestArrival(g, model, c, source, target, 0.0);

        CHECK_EQ(astar.isFound(), oracle.found);
        if (astar.isFound() && oracle.found) {
            CHECK_NEAR(astar.arrivalTimeSec(), oracle.arrivalTimeSec, 1e-6);
        }
        ++casesChecked;
    }

    CHECK_EQ(casesChecked, 4);
}

// ---------------------------------------------------------------------------
// Dijkstra vs A*: both solve the identical single-objective problem
// (findEarliestArrival's file comment), so on any graph both must agree on found status,
// arrival time and total objectives. They are NOT required to return the same *path* when
// multiple routes tie at the optimal arrival time (PathResult.hpp does not specify a
// canonical choice among ties) -- so path equality is checked only when the graph's
// structure makes the optimal route unique.
// ---------------------------------------------------------------------------

ORCA_TEST(AStarOracle, DijkstraAndAStarAgreeOnRandomisedSmallGraphs) {
    const TransferModel model;
    const TransferConstraints c = permissive();

    struct Case {
        std::vector<double> altitudesKm;
        std::vector<double> phasesRad;
        std::vector<std::pair<int, int>> edges;
    };

    const Case cases[] = {
        {{500, 600, 700, 800}, {0.0, 0.3, 0.9, 1.7}, {{0, 1}, {1, 2}, {2, 3}}},
        {{500, 650, 900, 1400}, {0.0, 1.1, 2.6, 0.4}, {{0, 1}, {0, 2}, {1, 3}, {2, 3}}},
        {{500, 700, 1000, 1300, 1600},
         {0.0, 0.5, 1.0, 1.5, 2.0},
         {{0, 1}, {0, 2}, {0, 3}, {3, 4}}},
        {{500, 800, 1100, 1600}, {0.0, 2.2, 0.7, 3.9}, {{0, 1}, {1, 2}, {2, 3}, {2, 1}}},
        {{500, 620, 710, 900, 1200, 1500},
         {0.0, 0.8, 2.1, 3.5, 1.2, 4.4},
         {{0, 1}, {0, 2}, {1, 3}, {2, 3}, {1, 4}, {3, 5}, {4, 5}, {2, 5}, {3, 1}}},
    };

    int casesChecked = 0;
    for (const Case& tc : cases) {
        MissionGraph g;
        std::vector<NodeId> nodes;
        for (std::size_t i = 0; i < tc.altitudesKm.size(); ++i) {
            nodes.push_back(g.addNode(GraphNode{
                NodeId{0}, SatelliteId{static_cast<std::uint32_t>(i)},
                orbitAt(tc.altitudesKm[i], tc.phasesRad[i])}));
        }
        for (const auto& [ui, vi] : tc.edges) {
            const NodeId u = nodes[static_cast<std::size_t>(ui)];
            const NodeId v = nodes[static_cast<std::size_t>(vi)];
            const TransferResult r =
                model.evaluate(g.node(u).orbit(), g.node(v).orbit(), 0.0, c);
            if (r.feasible) {
                g.addEdge(u, v, r, 0.0);
            }
        }

        const NodeId source = nodes.front();
        const NodeId target = nodes.back();
        const PathResult dijkstra =
            findEarliestArrival(g, model, source, target, 0.0, permissiveOptions());
        const PathResult astar =
            findEarliestArrivalAStar(g, model, source, target, 0.0, permissiveOptions());

        CHECK_EQ(dijkstra.isFound(), astar.isFound());
        if (dijkstra.isFound() && astar.isFound()) {
            CHECK_NEAR(dijkstra.arrivalTimeSec(), astar.arrivalTimeSec(), 1e-6);
            CHECK_NEAR(dijkstra.totalObjectives().time, astar.totalObjectives().time, 1e-6);
            CHECK_NEAR(dijkstra.totalObjectives().fuel, astar.totalObjectives().fuel, 1e-6);
            CHECK_NEAR(dijkstra.totalObjectives().risk, astar.totalObjectives().risk, 1e-6);
        }
        ++casesChecked;
    }

    CHECK_EQ(casesChecked, 5);
}
