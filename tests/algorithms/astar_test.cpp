/// Tests for earliest-arrival A*: basic structural correctness -- trivial paths, direct
/// edges, unreachability, directedness, tie-breaking, and search metrics (including
/// `heuristicEvaluations`, the field that distinguishes A*'s metrics from Dijkstra's).
///
/// Deliberately mirrors dijkstra_test.cpp's structure and scenarios test-for-test, since
/// both algorithms are required to solve the *same* problem (earliest arrival time,
/// PathResult.hpp) and should agree on every one of these basic properties. Where a
/// scenario is identical to one in dijkstra_test.cpp, that is intentional, not
/// duplication-by-accident: the point is that A* is a drop-in alternative for this problem,
/// not a different one. Time-dependence, reopening and the h=0 ablation have their own
/// file, astar_time_dependence_test.cpp; independent brute-force verification and
/// Dijkstra-vs-A* cross-validation live in astar_oracle_test.cpp.

#include <cmath>

#include "../../cpp/algorithms/AStar.hpp"
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

}  // namespace

// ---------------------------------------------------------------------------
// Basic correctness
// ---------------------------------------------------------------------------

ORCA_TEST(AStar, TrivialPathWhenSourceEqualsTarget) {
    MissionGraph g;
    const NodeId a = g.addNode(GraphNode{NodeId{0}, SatelliteId{0}, orbitAt(500.0, 0.0)});
    const TransferModel model;

    const PathResult result =
        findEarliestArrivalAStar(g, model, a, a, 412.0, permissiveOptions());
    CHECK(result.isFound());
    CHECK_EQ(result.legs().size(), std::size_t{0});
    CHECK_NEAR(result.arrivalTimeSec(), 412.0, 1e-12);
    CHECK(isValid(result.totalObjectives()));
    CHECK_NEAR(result.totalObjectives().time, 0.0, 1e-12);
    // The trivial case short-circuits before any heuristic evaluation, node expansion or
    // queue operation happens (AStar.cpp: `source == target` returns before the loop).
    CHECK_EQ(result.metrics().heuristicEvaluations, 0);
    CHECK_EQ(result.metrics().nodesExpanded, 0);
}

ORCA_TEST(AStar, DirectSingleEdge) {
    MissionGraph g;
    const NodeId a = g.addNode(GraphNode{NodeId{0}, SatelliteId{0}, orbitAt(500.0, 0.0)});
    const NodeId b = g.addNode(GraphNode{NodeId{0}, SatelliteId{1}, orbitAt(800.0, 1.0)});
    const TransferModel model;
    const TransferConstraints c = permissive();
    g.addEdge(a, b, model.evaluate(orbitAt(500.0, 0.0), orbitAt(800.0, 1.0), 0.0, c), 0.0);

    const PathResult result =
        findEarliestArrivalAStar(g, model, a, b, 0.0, permissiveOptions());
    CHECK(result.isFound());
    CHECK_EQ(result.legs().size(), std::size_t{1});
    CHECK(result.legs()[0].from == a);
    CHECK(result.legs()[0].to == b);
    CHECK(result.nodeSequence().size() == 2);
}

ORCA_TEST(AStar, UnreachableTargetNotFound) {
    MissionGraph g;
    const NodeId a = g.addNode(GraphNode{NodeId{0}, SatelliteId{0}, orbitAt(500.0, 0.0)});
    const NodeId isolated =
        g.addNode(GraphNode{NodeId{0}, SatelliteId{1}, orbitAt(2000.0, 0.0)});
    const TransferModel model;

    const PathResult result =
        findEarliestArrivalAStar(g, model, a, isolated, 0.0, permissiveOptions());
    CHECK(!result.isFound());
    CHECK_THROWS(result.arrivalTimeSec());
    CHECK_THROWS(result.totalObjectives());
    CHECK_THROWS(result.nodeSequence());
}

ORCA_TEST(AStar, DisconnectedComponentIsUnreachable) {
    MissionGraph g;
    const NodeId a = g.addNode(GraphNode{NodeId{0}, SatelliteId{0}, orbitAt(500.0, 0.0)});
    const NodeId b = g.addNode(GraphNode{NodeId{0}, SatelliteId{1}, orbitAt(600.0, 0.5)});
    const NodeId x = g.addNode(GraphNode{NodeId{0}, SatelliteId{2}, orbitAt(2000.0, 0.0)});
    const NodeId y = g.addNode(GraphNode{NodeId{0}, SatelliteId{3}, orbitAt(2500.0, 0.5)});
    const TransferModel model;
    const TransferConstraints c = permissive();
    g.addEdge(a, b, model.evaluate(orbitAt(500.0, 0.0), orbitAt(600.0, 0.5), 0.0, c), 0.0);
    g.addEdge(x, y, model.evaluate(orbitAt(2000.0, 0.0), orbitAt(2500.0, 0.5), 0.0, c), 0.0);

    CHECK(!findEarliestArrivalAStar(g, model, a, x, 0.0, permissiveOptions()).isFound());
    CHECK(!findEarliestArrivalAStar(g, model, a, y, 0.0, permissiveOptions()).isFound());
    CHECK(findEarliestArrivalAStar(g, model, a, b, 0.0, permissiveOptions()).isFound());
}

ORCA_TEST(AStar, EdgeDirectionIsRespected) {
    // A -> B exists; B -> A does not. Searching B -> A must fail even though A -> B
    // succeeds -- proves the heuristic guiding expansion order never makes the search
    // treat edges as bidirectional.
    MissionGraph g;
    const NodeId a = g.addNode(GraphNode{NodeId{0}, SatelliteId{0}, orbitAt(500.0, 0.0)});
    const NodeId b = g.addNode(GraphNode{NodeId{0}, SatelliteId{1}, orbitAt(800.0, 1.0)});
    const TransferModel model;
    const TransferConstraints c = permissive();
    g.addEdge(a, b, model.evaluate(orbitAt(500.0, 0.0), orbitAt(800.0, 1.0), 0.0, c), 0.0);

    CHECK(findEarliestArrivalAStar(g, model, a, b, 0.0, permissiveOptions()).isFound());
    CHECK(!findEarliestArrivalAStar(g, model, b, a, 0.0, permissiveOptions()).isFound());
}

ORCA_TEST(AStar, RejectsUnknownSourceOrTarget) {
    MissionGraph g;
    const NodeId a = g.addNode(GraphNode{NodeId{0}, SatelliteId{0}, orbitAt(500.0, 0.0)});
    const TransferModel model;
    CHECK_THROWS(
        findEarliestArrivalAStar(g, model, NodeId{99}, a, 0.0, permissiveOptions()));
    CHECK_THROWS(
        findEarliestArrivalAStar(g, model, a, NodeId{99}, 0.0, permissiveOptions()));
}

ORCA_TEST(AStar, EqualCostParallelPathsBothReachTarget) {
    // Same construction as DijkstraTest's equivalent: two same-altitude, same-phase
    // intermediate satellites (differing only in inclination, which the time heuristic
    // ignores by design) offer two parallel, numerically identical-cost routes. Which
    // specific route A* returns is unspecified; that it is FOUND at the correct tied time
    // is what this test asserts.
    MissionGraph g;
    const NodeId a = g.addNode(GraphNode{NodeId{0}, SatelliteId{0}, orbitAt(500.0, 0.0)});
    const NodeId b1 =
        g.addNode(GraphNode{NodeId{0}, SatelliteId{1}, orbitAt(500.0, 0.0, 0.1)});
    const NodeId b2 =
        g.addNode(GraphNode{NodeId{0}, SatelliteId{2}, orbitAt(500.0, 0.0, 0.2)});
    const NodeId d =
        g.addNode(GraphNode{NodeId{0}, SatelliteId{3}, orbitAt(500.0, 0.0, 0.3)});

    const TransferModel model;
    const TransferConstraints c = permissive();
    const auto ev = [&](NodeId u, NodeId v) {
        return model.evaluate(g.node(u).orbit(), g.node(v).orbit(), 0.0, c);
    };
    g.addEdge(a, b1, ev(a, b1), 0.0);
    g.addEdge(a, b2, ev(a, b2), 0.0);
    g.addEdge(b1, d, ev(b1, d), 0.0);
    g.addEdge(b2, d, ev(b2, d), 0.0);

    const PathResult result =
        findEarliestArrivalAStar(g, model, a, d, 0.0, permissiveOptions());
    CHECK(result.isFound());
    CHECK_NEAR(result.arrivalTimeSec(), 0.0, 1e-9);
    CHECK_EQ(result.legs().size(), std::size_t{2});
}

// ---------------------------------------------------------------------------
// Search metrics, including heuristicEvaluations -- the field with no Dijkstra analogue.
// ---------------------------------------------------------------------------

ORCA_TEST(AStar, MetricsAreInternallyConsistent) {
    MissionGraph g;
    const NodeId a = g.addNode(GraphNode{NodeId{0}, SatelliteId{0}, orbitAt(500.0, 0.0)});
    const NodeId b = g.addNode(GraphNode{NodeId{0}, SatelliteId{1}, orbitAt(800.0, 1.0)});
    const NodeId cNode =
        g.addNode(GraphNode{NodeId{0}, SatelliteId{2}, orbitAt(1200.0, 2.0)});
    const TransferModel model;
    const TransferConstraints c = permissive();
    g.addEdge(a, b, model.evaluate(orbitAt(500.0, 0.0), orbitAt(800.0, 1.0), 0.0, c), 0.0);
    g.addEdge(b, cNode, model.evaluate(orbitAt(800.0, 1.0), orbitAt(1200.0, 2.0), 0.0, c),
             0.0);

    const PathResult result =
        findEarliestArrivalAStar(g, model, a, cNode, 0.0, permissiveOptions());
    const SearchMetrics& m = result.metrics();

    CHECK(result.isFound());
    CHECK(m.nodesExpanded >= 1);
    // Unlike Dijkstra, "nodesExpanded <= nodeCount" is NOT a general A* invariant --
    // reopening (AStar.hpp) can expand one node more than once. It holds *here* only
    // because this fixture's realistic, close-altitude orbits give a consistent heuristic
    // (heuristic_consistency_test.cpp), so reopening never triggers on this graph.
    // AStarTimeDependence.ReopeningCorrectsWronglyExpandedNode... (astar_time_dependence_
    // test.cpp) is the test that deliberately forces the opposite.
    CHECK(m.nodesExpanded <= static_cast<int>(g.nodeCount()));
    CHECK(m.edgesEvaluated >= 1);
    CHECK(m.priorityQueuePushes >= m.priorityQueuePops);
    CHECK(m.relaxationsSucceeded >= 1);
    CHECK(m.executionTimeMs >= 0.0);
    // One heuristic evaluation for the source before the loop, plus one per successful
    // relaxation (AStar.cpp) -- at minimum, source + the two successful relaxations here.
    CHECK(m.heuristicEvaluations >= 3);
}

ORCA_TEST(AStar, MetricsPopulatedEvenWhenNotFound) {
    MissionGraph g;
    const NodeId a = g.addNode(GraphNode{NodeId{0}, SatelliteId{0}, orbitAt(500.0, 0.0)});
    const NodeId isolated =
        g.addNode(GraphNode{NodeId{0}, SatelliteId{1}, orbitAt(2000.0, 0.0)});
    const TransferModel model;

    const PathResult result =
        findEarliestArrivalAStar(g, model, a, isolated, 0.0, permissiveOptions());
    CHECK(!result.isFound());
    CHECK(result.metrics().nodesExpanded >= 1);  // source itself is still expanded
    CHECK(result.metrics().heuristicEvaluations >= 1);  // source's h, at minimum
    CHECK(result.metrics().executionTimeMs >= 0.0);
}

ORCA_TEST(AStar, HeuristicEvaluationsIsAlwaysPositiveUnlikeDijkstra) {
    // The metric that has no Dijkstra analogue: SearchMetrics.hpp documents
    // heuristicEvaluations as "always 0 for Dijkstra" -- for A*, it must be positive for
    // any non-trivial search, even one that fails to find the target, since the source's
    // heuristic is evaluated unconditionally before the main loop starts.
    MissionGraph g;
    const NodeId a = g.addNode(GraphNode{NodeId{0}, SatelliteId{0}, orbitAt(500.0, 0.0)});
    const NodeId b = g.addNode(GraphNode{NodeId{0}, SatelliteId{1}, orbitAt(900.0, 1.0)});
    const TransferModel model;
    const TransferConstraints c = permissive();
    g.addEdge(a, b, model.evaluate(orbitAt(500.0, 0.0), orbitAt(900.0, 1.0), 0.0, c), 0.0);

    const PathResult result =
        findEarliestArrivalAStar(g, model, a, b, 0.0, permissiveOptions());
    CHECK(result.isFound());
    CHECK(result.metrics().heuristicEvaluations > 0);
}

// ---------------------------------------------------------------------------
// The documented, tested boundary shared with Dijkstra: per-edge delta-v feasibility is
// enforced; a cumulative path-wide budget is not (Dijkstra.hpp's file comment explains
// why single-label search cannot soundly enforce it -- the same argument applies to A*
// unchanged, since both build the identical per-edge TransferConstraints).
// ---------------------------------------------------------------------------

ORCA_TEST(AStar, CumulativeBudgetIsNotEnforcedByDesign) {
    // Identical scenario to DijkstraTest's equivalent (dijkstra_time_dependence_test.cpp):
    // two 10-degree plane changes at 500 km, each individually within a 2.0 km/s budget
    // (1.326965039 km/s) but not together (2.653930 km/s > 2.0).
    MissionGraph g;
    const Orbit orbitA = orbitAt(500.0, 0.0, 0.0);
    const Orbit orbitB = orbitAt(500.0, 0.0, degToRad(10.0));
    const Orbit orbitC = orbitAt(500.0, 0.0, degToRad(20.0));
    const NodeId a = g.addNode(GraphNode{NodeId{0}, SatelliteId{0}, orbitA});
    const NodeId b = g.addNode(GraphNode{NodeId{0}, SatelliteId{1}, orbitB});
    const NodeId cNode = g.addNode(GraphNode{NodeId{0}, SatelliteId{2}, orbitC});

    const TransferModel model;
    const TransferConstraints buildC = permissive();
    g.addEdge(a, b, model.evaluate(orbitA, orbitB, 0.0, buildC), 0.0);
    g.addEdge(b, cNode, model.evaluate(orbitB, orbitC, 0.0, buildC), 0.0);

    SearchConstraints options;
    options.vehicleDeltaVBudgetKms = 2.0;
    options.maxWaitSec = 1.0e13;
    options.latestArrivalSec = 1.0e18;

    const PathResult result = findEarliestArrivalAStar(g, model, a, cNode, 0.0, options);

    CHECK(result.isFound());
    CHECK_NEAR(result.legs()[0].result.geometry.deltaVKms, 1.326965039, 1e-6);
    CHECK_NEAR(result.legs()[1].result.geometry.deltaVKms, 1.326965039, 1e-6);
    CHECK(result.totalObjectives().fuel > 1.0);
}
