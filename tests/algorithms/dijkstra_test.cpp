/// Tests for earliest-arrival Dijkstra: basic structural correctness — trivial paths,
/// direct edges, unreachability, directedness, tie-breaking, and search metrics.
///
/// The two headline properties this phase exists to establish — the "critical rule" that
/// edges are evaluated at the predecessor's arrival time, and the documented
/// cumulative-budget limitation — have their own file, dijkstra_time_dependence_test.cpp,
/// since they are the actual point of Phase 3 and deserve to be found on their own rather
/// than mixed in with routine structural checks. Independent brute-force verification
/// lives in dijkstra_oracle_test.cpp.

#include <cmath>

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

}  // namespace

// ---------------------------------------------------------------------------
// Basic correctness
// ---------------------------------------------------------------------------

ORCA_TEST(Dijkstra, TrivialPathWhenSourceEqualsTarget) {
    MissionGraph g;
    const NodeId a = g.addNode(GraphNode{NodeId{0}, SatelliteId{0}, orbitAt(500.0, 0.0)});
    const TransferModel model;

    const PathResult result = findEarliestArrival(g, model, a, a, 412.0, permissiveOptions());
    CHECK(result.isFound());
    CHECK_EQ(result.legs().size(), std::size_t{0});
    CHECK_NEAR(result.arrivalTimeSec(), 412.0, 1e-12);
    CHECK(isValid(result.totalObjectives()));
    CHECK_NEAR(result.totalObjectives().time, 0.0, 1e-12);
}

ORCA_TEST(Dijkstra, DirectSingleEdge) {
    MissionGraph g;
    const NodeId a = g.addNode(GraphNode{NodeId{0}, SatelliteId{0}, orbitAt(500.0, 0.0)});
    const NodeId b = g.addNode(GraphNode{NodeId{0}, SatelliteId{1}, orbitAt(800.0, 1.0)});
    const TransferModel model;
    const TransferConstraints c = permissive();
    g.addEdge(a, b, model.evaluate(orbitAt(500.0, 0.0), orbitAt(800.0, 1.0), 0.0, c), 0.0);

    const PathResult result = findEarliestArrival(g, model, a, b, 0.0, permissiveOptions());
    CHECK(result.isFound());
    CHECK_EQ(result.legs().size(), std::size_t{1});
    CHECK(result.legs()[0].from == a);
    CHECK(result.legs()[0].to == b);
    CHECK(result.nodeSequence().size() == 2);
}

ORCA_TEST(Dijkstra, UnreachableTargetNotFound) {
    // Structurally disconnected: no edge exists between the two components at all.
    MissionGraph g;
    const NodeId a = g.addNode(GraphNode{NodeId{0}, SatelliteId{0}, orbitAt(500.0, 0.0)});
    const NodeId isolated =
        g.addNode(GraphNode{NodeId{0}, SatelliteId{1}, orbitAt(2000.0, 0.0)});
    const TransferModel model;

    const PathResult result =
        findEarliestArrival(g, model, a, isolated, 0.0, permissiveOptions());
    CHECK(!result.isFound());
    CHECK_THROWS(result.arrivalTimeSec());
    CHECK_THROWS(result.totalObjectives());
    CHECK_THROWS(result.nodeSequence());
}

ORCA_TEST(Dijkstra, DisconnectedComponentIsUnreachable) {
    // Two well-formed components, no edge crossing between them.
    MissionGraph g;
    const NodeId a = g.addNode(GraphNode{NodeId{0}, SatelliteId{0}, orbitAt(500.0, 0.0)});
    const NodeId b = g.addNode(GraphNode{NodeId{0}, SatelliteId{1}, orbitAt(600.0, 0.5)});
    const NodeId x = g.addNode(GraphNode{NodeId{0}, SatelliteId{2}, orbitAt(2000.0, 0.0)});
    const NodeId y = g.addNode(GraphNode{NodeId{0}, SatelliteId{3}, orbitAt(2500.0, 0.5)});
    const TransferModel model;
    const TransferConstraints c = permissive();
    g.addEdge(a, b, model.evaluate(orbitAt(500.0, 0.0), orbitAt(600.0, 0.5), 0.0, c), 0.0);
    g.addEdge(x, y, model.evaluate(orbitAt(2000.0, 0.0), orbitAt(2500.0, 0.5), 0.0, c), 0.0);

    CHECK(!findEarliestArrival(g, model, a, x, 0.0, permissiveOptions()).isFound());
    CHECK(!findEarliestArrival(g, model, a, y, 0.0, permissiveOptions()).isFound());
    CHECK(findEarliestArrival(g, model, a, b, 0.0, permissiveOptions()).isFound());
}

ORCA_TEST(Dijkstra, EdgeDirectionIsRespected) {
    // A -> B exists; B -> A does not. Searching B -> A must fail even though A -> B
    // succeeds, proving the search does not silently treat edges as bidirectional.
    MissionGraph g;
    const NodeId a = g.addNode(GraphNode{NodeId{0}, SatelliteId{0}, orbitAt(500.0, 0.0)});
    const NodeId b = g.addNode(GraphNode{NodeId{0}, SatelliteId{1}, orbitAt(800.0, 1.0)});
    const TransferModel model;
    const TransferConstraints c = permissive();
    g.addEdge(a, b, model.evaluate(orbitAt(500.0, 0.0), orbitAt(800.0, 1.0), 0.0, c), 0.0);

    CHECK(findEarliestArrival(g, model, a, b, 0.0, permissiveOptions()).isFound());
    CHECK(!findEarliestArrival(g, model, b, a, 0.0, permissiveOptions()).isFound());
}

ORCA_TEST(Dijkstra, RejectsUnknownSourceOrTarget) {
    MissionGraph g;
    const NodeId a = g.addNode(GraphNode{NodeId{0}, SatelliteId{0}, orbitAt(500.0, 0.0)});
    const TransferModel model;
    CHECK_THROWS(findEarliestArrival(g, model, NodeId{99}, a, 0.0, permissiveOptions()));
    CHECK_THROWS(findEarliestArrival(g, model, a, NodeId{99}, 0.0, permissiveOptions()));
}

ORCA_TEST(Dijkstra, EqualCostParallelPathsBothReachTarget) {
    // Two same-altitude, same-phase intermediate satellites (differing only in
    // inclination) offer two parallel, structurally distinct but numerically IDENTICAL
    // (zero transfer time -- pure plane changes with matching phase) routes to a common
    // target. Verified: both give arrival == start time exactly. Which specific route is
    // returned is unspecified (tie-break by heap order); that it is FOUND, at the correct
    // tied time, is what this test asserts.
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
    const auto ev = [&](NodeId u, NodeId v) { return model.evaluate(g.node(u).orbit(), g.node(v).orbit(), 0.0, c); };
    g.addEdge(a, b1, ev(a, b1), 0.0);
    g.addEdge(a, b2, ev(a, b2), 0.0);
    g.addEdge(b1, d, ev(b1, d), 0.0);
    g.addEdge(b2, d, ev(b2, d), 0.0);

    const PathResult result = findEarliestArrival(g, model, a, d, 0.0, permissiveOptions());
    CHECK(result.isFound());
    CHECK_NEAR(result.arrivalTimeSec(), 0.0, 1e-9);
    CHECK_EQ(result.legs().size(), std::size_t{2});
}

// ---------------------------------------------------------------------------
// Search metrics
// ---------------------------------------------------------------------------

ORCA_TEST(Dijkstra, MetricsAreInternallyConsistent) {
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
        findEarliestArrival(g, model, a, cNode, 0.0, permissiveOptions());
    const SearchMetrics& m = result.metrics();

    CHECK(result.isFound());
    CHECK(m.nodesExpanded >= 1);
    CHECK(m.nodesExpanded <= static_cast<int>(g.nodeCount()));
    CHECK(m.edgesEvaluated >= 1);
    CHECK(m.priorityQueuePushes >= m.priorityQueuePops);
    CHECK(m.relaxationsSucceeded >= 1);
    CHECK(m.executionTimeMs >= 0.0);
}

ORCA_TEST(Dijkstra, MetricsPopulatedEvenWhenNotFound) {
    MissionGraph g;
    const NodeId a = g.addNode(GraphNode{NodeId{0}, SatelliteId{0}, orbitAt(500.0, 0.0)});
    const NodeId isolated =
        g.addNode(GraphNode{NodeId{0}, SatelliteId{1}, orbitAt(2000.0, 0.0)});
    const TransferModel model;

    const PathResult result =
        findEarliestArrival(g, model, a, isolated, 0.0, permissiveOptions());
    CHECK(!result.isFound());
    CHECK(result.metrics().nodesExpanded >= 1);  // source itself is still expanded
    CHECK(result.metrics().executionTimeMs >= 0.0);
}
