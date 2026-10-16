/// Tests for A*'s time-dependence, the h=0 ablation, and reopening.
///
/// This is A*'s analogue of dijkstra_time_dependence_test.cpp: the critical rule (edges
/// evaluated at the expanding node's actual arrival time, never the search's global start)
/// applies to A* exactly as it does to Dijkstra, since both call the identical
/// `TransferModel::evaluate(fromOrbit, toOrbit, current.g, constraints)` at the identical
/// point in the algorithm. The first two tests below reuse the exact scenarios from
/// dijkstra_time_dependence_test.cpp -- same orbits, same independently-verified numbers --
/// specifically to demonstrate agreement, not merely similarity.
///
/// The remaining two tests exist only for A*: `ZeroHeuristicMatchesDijkstra` (the h=0
/// ablation Phase 4 requires) and the reopening test, which forces the one code path
/// (AStar.cpp's lack of a permanent closed set) that every other test in this file leaves
/// unexercised, because the real heuristic is consistent throughout ORCA's realistic range
/// (heuristic_consistency_test.cpp) and so never needs to reopen anything in a realistic
/// scenario. Without a dedicated test, that path would be silently untested.

#include <cmath>
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

}  // namespace

// ---------------------------------------------------------------------------
// THE CRITICAL RULE, reusing dijkstra_time_dependence_test.cpp's exact scenarios.
// ---------------------------------------------------------------------------

ORCA_TEST(AStarTimeDependence,
         IndirectLegUsesArrivalTimeAtIntermediateNodeNotGlobalStart) {
    MissionGraph g;
    const Orbit orbitA = orbitAt(500.0, 0.0);
    const Orbit orbitB = orbitAt(650.0, 1.0);
    const Orbit orbitC = orbitAt(800.0, 2.0);
    const NodeId a = g.addNode(GraphNode{NodeId{0}, SatelliteId{0}, orbitA});
    const NodeId b = g.addNode(GraphNode{NodeId{0}, SatelliteId{1}, orbitB});
    const NodeId cNode = g.addNode(GraphNode{NodeId{0}, SatelliteId{2}, orbitC});

    const TransferModel model;
    const TransferConstraints c = permissive();
    g.addEdge(a, b, model.evaluate(orbitA, orbitB, 0.0, c), 0.0);
    g.addEdge(b, cNode, model.evaluate(orbitB, orbitC, 0.0, c), 0.0);

    const PathResult result =
        findEarliestArrivalAStar(g, model, a, cNode, 0.0, permissiveOptions());
    CHECK(result.isFound());
    CHECK_EQ(result.legs().size(), std::size_t{2});

    const Seconds arrivalAtB = result.legs()[0].result.geometry.arrivalTimeSec;
    const TransferResult correctlyTimed = model.evaluate(orbitB, orbitC, arrivalAtB, c);
    const TransferResult staticallyTimed = model.evaluate(orbitB, orbitC, 0.0, c);

    CHECK(std::fabs(correctlyTimed.geometry.waitSec - staticallyTimed.geometry.waitSec) >
         1000.0);

    const TransferResult& actualLeg2 = result.legs()[1].result;
    CHECK_NEAR(actualLeg2.geometry.waitSec, correctlyTimed.geometry.waitSec, 1e-6);
    CHECK_NEAR(actualLeg2.geometry.arrivalTimeSec, correctlyTimed.geometry.arrivalTimeSec,
              1e-6);
    CHECK(std::fabs(actualLeg2.geometry.waitSec - staticallyTimed.geometry.waitSec) >
         1000.0);
}

ORCA_TEST(AStarTimeDependence, ChoosesIndirectRouteWhenGenuinelyFasterOnArrivalTime) {
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

    const PathResult result =
        findEarliestArrivalAStar(g, model, a, cNode, 0.0, permissiveOptions());
    CHECK(result.isFound());

    const TransferResult direct = model.evaluate(orbitA, orbitC, 0.0, c);
    CHECK(direct.feasible);
    CHECK(result.arrivalTimeSec() < direct.geometry.arrivalTimeSec);
    CHECK_EQ(result.legs().size(), std::size_t{2});
    CHECK(result.legs()[0].to == b);
}

// ---------------------------------------------------------------------------
// The h=0 ablation (Phase 4 brief §45-46): A* with a heuristic that always returns 0
// reduces to f=g everywhere, i.e. pure earliest-arrival-time priority -- the same ordering
// principle Dijkstra's bestArrival-driven queue uses. This does not assert identical
// internal metrics (A* never maintains a permanent closed set, so its
// nodesExpanded/priorityQueue counts can differ from Dijkstra's even when both explore the
// same graph); it asserts identical *results*: found/not-found, arrival time, total
// objectives, and node sequence.
// ---------------------------------------------------------------------------

ORCA_TEST(AStarTimeDependence, ZeroHeuristicMatchesDijkstraOnDirectRoute) {
    MissionGraph g;
    const Orbit orbitA = orbitAt(500.0, 0.0);
    const Orbit orbitB = orbitAt(800.0, 1.0);
    const NodeId a = g.addNode(GraphNode{NodeId{0}, SatelliteId{0}, orbitA});
    const NodeId b = g.addNode(GraphNode{NodeId{0}, SatelliteId{1}, orbitB});
    const TransferModel model;
    const TransferConstraints c = permissive();
    g.addEdge(a, b, model.evaluate(orbitA, orbitB, 0.0, c), 0.0);

    const auto zeroHeuristic = [](const Orbit&, const Orbit&) -> Seconds { return 0.0; };

    const PathResult dijkstra =
        findEarliestArrival(g, model, a, b, 0.0, permissiveOptions());
    const PathResult astar = findEarliestArrivalAStar(g, model, a, b, 0.0,
                                                       permissiveOptions(), zeroHeuristic);

    CHECK_EQ(dijkstra.isFound(), astar.isFound());
    CHECK_NEAR(dijkstra.arrivalTimeSec(), astar.arrivalTimeSec(), 1e-9);
    CHECK_EQ(dijkstra.nodeSequence().size(), astar.nodeSequence().size());
    // heuristicEvaluations is Dijkstra's always-0 field (SearchMetrics.hpp); under h=0,
    // A* still *calls* the heuristic (it just always returns 0), so this is the one metric
    // expected to differ even here -- checked explicitly to document that asymmetry rather
    // than leave it implicit.
    CHECK_EQ(dijkstra.metrics().heuristicEvaluations, 0);
    CHECK(astar.metrics().heuristicEvaluations > 0);
}

ORCA_TEST(AStarTimeDependence, ZeroHeuristicMatchesDijkstraOnIndirectRoute) {
    // Reuses ChoosesIndirectRouteWhenGenuinelyFasterOnArrivalTime's scenario: the case
    // where taking the indirect route matters, not just a direct edge.
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

    const auto zeroHeuristic = [](const Orbit&, const Orbit&) -> Seconds { return 0.0; };

    const PathResult dijkstra =
        findEarliestArrival(g, model, a, cNode, 0.0, permissiveOptions());
    const PathResult astar = findEarliestArrivalAStar(
        g, model, a, cNode, 0.0, permissiveOptions(), zeroHeuristic);

    CHECK(dijkstra.isFound());
    CHECK(astar.isFound());
    CHECK_NEAR(dijkstra.arrivalTimeSec(), astar.arrivalTimeSec(), 1e-9);
    CHECK_NEAR(dijkstra.totalObjectives().time, astar.totalObjectives().time, 1e-9);
    CHECK_NEAR(dijkstra.totalObjectives().fuel, astar.totalObjectives().fuel, 1e-9);
    const std::vector<NodeId> dijkstraPath = dijkstra.nodeSequence();
    const std::vector<NodeId> astarPath = astar.nodeSequence();
    CHECK_EQ(dijkstraPath.size(), astarPath.size());
    for (std::size_t i = 0; i < dijkstraPath.size() && i < astarPath.size(); ++i) {
        CHECK(dijkstraPath[i] == astarPath[i]);
    }
}

// ---------------------------------------------------------------------------
// Reopening: forces the one code path every other test leaves unexercised, using a
// deliberately admissible-but-INCONSISTENT synthetic heuristic (the override hook exists
// exactly to make this kind of test possible -- AStar.hpp's `heuristic` parameter).
//
// The 5-node graph and every g/h value below was found by compiling and running this
// exact scenario (not hand-derived): S and two branches (B1, a 1-hop "misleading" route;
// P, a 1-hop route to a genuinely faster path to V). The chosen heuristic values push B1's
// (worse) route to V to be expanded BEFORE P is ever explored -- each individual h(n) is
// checked to stay within n's real admissible ceiling (trueRemaining(n), computed from the
// real TransferModel, never guessed) even though the heuristic as a whole is NOT
// consistent, which is exactly what forces V to be expanded twice.
// ---------------------------------------------------------------------------

ORCA_TEST(AStarTimeDependence,
         ReopeningCorrectsWronglyExpandedNodeUnderInconsistentButAdmissibleHeuristic) {
    MissionGraph g;
    const Orbit orbitS = orbitAt(500.0, 0.0);
    const Orbit orbitB1 = orbitAt(520.0, 0.3);
    const Orbit orbitP = orbitAt(600.0, 0.31);
    const Orbit orbitV = orbitAt(3000.0, 1.0);
    const Orbit orbitT = orbitAt(3500.0, 0.7);
    const NodeId s = g.addNode(GraphNode{NodeId{0}, SatelliteId{0}, orbitS});
    const NodeId b1 = g.addNode(GraphNode{NodeId{0}, SatelliteId{1}, orbitB1});
    const NodeId p = g.addNode(GraphNode{NodeId{0}, SatelliteId{2}, orbitP});
    const NodeId v = g.addNode(GraphNode{NodeId{0}, SatelliteId{3}, orbitV});
    const NodeId t = g.addNode(GraphNode{NodeId{0}, SatelliteId{4}, orbitT});

    const TransferModel model;
    const TransferConstraints c = permissive();
    g.addEdge(s, b1, model.evaluate(orbitS, orbitB1, 0.0, c), 0.0);
    g.addEdge(b1, v, model.evaluate(orbitB1, orbitV, 0.0, c), 0.0);
    g.addEdge(s, p, model.evaluate(orbitS, orbitP, 0.0, c), 0.0);
    g.addEdge(p, v, model.evaluate(orbitP, orbitV, 0.0, c), 0.0);
    g.addEdge(v, t, model.evaluate(orbitV, orbitT, 0.0, c), 0.0);

    // Independent ground truth: real Dijkstra, sharing no code with A*'s heuristic logic.
    const PathResult truth = findEarliestArrival(g, model, s, t, 0.0, permissiveOptions());
    CHECK(truth.isFound());
    CHECK_NEAR(truth.arrivalTimeSec(), 117250.310898975, 1e-3);

    // Verified-admissible constants (each <= the real trueRemaining(node), confirmed by
    // compiling and running this exact scenario): h(B1)=0, h(V)=0 -- the loosest possible,
    // always safe -- and h(P)=80000, comfortably inside P's real ceiling of ~102725.57s.
    // This combination is what forces B1's (worse) route to V to be expanded before P is
    // ever explored: see AStar.hpp's "WHY THE FIRST GOAL POP IS STILL SAFE TO TRUST" for
    // why the final answer is still correct despite that.
    const auto adversarialHeuristic = [&](const Orbit& current, const Orbit&) -> Seconds {
        if (current.altitudeKm() == orbitP.altitudeKm()) {
            return 80000.0;
        }
        return 0.0;  // S, B1, V, T all get the loosest admissible value
    };

    std::vector<AStarTraceStep> trace;
    const PathResult result = findEarliestArrivalAStar(
        g, model, s, t, 0.0, permissiveOptions(), adversarialHeuristic, &trace);

    CHECK(result.isFound());
    CHECK_NEAR(result.arrivalTimeSec(), truth.arrivalTimeSec(), 1e-3);

    // V must appear at least twice in the expansion trace: once with the wrong (via-B1)
    // g, once more with the corrected (via-P) g -- direct evidence reopening occurred,
    // not merely that the final answer happens to be right.
    int vExpansions = 0;
    Seconds firstGAtV = -1.0;
    Seconds secondGAtV = -1.0;
    for (const AStarTraceStep& step : trace) {
        if (step.node == v) {
            ++vExpansions;
            if (vExpansions == 1) {
                firstGAtV = step.g;
            } else if (vExpansions == 2) {
                secondGAtV = step.g;
            }
        }
    }
    CHECK(vExpansions >= 2);
    // The first (wrong) expansion's g must be strictly worse than the corrected one --
    // reopening only means something if the second expansion is a genuine improvement.
    CHECK(secondGAtV < firstGAtV);
    CHECK_NEAR(firstGAtV, 81121.241815542, 1e-3);
    CHECK_NEAR(secondGAtV, 20153.117226125, 1e-3);
}
