/// Tests for `findParetoRoutes()`'s time-dependence, cycle termination, duplicate
/// handling, `maxLabels` truncation, and the cumulative delta-v hard constraint -- split
/// from `pareto_search_test.cpp`'s basic-correctness/trade-off tests the same way
/// `dijkstra_time_dependence_test.cpp` is split from `dijkstra_test.cpp` (file-size
/// guideline, Phase 5 brief §65).
///
/// Every numeric expectation below was found by compiling and running the exact scenario
/// against the real `TransferModel`/`findParetoRoutes`, never hand-derived.

#include <cmath>

#include "../../cpp/algorithms/Dijkstra.hpp"
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

/// The FourMutuallyNonDominatedRoutesAllSurvive graph from pareto_search_test.cpp,
/// rebuilt here since the truncation and dominated-candidate tests below both need it and
/// this file does not share fixtures across translation units (matching every other test
/// file's convention in this codebase).
MissionGraph buildFourWayTradeOffGraph(const TransferModel& model, NodeId& sourceOut,
                                       NodeId& targetOut) {
    MissionGraph g;
    const Orbit s0 = orbitAt(500.0, 0.0, 0.0);
    const Orbit tgt = orbitAt(3000.0, 4.0, degToRad(20.0));
    const Orbit m1 = orbitAt(600.0, 0.1, degToRad(14.0));
    const Orbit m2 = orbitAt(1800.0, 1.7, degToRad(1.0));
    const Orbit m3 = orbitAt(900.0, 1.7, degToRad(1.0));
    const Orbit m4 = orbitAt(1300.0, 3.4, degToRad(1.0));
    const NodeId ns = g.addNode(GraphNode{NodeId{0}, SatelliteId{0}, s0});
    const NodeId nt = g.addNode(GraphNode{NodeId{0}, SatelliteId{5}, tgt});
    const TransferConstraints c = permissive();
    int satIdx = 1;
    for (const Orbit& mid : {m1, m2, m3, m4}) {
        const NodeId n = g.addNode(
            GraphNode{NodeId{0}, SatelliteId{static_cast<std::uint32_t>(satIdx++)}, mid});
        g.addEdge(ns, n, model.evaluate(s0, mid, 0.0, c), 0.0);
        g.addEdge(n, nt, model.evaluate(mid, tgt, 0.0, c), 0.0);
    }
    sourceOut = ns;
    targetOut = nt;
    return g;
}

}  // namespace

// ---------------------------------------------------------------------------
// maxLabels truncation (Phase 5 brief §39-40). Found missing during self-review: the
// truncation code path (ParetoSearch.cpp's `pool.size() >= options.maxLabels` check) had
// no test confirming it actually fires, or that `metrics.truncated` is set correctly.
// Numbers below verified by compiling and running this exact scenario at each cap.
// ---------------------------------------------------------------------------

ORCA_TEST(ParetoSearch, LowMaxLabelsCapTruncatesWithADemonstrablyIncompleteFront) {
    // True frontier is 4 routes, needing 9 labels ever inserted (verified). A cap of 5
    // stops the search before any route reaches the target at all.
    const TransferModel model;
    NodeId ns{0};
    NodeId nt{0};
    const MissionGraph g = buildFourWayTradeOffGraph(model, ns, nt);

    ParetoSearchOptions cappedOpts;
    cappedOpts.maxLabels = 5;
    const ParetoSearchResult capped =
        findParetoRoutes(g, model, ns, nt, 0.0, permissiveOptions(), cappedOpts);
    CHECK(capped.metrics.truncated);
    CHECK_EQ(capped.solutions.size(), std::size_t{0});  // demonstrably incomplete: true
                                                        // front has 4 routes

    ParetoSearchOptions generousOpts;  // default maxLabels, effectively unbounded here
    const ParetoSearchResult complete =
        findParetoRoutes(g, model, ns, nt, 0.0, permissiveOptions(), generousOpts);
    CHECK(!complete.metrics.truncated);
    CHECK_EQ(complete.solutions.size(), std::size_t{4});
}

ORCA_TEST(ParetoSearch, TruncatedFlagCanFireEvenWhenResultHappensToBeComplete) {
    // A cap set to exactly the number of labels the full search needs (9, verified) still
    // trips the truncation check on its way there -- `truncated` is a CONSERVATIVE flag:
    // it may be true even when nothing was actually lost, but is never false when
    // something was. Documented here so a future reader does not mistake `truncated` for
    // "the front is provably incomplete" -- only "completeness was not guaranteed."
    const TransferModel model;
    NodeId ns{0};
    NodeId nt{0};
    const MissionGraph g = buildFourWayTradeOffGraph(model, ns, nt);

    ParetoSearchOptions opts;
    opts.maxLabels = 9;
    const ParetoSearchResult result =
        findParetoRoutes(g, model, ns, nt, 0.0, permissiveOptions(), opts);
    CHECK(result.metrics.truncated);
    CHECK_EQ(result.solutions.size(), std::size_t{4});  // happens to be complete anyway
}

ORCA_TEST(ParetoSearch, DominatedCandidateNeverAppearsAlongsideRealFrontier) {
    // The four-way trade-off graph, plus one extra, deliberately extreme candidate (high
    // altitude jump, bad phase, large inclination) -- verified to be dominated, so the
    // frontier must stay at 4, not grow to 5.
    MissionGraph g;
    const Orbit s0 = orbitAt(500.0, 0.0, 0.0);
    const Orbit tgt = orbitAt(3000.0, 4.0, degToRad(20.0));
    const Orbit m1 = orbitAt(600.0, 0.1, degToRad(14.0));
    const Orbit m2 = orbitAt(1800.0, 1.7, degToRad(1.0));
    const Orbit m3 = orbitAt(900.0, 1.7, degToRad(1.0));
    const Orbit m4 = orbitAt(1300.0, 3.4, degToRad(1.0));
    const Orbit mBad = orbitAt(2500.0, 5.0, degToRad(14.0));
    const NodeId ns = g.addNode(GraphNode{NodeId{0}, SatelliteId{0}, s0});
    const NodeId nt = g.addNode(GraphNode{NodeId{0}, SatelliteId{6}, tgt});
    const TransferModel model;
    const TransferConstraints c = permissive();
    int satIdx = 1;
    for (const Orbit& mid : {m1, m2, m3, m4, mBad}) {
        const NodeId n = g.addNode(
            GraphNode{NodeId{0}, SatelliteId{static_cast<std::uint32_t>(satIdx++)}, mid});
        g.addEdge(ns, n, model.evaluate(s0, mid, 0.0, c), 0.0);
        g.addEdge(n, nt, model.evaluate(mid, tgt, 0.0, c), 0.0);
    }

    const ParetoSearchResult result =
        findParetoRoutes(g, model, ns, nt, 0.0, permissiveOptions());
    CHECK_EQ(result.solutions.size(), std::size_t{4});
}

// ---------------------------------------------------------------------------
// Duplicate handling (Phase 5 brief §9-10, §24 Step 2)
// ---------------------------------------------------------------------------

ORCA_TEST(ParetoSearch, DuplicateRoutesHandledDeterministicallyNotDoubled) {
    // Two branches that happen to produce IDENTICAL cost vectors at the target (same
    // altitude/phase/inclination on both intermediates) -- exactly one solution must
    // survive, and metrics must record it as a duplicate, not a silent double-count.
    MissionGraph g;
    const Orbit s0 = orbitAt(500.0, 0.0, 0.0);
    const Orbit m1 = orbitAt(500.0, 0.0, degToRad(5.0));
    const Orbit m2 = orbitAt(500.0, 0.0, degToRad(5.0));
    const Orbit tgt = orbitAt(500.0, 0.0, degToRad(10.0));
    const NodeId ns = g.addNode(GraphNode{NodeId{0}, SatelliteId{0}, s0});
    const NodeId n1 = g.addNode(GraphNode{NodeId{0}, SatelliteId{1}, m1});
    const NodeId n2 = g.addNode(GraphNode{NodeId{0}, SatelliteId{2}, m2});
    const NodeId nt = g.addNode(GraphNode{NodeId{0}, SatelliteId{3}, tgt});
    const TransferModel model;
    const TransferConstraints c = permissive();
    g.addEdge(ns, n1, model.evaluate(s0, m1, 0.0, c), 0.0);
    g.addEdge(ns, n2, model.evaluate(s0, m2, 0.0, c), 0.0);
    g.addEdge(n1, nt, model.evaluate(m1, tgt, 0.0, c), 0.0);
    g.addEdge(n2, nt, model.evaluate(m2, tgt, 0.0, c), 0.0);

    const ParetoSearchResult result =
        findParetoRoutes(g, model, ns, nt, 0.0, permissiveOptions());
    CHECK_EQ(result.solutions.size(), std::size_t{1});
    CHECK(result.metrics.labelsDuplicate >= 1);
}

// ---------------------------------------------------------------------------
// Cycles: must terminate, and agree with independently-verified Dijkstra.
// ---------------------------------------------------------------------------

ORCA_TEST(ParetoSearch, CyclicGraphTerminatesAndAgreesWithDijkstraOnArrivalTime) {
    // The exact 7-node directed graph with a real cycle (n3 -> n1) from
    // dijkstra_oracle_test.cpp -- reused deliberately, since Dijkstra's optimality on it
    // is already independently established there.
    MissionGraph g;
    const NodeId n0 = g.addNode(GraphNode{NodeId{0}, SatelliteId{0}, orbitAt(500.0, 0.0)});
    const NodeId n1 =
        g.addNode(GraphNode{NodeId{0}, SatelliteId{1}, orbitAt(620.0, 0.8, degToRad(5.7))});
    const NodeId n2 =
        g.addNode(GraphNode{NodeId{0}, SatelliteId{2}, orbitAt(710.0, 2.1, degToRad(2.9))});
    const NodeId n3 =
        g.addNode(GraphNode{NodeId{0}, SatelliteId{3}, orbitAt(900.0, 3.5, degToRad(11.5))});
    const NodeId n4 =
        g.addNode(GraphNode{NodeId{0}, SatelliteId{4}, orbitAt(1200.0, 1.2, degToRad(8.6))});
    const NodeId n5 =
        g.addNode(GraphNode{NodeId{0}, SatelliteId{5}, orbitAt(1500.0, 4.4, degToRad(17.2))});
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
    add(n3, n1);  // the cycle

    const ParetoSearchResult result =
        findParetoRoutes(g, model, n0, n5, 0.0, permissiveOptions());
    CHECK(!result.solutions.empty());  // terminated with a real answer, not a hang

    const PathResult dijkstra = findEarliestArrival(g, model, n0, n5, 0.0, permissiveOptions());
    CHECK(dijkstra.isFound());
    // Dijkstra's earliest-arrival answer must appear somewhere on the Pareto front: the
    // fastest route can never be dominated on time by anything, so it is always retained.
    bool foundMatchingArrival = false;
    for (const ParetoRoute& route : result.solutions) {
        if (std::fabs(route.arrivalTimeSec - dijkstra.arrivalTimeSec()) < 1e-6) {
            foundMatchingArrival = true;
        }
    }
    CHECK(foundMatchingArrival);
}

// ---------------------------------------------------------------------------
// Time-dependence: THE CRITICAL RULE, identical requirement to Dijkstra/A*.
// ---------------------------------------------------------------------------

ORCA_TEST(ParetoSearch, IndirectLegUsesLabelArrivalTimeNotGlobalStart) {
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

    const ParetoSearchResult result =
        findParetoRoutes(g, model, a, cNode, 0.0, permissiveOptions());
    CHECK_EQ(result.solutions.size(), std::size_t{1});
    CHECK_EQ(result.solutions[0].legs.size(), std::size_t{2});

    const Seconds arrivalAtB = result.solutions[0].legs[0].result.geometry.arrivalTimeSec;
    const TransferResult correctlyTimed = model.evaluate(orbitB, orbitC, arrivalAtB, c);
    const TransferResult staticallyTimed = model.evaluate(orbitB, orbitC, 0.0, c);
    CHECK(std::fabs(correctlyTimed.geometry.waitSec - staticallyTimed.geometry.waitSec) >
         1000.0);

    const TransferResult& actualLeg2 = result.solutions[0].legs[1].result;
    CHECK_NEAR(actualLeg2.geometry.waitSec, correctlyTimed.geometry.waitSec, 1e-6);
    CHECK(std::fabs(actualLeg2.geometry.waitSec - staticallyTimed.geometry.waitSec) > 1000.0);
}

// ---------------------------------------------------------------------------
// The cumulative delta-v hard constraint -- the concrete fix for Dijkstra/A*'s
// documented, tested limitation (Phase 5 brief §41).
// ---------------------------------------------------------------------------

ORCA_TEST(ParetoSearch, CumulativeDeltaVHardConstraintRejectsWhatDijkstraWronglyAccepts) {
    // Identical scenario to Dijkstra/A*'s CumulativeBudgetIsNotEnforcedByDesign test: two
    // 10-degree plane changes at 500 km, each individually affordable (1.326965 km/s) but
    // not together against a 2.0 km/s budget (2.653930 km/s > 2.0).
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

    // Dijkstra: the documented limitation -- wrongly reports this route as found, since
    // it can only check per-edge feasibility, never a cumulative path-wide budget.
    const PathResult dijkstra = findEarliestArrival(g, model, a, cNode, 0.0, options);
    CHECK(dijkstra.isFound());

    // Pareto: correctly rejects it via each candidate's accumulated cost.fuel.
    const ParetoSearchResult pareto = findParetoRoutes(g, model, a, cNode, 0.0, options);
    CHECK(pareto.isEmpty());
}

ORCA_TEST(ParetoSearch, CumulativeConstraintRejectsOneRouteButCheaperAlternateSurvives) {
    // Same target as the two-leg over-budget route above (20 degrees total from A), plus
    // a direct A->C edge: the SAME total plane change as ONE combined burn instead of two
    // separate ones. Verified real numbers: two-leg sum = 2.653930 km/s (1.326965 x 2);
    // direct combined = 2.643831 km/s -- cheaper, matching TRANSFER_MODEL.md's established
    // "combining beats separate burns" result, but only by about 0.4%. A budget of 2.65
    // km/s (strictly between the two) is what actually separates "the two-leg route is
    // rejected" from "a cheaper alternate survives" -- 2.0 km/s (used in the test above)
    // rejects BOTH, which would not demonstrate this test's point.
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
    g.addEdge(a, cNode, model.evaluate(orbitA, orbitC, 0.0, buildC), 0.0);

    SearchConstraints options;
    options.vehicleDeltaVBudgetKms = 2.65;
    options.maxWaitSec = 1.0e13;
    options.latestArrivalSec = 1.0e18;

    const ParetoSearchResult pareto = findParetoRoutes(g, model, a, cNode, 0.0, options);
    CHECK(!pareto.isEmpty());
    for (const ParetoRoute& route : pareto.solutions) {
        // Every surviving route must be the direct one leg -- the two-leg route (2.653930
        // km/s cumulative) exceeds 2.65 and was rejected; only the direct 2.643831 km/s
        // combined burn fits.
        CHECK_EQ(route.legs.size(), std::size_t{1});
        CHECK_NEAR(route.legs[0].result.geometry.deltaVKms, 2.643831, 1e-5);
    }
}
