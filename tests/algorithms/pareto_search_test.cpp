/// Correctness tests for `findParetoRoutes()`: trivial/single/multi-route frontiers and
/// genuine trade-offs, including a hard-won four-route mutually non-dominated frontier.
/// Time-dependence, cycles, duplicates, `maxLabels` truncation and the cumulative
/// delta-v hard constraint live in `pareto_search_time_dependence_test.cpp` (split the
/// same way `dijkstra_test.cpp`/`dijkstra_time_dependence_test.cpp` are, for file-size
/// reasons -- Phase 5 brief §65). Independent brute-force oracle verification lives in
/// `pareto_search_oracle_test.cpp`.
///
/// Every numeric expectation below was found by compiling and running this exact scenario
/// against the real `TransferModel`/`findParetoRoutes` (never hand-derived) -- including
/// the four- and two-route trade-off scenarios, which came from a wider candidate sweep
/// (see the comments on each) reduced to the minimal graph that reproduces the same
/// result. Constructing a *natural* three-or-more-way trade-off from realistic orbital
/// parameters took real search: most parameter combinations turn out to be strongly
/// correlated across objectives (a bigger manoeuvre tends to cost more time, fuel *and*
/// risk together), so genuinely incomparable routes are less common than the textbook
/// picture suggests. That difficulty is itself an honest finding, not hidden -- see
/// `PARETO_SEARCH.md`'s discussion of it.

#include <cmath>

#include "../../cpp/algorithms/Dominance.hpp"
#include "../../cpp/algorithms/ParetoSearch.hpp"
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
// Basic frontier shapes
// ---------------------------------------------------------------------------

ORCA_TEST(ParetoSearch, TrivialSourceEqualsTargetGivesOneZeroCostSolution) {
    MissionGraph g;
    const NodeId a = g.addNode(GraphNode{NodeId{0}, SatelliteId{0}, orbitAt(500.0, 0.0)});
    const TransferModel model;

    const ParetoSearchResult result =
        findParetoRoutes(g, model, a, a, 412.0, permissiveOptions());
    CHECK_EQ(result.solutions.size(), std::size_t{1});
    CHECK_EQ(result.solutions[0].legs.size(), std::size_t{0});
    CHECK_NEAR(result.solutions[0].arrivalTimeSec, 412.0, 1e-12);
    CHECK(isValid(result.solutions[0].totalCost));
    CHECK_NEAR(result.solutions[0].totalCost.time, 0.0, 1e-12);
}

ORCA_TEST(ParetoSearch, SingleRouteNoAlternatives) {
    MissionGraph g;
    const NodeId a = g.addNode(GraphNode{NodeId{0}, SatelliteId{0}, orbitAt(500.0, 0.0)});
    const NodeId b = g.addNode(GraphNode{NodeId{0}, SatelliteId{1}, orbitAt(800.0, 1.0)});
    const TransferModel model;
    const TransferConstraints c = permissive();
    g.addEdge(a, b, model.evaluate(orbitAt(500.0, 0.0), orbitAt(800.0, 1.0), 0.0, c), 0.0);

    const ParetoSearchResult result =
        findParetoRoutes(g, model, a, b, 0.0, permissiveOptions());
    CHECK_EQ(result.solutions.size(), std::size_t{1});
    CHECK_EQ(result.solutions[0].legs.size(), std::size_t{1});
}

ORCA_TEST(ParetoSearch, UnreachableTargetGivesEmptyFrontier) {
    MissionGraph g;
    const NodeId a = g.addNode(GraphNode{NodeId{0}, SatelliteId{0}, orbitAt(500.0, 0.0)});
    const NodeId isolated =
        g.addNode(GraphNode{NodeId{0}, SatelliteId{1}, orbitAt(2000.0, 0.0)});
    const TransferModel model;

    const ParetoSearchResult result =
        findParetoRoutes(g, model, a, isolated, 0.0, permissiveOptions());
    CHECK(result.isEmpty());
    CHECK(result.solutions.empty());
}

ORCA_TEST(ParetoSearch, RejectsUnknownSourceOrTarget) {
    MissionGraph g;
    const NodeId a = g.addNode(GraphNode{NodeId{0}, SatelliteId{0}, orbitAt(500.0, 0.0)});
    const TransferModel model;
    CHECK_THROWS(findParetoRoutes(g, model, NodeId{99}, a, 0.0, permissiveOptions()));
    CHECK_THROWS(findParetoRoutes(g, model, a, NodeId{99}, 0.0, permissiveOptions()));
}

// ---------------------------------------------------------------------------
// Genuine trade-offs: the property no single-objective search can express.
// ---------------------------------------------------------------------------

ORCA_TEST(ParetoSearch, TwoGenuineTradeOffRoutesBothSurvive) {
    // A faster-but-longer route and a slower-but-shorter route to the same target -- real
    // orbital data, verified: neither dominates (time and distance trade off oppositely).
    MissionGraph g;
    const Orbit s0 = orbitAt(500.0, 0.0);
    const Orbit fastMid = orbitAt(600.0, 0.3);
    const Orbit cheapMid = orbitAt(520.0, 1.5);
    const Orbit tgt = orbitAt(1500.0, 2.7);
    const NodeId ns = g.addNode(GraphNode{NodeId{0}, SatelliteId{0}, s0});
    const NodeId nFast = g.addNode(GraphNode{NodeId{0}, SatelliteId{1}, fastMid});
    const NodeId nCheap = g.addNode(GraphNode{NodeId{0}, SatelliteId{2}, cheapMid});
    const NodeId nt = g.addNode(GraphNode{NodeId{0}, SatelliteId{3}, tgt});
    const TransferModel model;
    const TransferConstraints c = permissive();
    g.addEdge(ns, nFast, model.evaluate(s0, fastMid, 0.0, c), 0.0);
    g.addEdge(ns, nCheap, model.evaluate(s0, cheapMid, 0.0, c), 0.0);
    g.addEdge(nFast, nt, model.evaluate(fastMid, tgt, 0.0, c), 0.0);
    g.addEdge(nCheap, nt, model.evaluate(cheapMid, tgt, 0.0, c), 0.0);

    const ParetoSearchResult result =
        findParetoRoutes(g, model, ns, nt, 0.0, permissiveOptions());
    CHECK_EQ(result.solutions.size(), std::size_t{2});

    const bool firstIsFaster = result.solutions[0].totalCost.time < result.solutions[1].totalCost.time;
    const ParetoRoute& faster = firstIsFaster ? result.solutions[0] : result.solutions[1];
    const ParetoRoute& shorter = firstIsFaster ? result.solutions[1] : result.solutions[0];
    CHECK_NEAR(faster.totalCost.time, 49902.338, 1.0);
    CHECK_NEAR(shorter.totalCost.time, 323339.787, 1.0);
    // The trade-off: faster route travels farther, shorter-distance route is slower.
    CHECK(faster.totalCost.distance > shorter.totalCost.distance);
}

ORCA_TEST(ParetoSearch, FourMutuallyNonDominatedRoutesAllSurvive) {
    // Found via a wider candidate sweep (60 combinations of altitude/phase/inclination),
    // reduced to the minimal 4-candidate graph that reproduces the identical result --
    // see the file comment. Exercises a genuine three-way interplay: cand A beats cand B
    // on time and risk but loses on distance; no single objective ranks all four the same
    // way.
    MissionGraph g;
    const Orbit s0 = orbitAt(500.0, 0.0, 0.0);
    const Orbit tgt = orbitAt(3000.0, 4.0, degToRad(20.0));
    const Orbit m1 = orbitAt(600.0, 0.1, degToRad(14.0));
    const Orbit m2 = orbitAt(1800.0, 1.7, degToRad(1.0));
    const Orbit m3 = orbitAt(900.0, 1.7, degToRad(1.0));
    const Orbit m4 = orbitAt(1300.0, 3.4, degToRad(1.0));
    const NodeId ns = g.addNode(GraphNode{NodeId{0}, SatelliteId{0}, s0});
    const NodeId nt = g.addNode(GraphNode{NodeId{0}, SatelliteId{5}, tgt});
    const TransferModel model;
    const TransferConstraints c = permissive();
    int satIdx = 1;
    for (const Orbit& mid : {m1, m2, m3, m4}) {
        const NodeId n = g.addNode(
            GraphNode{NodeId{0}, SatelliteId{static_cast<std::uint32_t>(satIdx++)}, mid});
        g.addEdge(ns, n, model.evaluate(s0, mid, 0.0, c), 0.0);
        g.addEdge(n, nt, model.evaluate(mid, tgt, 0.0, c), 0.0);
    }

    const ParetoSearchResult result =
        findParetoRoutes(g, model, ns, nt, 0.0, permissiveOptions());
    CHECK_EQ(result.solutions.size(), std::size_t{4});

    // Every pair must be genuinely incomparable -- checked directly rather than trusted,
    // since "4 solutions" alone does not prove none secretly dominates another (a bug
    // that failed to remove a dominated label would also produce 4, just wrong ones).
    for (std::size_t i = 0; i < result.solutions.size(); ++i) {
        for (std::size_t j = 0; j < result.solutions.size(); ++j) {
            if (i == j) continue;
            CHECK(!dominates(result.solutions[i].totalCost, result.solutions[j].totalCost));
        }
    }
}
