/// Tests for Dijkstra's time-dependence, and the documented cumulative-budget limitation.
///
/// This is the file the Phase 3 brief is centrally about: §20's "critical rule" and the
/// resource-constraint design boundary explained in Dijkstra.hpp's file comment. Basic
/// structural correctness (trivial/direct/unreachable/directed/metrics) lives in
/// dijkstra_test.cpp; independent brute-force verification lives in
/// dijkstra_oracle_test.cpp.
///
/// Every scenario with a specific numeric expectation was verified by compiling and
/// running it against the real `TransferModel`/`Dijkstra` before being written here. The
/// two phase-tuned scenarios deliberately use *robust* configurations rather than exact
/// rendezvous-window alignment: an earlier attempt at an exactly-aligned test case turned
/// out to sit on a knife-edge where a sub-ulp difference between derivation and execution
/// flips the result by a full synodic period. Simple, arbitrary phase values, verified to
/// produce a wide margin, are used instead throughout.

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
// THE CRITICAL RULE: edges are evaluated at the arrival time of the node being
// expanded, never at the search's global start time.
// ---------------------------------------------------------------------------

ORCA_TEST(TimeDependence, IndirectLegUsesArrivalTimeAtIntermediateNodeNotGlobalStart) {
    // A -> B -> C, verified (by direct execution) to have a leg-2 wait that differs by
    // roughly 158,000 seconds depending on whether it is evaluated at B's actual arrival
    // time (correct) or at the search's t=0 (the bug this test exists to catch).
    MissionGraph g;
    const Orbit orbitA = orbitAt(500.0, 0.0);
    const Orbit orbitB = orbitAt(650.0, 1.0);
    const Orbit orbitC = orbitAt(800.0, 2.0);
    const NodeId a = g.addNode(GraphNode{NodeId{0}, SatelliteId{0}, orbitA});
    const NodeId b = g.addNode(GraphNode{NodeId{0}, SatelliteId{1}, orbitB});
    const NodeId cNode = g.addNode(GraphNode{NodeId{0}, SatelliteId{2}, orbitC});

    const TransferModel model;
    const TransferConstraints c = permissive();
    // The edges' cached referenceResult is deliberately sampled at t=0 for BOTH edges --
    // the wrong departure time for the second leg. If Dijkstra ever read this cache
    // instead of re-evaluating, this test would pass by accident; the assertions below
    // check Dijkstra's actual internal leg, not merely whether a path was found, to close
    // that hole.
    g.addEdge(a, b, model.evaluate(orbitA, orbitB, 0.0, c), 0.0);
    g.addEdge(b, cNode, model.evaluate(orbitB, orbitC, 0.0, c), 0.0);

    const PathResult result =
        findEarliestArrival(g, model, a, cNode, 0.0, permissiveOptions());
    CHECK(result.isFound());
    CHECK_EQ(result.legs().size(), std::size_t{2});

    const Seconds arrivalAtB = result.legs()[0].result.geometry.arrivalTimeSec;

    const TransferResult correctlyTimed = model.evaluate(orbitB, orbitC, arrivalAtB, c);
    const TransferResult staticallyTimed = model.evaluate(orbitB, orbitC, 0.0, c);

    // Sanity: the two independent reference calls must actually differ, or this test
    // would prove nothing regardless of which one Dijkstra's leg matches.
    CHECK(std::fabs(correctlyTimed.geometry.waitSec - staticallyTimed.geometry.waitSec) >
         1000.0);

    const TransferResult& actualLeg2 = result.legs()[1].result;
    CHECK_NEAR(actualLeg2.geometry.waitSec, correctlyTimed.geometry.waitSec, 1e-6);
    CHECK_NEAR(actualLeg2.geometry.arrivalTimeSec, correctlyTimed.geometry.arrivalTimeSec,
              1e-6);
    // And explicitly does NOT match the static (buggy) computation, by a wide margin --
    // this is what would fail if a future refactor accidentally converted the search back
    // to a static-weight shortest path wearing a time-dependent graph's clothes.
    CHECK(std::fabs(actualLeg2.geometry.waitSec - staticallyTimed.geometry.waitSec) >
         1000.0);
}

ORCA_TEST(TimeDependence, DijkstraChoosesIndirectRouteWhenGenuinelyFasterOnArrivalTime) {
    // A and C sit 5 km apart in altitude (near-identical -> a huge synodic period, so a
    // direct transfer departing "now" almost always has to wait a large fraction of it).
    // B is a well-separated staging point reachable quickly from both. Verified: direct
    // arrives at ~2.49e6 s; indirect (via B) arrives at ~9.58e4 s -- a margin of roughly
    // 2.39e6 s, confirmed insensitive to small phase perturbations (not a knife-edge).
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
    g.addEdge(a, cNode, model.evaluate(orbitA, orbitC, 0.0, c), 0.0);  // slow, but present

    const PathResult result =
        findEarliestArrival(g, model, a, cNode, 0.0, permissiveOptions());
    CHECK(result.isFound());

    // The direct edge exists and is feasible (checked independently), yet Dijkstra must
    // choose the two-leg route because it arrives earlier.
    const TransferResult direct = model.evaluate(orbitA, orbitC, 0.0, c);
    CHECK(direct.feasible);
    CHECK(result.arrivalTimeSec() < direct.geometry.arrivalTimeSec);
    CHECK_EQ(result.legs().size(), std::size_t{2});
    CHECK(result.legs()[0].to == b);
}

// ---------------------------------------------------------------------------
// The documented, tested boundary: per-edge delta-v feasibility is enforced; a
// cumulative path-wide budget is not (Dijkstra.hpp's file comment explains why single-
// label search cannot soundly enforce it, and why that is Pareto search's job instead).
// ---------------------------------------------------------------------------

ORCA_TEST(Dijkstra, CumulativeBudgetIsNotEnforcedByDesign) {
    // Two 10-degree plane changes at 500 km, same altitude and phase throughout so both
    // legs are instantaneous (coast = wait = 0) and differ only in delta-v. Each leg costs
    // 1.326965039 km/s (matches the independently-verified Phase 2 oracle value for a
    // 10-degree plane change at 500 km). A 2.0 km/s vehicle budget affords EITHER leg
    // alone (1.326965 <= 2.0) but not both together (2.653930 > 2.0).
    MissionGraph g;
    const Orbit orbitA = orbitAt(500.0, 0.0, 0.0);
    const Orbit orbitB = orbitAt(500.0, 0.0, degToRad(10.0));
    const Orbit orbitC = orbitAt(500.0, 0.0, degToRad(20.0));
    const NodeId a = g.addNode(GraphNode{NodeId{0}, SatelliteId{0}, orbitA});
    const NodeId b = g.addNode(GraphNode{NodeId{0}, SatelliteId{1}, orbitB});
    const NodeId cNode = g.addNode(GraphNode{NodeId{0}, SatelliteId{2}, orbitC});

    const TransferModel model;
    const TransferConstraints buildC = permissive();
    // Deliberately no direct A->C edge: this test isolates the two-leg cumulative case.
    g.addEdge(a, b, model.evaluate(orbitA, orbitB, 0.0, buildC), 0.0);
    g.addEdge(b, cNode, model.evaluate(orbitB, orbitC, 0.0, buildC), 0.0);

    SearchConstraints options;
    options.vehicleDeltaVBudgetKms = 2.0;
    options.maxWaitSec = 1.0e13;
    options.latestArrivalSec = 1.0e18;

    const PathResult result = findEarliestArrival(g, model, a, cNode, 0.0, options);

    // This is the documented limitation, verified rather than assumed: the search
    // reports a route as found even though its cumulative delta-v exceeds the vehicle's
    // total budget, because each INDIVIDUAL leg (1.326965 km/s) is well within it.
    CHECK(result.isFound());
    CHECK_NEAR(result.legs()[0].result.geometry.deltaVKms, 1.326965039, 1e-6);
    CHECK_NEAR(result.legs()[1].result.geometry.deltaVKms, 1.326965039, 1e-6);

    // The reported total honestly reflects the overspend (fuel = deltaV / budget summed
    // across legs via the canonical compose() rule) even though the search did not use
    // it to reject the route -- exactly the gap the file comment documents.
    CHECK(result.totalObjectives().fuel > 1.0);
    CHECK_NEAR(result.totalObjectives().fuel, 2.0 * 1.326965039 / 2.0, 1e-6);
}

ORCA_TEST(Dijkstra, SingleLegExceedingTotalBudgetIsRejected) {
    // The per-edge check this design DOES perform: a leg whose own delta-v exceeds the
    // vehicle's entire budget must never be relaxed, regardless of path history.
    MissionGraph g;
    const Orbit orbitA = orbitAt(500.0, 0.0, 0.0);
    const Orbit orbitB = orbitAt(500.0, 0.0, degToRad(90.0));  // ~10.77 km/s, huge
    const NodeId a = g.addNode(GraphNode{NodeId{0}, SatelliteId{0}, orbitA});
    const NodeId b = g.addNode(GraphNode{NodeId{0}, SatelliteId{1}, orbitB});

    const TransferModel model;
    g.addEdge(a, b, model.evaluate(orbitA, orbitB, 0.0, permissive()), 0.0);

    SearchConstraints options;
    options.vehicleDeltaVBudgetKms = 2.5;  // far less than the ~10.77 km/s the leg needs
    options.maxWaitSec = 1.0e13;
    options.latestArrivalSec = 1.0e18;

    CHECK(!findEarliestArrival(g, model, a, b, 0.0, options).isFound());
}

