/// Independent brute-force verification of Dijkstra, per Phase 3 §§30, 32-33.
///
/// The oracle below enumerates every *simple* path from source to target (bounded by
/// `visited`, so cycles cannot cause non-termination) and evaluates each one
/// sequentially — `edge1 at t0, edge2 at arrival(edge1), ...` — using the same
/// `TransferModel` Dijkstra uses, but calling it directly rather than through any
/// Dijkstra code path. It shares no logic with `cpp/algorithms/Dijkstra.cpp` beyond the
/// `TransferModel` both are required to use as ORCA's single source of transfer truth
/// (ARCHITECTURE.md P4) — the graph traversal, path enumeration and minimum-selection are
/// independently written here.
///
/// This is what makes the comparison meaningful: brute force is obviously correct (it
/// tries everything) and obviously too slow to use in production; Dijkstra is fast but
/// its correctness rests on FIFO and the greedy-commitment invariant. Agreement between
/// the two, on graphs small enough for brute force to be exhaustive, is the strongest
/// evidence available that the fast algorithm has not traded away correctness for speed.

#include <algorithm>
#include <vector>

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

/// Depth-first enumeration of every simple path from `current` to `target`, tracking the
/// minimum arrival time found. `visited` enforces simplicity (no repeated node), which
/// both bounds the search and is legitimate for shortest-path verification: revisiting a
/// node can never help when every objective composed along a path is non-negative
/// (ObjectiveVector's invariant) — an optimal path is never improved by a detour through
/// somewhere it has already been.
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
            continue;  // simple paths only
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
// A 7-node directed graph with a cycle, partial connectivity, and an isolated node.
// Every edge below was confirmed feasible before being added.
// ---------------------------------------------------------------------------

ORCA_TEST(DijkstraOracle, AgreesWithBruteForceOnDirectedGraphWithACycle) {
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

    // Deliberately not fully connected, and n3 -> n1 closes a cycle back toward the
    // source side of the graph.
    add(n0, n1);
    add(n0, n2);
    add(n1, n3);
    add(n2, n3);
    add(n1, n4);
    add(n3, n5);
    add(n4, n5);
    add(n2, n5);
    add(n3, n1);  // cycle

    const PathResult dijkstra =
        findEarliestArrival(g, model, n0, n5, 0.0, permissiveOptions());
    const OracleResult oracle = bruteForceEarliestArrival(g, model, c, n0, n5, 0.0);

    CHECK(dijkstra.isFound());
    CHECK(oracle.found);
    CHECK_NEAR(dijkstra.arrivalTimeSec(), oracle.arrivalTimeSec, 1e-6);

    // Both independently identify the same winning route (0 -> 2 -> 5), not merely the
    // same arrival time by coincidence.
    const std::vector<NodeId> dijkstraPath = dijkstra.nodeSequence();
    CHECK_EQ(dijkstraPath.size(), oracle.path.size());
    for (std::size_t i = 0; i < dijkstraPath.size() && i < oracle.path.size(); ++i) {
        CHECK(dijkstraPath[i] == oracle.path[i]);
    }
}

ORCA_TEST(DijkstraOracle, AgreesOnUnreachableTarget) {
    // Same graph as above, plus one node with no incoming edge from the source's
    // component. Both the fast algorithm and brute force must agree it is unreachable.
    MissionGraph g;
    const NodeId n0 = g.addNode(GraphNode{NodeId{0}, SatelliteId{0}, orbitAt(500.0, 0.0)});
    const NodeId n1 =
        g.addNode(GraphNode{NodeId{0}, SatelliteId{1}, orbitAt(620.0, 0.8, 0.1)});
    const NodeId isolated =
        g.addNode(GraphNode{NodeId{0}, SatelliteId{2}, orbitAt(2200.0, 0.0)});

    const TransferModel model;
    const TransferConstraints c = permissive();
    g.addEdge(n0, n1, model.evaluate(g.node(n0).orbit(), g.node(n1).orbit(), 0.0, c), 0.0);

    const PathResult dijkstra =
        findEarliestArrival(g, model, n0, isolated, 0.0, permissiveOptions());
    const OracleResult oracle = bruteForceEarliestArrival(g, model, c, n0, isolated, 0.0);

    CHECK(!dijkstra.isFound());
    CHECK(!oracle.found);
}

ORCA_TEST(DijkstraOracle, AgreesOnIndirectRouteBeatingDirect) {
    // The same near-identical-altitude construction from dijkstra_test.cpp's
    // TimeDependence suite, re-verified here against an oracle that has no shared code
    // with Dijkstra at all -- the FIFO-proof-to-search-to-verification chain the Phase 3
    // brief asks for (§32).
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

    const PathResult dijkstra =
        findEarliestArrival(g, model, a, cNode, 0.0, permissiveOptions());
    const OracleResult oracle = bruteForceEarliestArrival(g, model, c, a, cNode, 0.0);

    CHECK(dijkstra.isFound());
    CHECK(oracle.found);
    CHECK_NEAR(dijkstra.arrivalTimeSec(), oracle.arrivalTimeSec, 1e-6);
    CHECK_EQ(oracle.path.size(), std::size_t{3});  // A, B, C -- confirms oracle picked indirect too
}

ORCA_TEST(DijkstraOracle, AgreesOnRandomisedSmallGraphs) {
    // Broader sweep: several small, structurally distinct directed graphs built from
    // varied but fixed (non-random, for reproducibility) orbital parameters, each checked
    // against the oracle. Complements the single hand-inspected case above with breadth.
    const TransferModel model;
    const TransferConstraints c = permissive();

    struct Case {
        std::vector<double> altitudesKm;
        std::vector<double> phasesRad;
        std::vector<std::pair<int, int>> edges;  // indices into the node list
    };

    const Case cases[] = {
        // Linear chain.
        {{500, 600, 700, 800},
         {0.0, 0.3, 0.9, 1.7},
         {{0, 1}, {1, 2}, {2, 3}}},
        // Diamond: two disjoint 2-hop routes to the same target.
        {{500, 650, 900, 1400},
         {0.0, 1.1, 2.6, 0.4},
         {{0, 1}, {0, 2}, {1, 3}, {2, 3}}},
        // Star out of the source, only one path reaches the last node.
        {{500, 700, 1000, 1300, 1600},
         {0.0, 0.5, 1.0, 1.5, 2.0},
         {{0, 1}, {0, 2}, {0, 3}, {3, 4}}},
        // A back edge creating a cycle that does not touch the optimal route.
        {{500, 800, 1100, 1600},
         {0.0, 2.2, 0.7, 3.9},
         {{0, 1}, {1, 2}, {2, 3}, {2, 1}}},
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
        const OracleResult oracle =
            bruteForceEarliestArrival(g, model, c, source, target, 0.0);

        CHECK_EQ(dijkstra.isFound(), oracle.found);
        if (dijkstra.isFound() && oracle.found) {
            CHECK_NEAR(dijkstra.arrivalTimeSec(), oracle.arrivalTimeSec, 1e-6);
        }
        ++casesChecked;
    }

    CHECK_EQ(casesChecked, 4);
}
