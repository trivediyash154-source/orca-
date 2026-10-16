/// Independent brute-force verification of `findParetoRoutes()` — the most important
/// correctness test in this phase (Phase 5 brief §44-47).
///
/// The oracle below shares NOTHING with the production code it verifies: it does not call
/// `dominates()`/`isEquivalent()` (`Dominance.hpp`), does not call `findParetoRoutes()`,
/// and does not use `ParetoFront`. Dominance is reimplemented from scratch, in this file,
/// with its own independently-chosen epsilon constant. This is deliberate, not an
/// oversight: reusing the production dominance predicate to verify the production search
/// would let a single shared bug validate itself. `dijkstra_oracle_test.cpp` and
/// `heuristic_admissibility_test.cpp` establish the same discipline for the single-
/// objective searches; this file extends it to the multi-objective case.
///
/// ## What "agreement" means here
///
/// The oracle enumerates every *simple* path (bounded by a visited-set), evaluates each
/// sequentially through the real `TransferModel`, applies the same per-edge feasibility
/// AND the same cumulative-delta-v hard constraint (`cost.fuel <= 1.0`) production search
/// enforces, then extracts the non-dominated set using its own from-scratch dominance
/// check. Because equal-cost duplicate paths never dominate each other, a naive "keep
/// everything not dominated" oracle retains every one of them, while production search
/// deliberately collapses duplicates to one representative (`ParetoSearch.cpp`'s
/// documented policy). The comparison below therefore checks *distinct cost vectors*, not
/// raw counts — the right level to compare two implementations with different, both
/// legitimate, duplicate-handling policies.

#include <algorithm>
#include <cmath>
#include <vector>

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

// ---- Independent dominance, written fresh -- see file comment. ------------------------

constexpr double kOracleEpsilon = 1.0e-6;

bool independentDominates(const ObjectiveVector& a, const ObjectiveVector& b) {
    bool strictlyBetter = false;
    if (a.time > b.time + kOracleEpsilon) return false;
    if (a.time < b.time - kOracleEpsilon) strictlyBetter = true;
    if (a.fuel > b.fuel + kOracleEpsilon) return false;
    if (a.fuel < b.fuel - kOracleEpsilon) strictlyBetter = true;
    if (a.risk > b.risk + kOracleEpsilon) return false;
    if (a.risk < b.risk - kOracleEpsilon) strictlyBetter = true;
    if (a.distance > b.distance + kOracleEpsilon) return false;
    if (a.distance < b.distance - kOracleEpsilon) strictlyBetter = true;
    return strictlyBetter;
}

bool independentlyEqual(const ObjectiveVector& a, const ObjectiveVector& b) {
    return std::fabs(a.time - b.time) < kOracleEpsilon &&
          std::fabs(a.fuel - b.fuel) < kOracleEpsilon &&
          std::fabs(a.risk - b.risk) < kOracleEpsilon &&
          std::fabs(a.distance - b.distance) < kOracleEpsilon;
}

// ---- Brute-force multi-objective oracle. -----------------------------------------------

struct OracleSolution {
    ObjectiveVector cost;
};

void enumerateSimplePaths(const MissionGraph& graph, const TransferModel& model,
                          const TransferConstraints& constraints, NodeId current,
                          NodeId target, Seconds arrivedAt, const ObjectiveVector& cost,
                          std::vector<NodeId>& visited,
                          std::vector<OracleSolution>& feasibleAtTarget) {
    if (current == target) {
        feasibleAtTarget.push_back(OracleSolution{cost});
        return;
    }
    for (const EdgeId edgeId : graph.outgoingEdges(current)) {
        const GraphEdge& edge = graph.edge(edgeId);
        if (std::find(visited.begin(), visited.end(), edge.to()) != visited.end()) {
            continue;  // simple paths only, matches dijkstra_oracle_test.cpp's reasoning
        }
        const TransferResult result = model.evaluate(graph.node(current).orbit(),
                                                      graph.node(edge.to()).orbit(),
                                                      arrivedAt, constraints);
        if (!result.feasible) {
            continue;
        }
        const ObjectiveVector newCost = compose(cost, result.objectives());
        if (newCost.fuel > 1.0 + kOracleEpsilon) {
            continue;  // the same cumulative delta-v hard constraint production enforces
        }
        visited.push_back(edge.to());
        enumerateSimplePaths(graph, model, constraints, edge.to(), target,
                             result.geometry.arrivalTimeSec, newCost, visited,
                             feasibleAtTarget);
        visited.pop_back();
    }
}

/// Every feasible simple-path cost vector reduced to its non-dominated, deduplicated
/// (by cost) subset -- what a correct multi-objective search's `solutions` should equal.
std::vector<ObjectiveVector> bruteForceParetoFront(const MissionGraph& graph,
                                                   const TransferModel& model,
                                                   const TransferConstraints& constraints,
                                                   NodeId source, NodeId target,
                                                   Seconds startTimeSec) {
    std::vector<OracleSolution> feasible;
    std::vector<NodeId> visited{source};
    enumerateSimplePaths(graph, model, constraints, source, target, startTimeSec,
                        ObjectiveVector{}, visited, feasible);

    std::vector<ObjectiveVector> nonDominated;
    for (const OracleSolution& candidate : feasible) {
        bool dominated = false;
        for (const OracleSolution& other : feasible) {
            if (independentDominates(other.cost, candidate.cost)) {
                dominated = true;
                break;
            }
        }
        if (dominated) {
            continue;
        }
        const bool alreadyPresent =
            std::any_of(nonDominated.begin(), nonDominated.end(), [&](const ObjectiveVector& v) {
                return independentlyEqual(v, candidate.cost);
            });
        if (!alreadyPresent) {
            nonDominated.push_back(candidate.cost);
        }
    }
    return nonDominated;
}

/// True if `search`'s solutions have exactly the same set of distinct cost vectors as
/// `oracle` -- set equality up to the shared epsilon, order-independent.
bool sameCostVectorSet(const std::vector<ParetoRoute>& search,
                       const std::vector<ObjectiveVector>& oracle) {
    std::vector<ObjectiveVector> searchCosts;
    for (const ParetoRoute& route : search) {
        const bool alreadyPresent = std::any_of(
            searchCosts.begin(), searchCosts.end(),
            [&](const ObjectiveVector& v) { return independentlyEqual(v, route.totalCost); });
        if (!alreadyPresent) {
            searchCosts.push_back(route.totalCost);
        }
    }
    if (searchCosts.size() != oracle.size()) {
        return false;
    }
    for (const ObjectiveVector& oracleCost : oracle) {
        const bool matched =
            std::any_of(searchCosts.begin(), searchCosts.end(), [&](const ObjectiveVector& v) {
                return independentlyEqual(v, oracleCost);
            });
        if (!matched) {
            return false;
        }
    }
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// Hand-inspected cases
// ---------------------------------------------------------------------------

ORCA_TEST(ParetoSearchOracle, AgreesOnTwoGenuineTradeOffRoutes) {
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

    const ParetoSearchResult search =
        findParetoRoutes(g, model, ns, nt, 0.0, permissiveOptions());
    const std::vector<ObjectiveVector> oracle =
        bruteForceParetoFront(g, model, c, ns, nt, 0.0);

    CHECK_EQ(oracle.size(), std::size_t{2});
    CHECK(sameCostVectorSet(search.solutions, oracle));
}

ORCA_TEST(ParetoSearchOracle, AgreesOnFourMutuallyNonDominatedRoutes) {
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

    const ParetoSearchResult search =
        findParetoRoutes(g, model, ns, nt, 0.0, permissiveOptions());
    const std::vector<ObjectiveVector> oracle =
        bruteForceParetoFront(g, model, c, ns, nt, 0.0);

    CHECK_EQ(oracle.size(), std::size_t{4});
    CHECK(sameCostVectorSet(search.solutions, oracle));
}

ORCA_TEST(ParetoSearchOracle, AgreesOnDirectedGraphWithARealCycle) {
    // The same 7-node graph with a real cycle dijkstra_oracle_test.cpp verifies Dijkstra
    // against -- re-checked here for the multi-objective search.
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
    add(n3, n1);

    const ParetoSearchResult search =
        findParetoRoutes(g, model, n0, n5, 0.0, permissiveOptions());
    const std::vector<ObjectiveVector> oracle =
        bruteForceParetoFront(g, model, c, n0, n5, 0.0);

    CHECK(!oracle.empty());
    CHECK(sameCostVectorSet(search.solutions, oracle));
}

ORCA_TEST(ParetoSearchOracle, AgreesOnUnreachableTarget) {
    MissionGraph g;
    const NodeId a = g.addNode(GraphNode{NodeId{0}, SatelliteId{0}, orbitAt(500.0, 0.0)});
    const NodeId isolated =
        g.addNode(GraphNode{NodeId{0}, SatelliteId{1}, orbitAt(2000.0, 0.0)});
    const TransferModel model;
    const TransferConstraints c = permissive();

    const ParetoSearchResult search =
        findParetoRoutes(g, model, a, isolated, 0.0, permissiveOptions());
    const std::vector<ObjectiveVector> oracle =
        bruteForceParetoFront(g, model, c, a, isolated, 0.0);

    CHECK(oracle.empty());
    CHECK(search.isEmpty());
}

ORCA_TEST(ParetoSearchOracle, AgreesOnCumulativeDeltaVRejection) {
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
    TransferConstraints oracleConstraints = permissive();
    oracleConstraints.maxDeltaVKms = 2.0;
    oracleConstraints.deltaVBudgetKms = 2.0;

    const ParetoSearchResult search = findParetoRoutes(g, model, a, cNode, 0.0, options);
    const std::vector<ObjectiveVector> oracle =
        bruteForceParetoFront(g, model, oracleConstraints, a, cNode, 0.0);

    CHECK(oracle.empty());
    CHECK(search.isEmpty());
}

// ---------------------------------------------------------------------------
// Randomised cross-validation across several structurally distinct, deterministic graphs
// (Phase 5 brief §46).
// ---------------------------------------------------------------------------

ORCA_TEST(ParetoSearchOracle, AgreesOnRandomisedSmallGraphs) {
    const TransferModel model;
    const TransferConstraints c = permissive();

    struct Case {
        std::vector<double> altitudesKm;
        std::vector<double> phasesRad;
        std::vector<double> inclDeg;
        std::vector<std::pair<int, int>> edges;
    };

    const Case cases[] = {
        // Linear chain.
        {{500, 600, 700, 800}, {0.0, 0.3, 0.9, 1.7}, {0, 2, 5, 9}, {{0, 1}, {1, 2}, {2, 3}}},
        // Diamond: two disjoint 2-hop routes to the same target.
        {{500, 650, 900, 1400},
         {0.0, 1.1, 2.6, 0.4},
         {0, 4, 8, 12},
         {{0, 1}, {0, 2}, {1, 3}, {2, 3}}},
        // Star with one reachable branch.
        {{500, 700, 1000, 1300, 1600},
         {0.0, 0.5, 1.0, 1.5, 2.0},
         {0, 3, 6, 9, 12},
         {{0, 1}, {0, 2}, {0, 3}, {3, 4}}},
        // Back edge creating a cycle.
        {{500, 800, 1100, 1600},
         {0.0, 2.2, 0.7, 3.9},
         {0, 5, 10, 15},
         {{0, 1}, {1, 2}, {2, 3}, {2, 1}}},
        // Wider fan-in: multiple intermediates converging on the target.
        {{500, 620, 710, 900, 1200, 1500},
         {0.0, 0.8, 2.1, 3.5, 1.2, 4.4},
         {0, 4, 3, 9, 7, 14},
         {{0, 1}, {0, 2}, {1, 3}, {2, 3}, {1, 4}, {3, 5}, {4, 5}, {2, 5}, {3, 1}}},
    };

    int casesChecked = 0;
    for (const Case& tc : cases) {
        MissionGraph g;
        std::vector<NodeId> nodes;
        for (std::size_t i = 0; i < tc.altitudesKm.size(); ++i) {
            nodes.push_back(g.addNode(GraphNode{
                NodeId{0}, SatelliteId{static_cast<std::uint32_t>(i)},
                orbitAt(tc.altitudesKm[i], tc.phasesRad[i], degToRad(tc.inclDeg[i]))}));
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
        const ParetoSearchResult search =
            findParetoRoutes(g, model, source, target, 0.0, permissiveOptions());
        const std::vector<ObjectiveVector> oracle =
            bruteForceParetoFront(g, model, c, source, target, 0.0);

        CHECK(sameCostVectorSet(search.solutions, oracle));
        ++casesChecked;
    }

    CHECK_EQ(casesChecked, 5);
}
