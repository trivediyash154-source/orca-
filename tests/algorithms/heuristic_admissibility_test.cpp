/// Independent verification that `astar_heuristic::estimateRemainingTimeSec` never
/// overestimates the true remaining cost -- the admissibility property A*'s correctness
/// depends on (AStar.hpp's termination proof requires it, nothing weaker).
///
/// "Independent" here means: never calls `findEarliestArrivalAStar`, and for the deepest
/// check (the brute-force graph oracle) never calls `findEarliestArrival` (Dijkstra)
/// either -- path enumeration and minimum-selection are written fresh in this file, the
/// same discipline dijkstra_oracle_test.cpp uses to verify Dijkstra itself. Three
/// independent angles, from broadest/cheapest to narrowest/deepest:
///
///  1. AdmissibleAgainstDirectTransferAcrossRealisticGrid -- h(n) <= a real single real
///     transfer's own remaining time, swept across a wide altitude grid. Necessary but not
///     sufficient on its own (a multi-hop route could in principle beat direct), included
///     for broad, cheap coverage.
///  2. AdmissibleAgainstBruteForceMultiHopGraphOracle -- h(n) <= the true minimum over
///     *every* simple path in real, multi-node graphs, including ones deliberately routed
///     near Earth's surface to stress the floor-detour term specifically
///     (AStarHeuristic.hpp §4). This is the deep check: it cannot be fooled by a
///     multi-hop detour the direct-only check in (1) would miss.
///  3. FloorDetourMonotonicityLemma -- independently re-derives, by direct numerical sweep
///     against the real `hohmann::compute()` (not the heuristic), the specific
///     mathematical claim the floor-detour term's admissibility rests on: that
///     `coast(ra,x) + coast(x,rb)` is minimised at the smallest physically valid `x`. This
///     is the load-bearing lemma from AStarHeuristic.hpp §4, checked as a fact about the
///     transfer model rather than assumed from the header's prose.
///
/// Every numeric expectation below comes from compiling and running the real
/// `TransferModel` / `hohmann::compute` -- none are hand-derived or copied from the
/// scratch exploration that originally found this formula.

#include <algorithm>
#include <cmath>
#include <vector>

#include "../../cpp/algorithms/AStarHeuristic.hpp"
#include "../../cpp/core/Constants.hpp"
#include "../../cpp/graph/MissionGraph.hpp"
#include "../../cpp/transfer/HohmannTransfer.hpp"
#include "../../cpp/transfer/TransferModel.hpp"
#include "../TestFramework.hpp"

using namespace orca;
using namespace orca::astar_heuristic;

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

// ---- Brute-force multi-hop oracle: independent of Dijkstra, A*, and the heuristic. -----
// Structurally the same enumeration dijkstra_oracle_test.cpp uses, rewritten here rather
// than shared, matching that file's own reasoning for writing it fresh: it is what makes
// the comparison meaningful.

struct OracleResult {
    bool found = false;
    Seconds arrivalTimeSec = 0.0;
};

void enumerateSimplePaths(const MissionGraph& graph, const TransferModel& model,
                          const TransferConstraints& constraints, NodeId current,
                          NodeId target, Seconds arrivedAt, std::vector<NodeId>& visited,
                          OracleResult& best) {
    if (current == target) {
        if (!best.found || arrivedAt < best.arrivalTimeSec) {
            best.found = true;
            best.arrivalTimeSec = arrivedAt;
        }
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
                             result.geometry.arrivalTimeSec, visited, best);
        visited.pop_back();
    }
}

[[nodiscard]] OracleResult bruteForceEarliestArrival(const MissionGraph& graph,
                                                     const TransferModel& model,
                                                     const TransferConstraints& constraints,
                                                     NodeId source, NodeId target,
                                                     Seconds startTimeSec) {
    OracleResult best;
    std::vector<NodeId> visited{source};
    enumerateSimplePaths(graph, model, constraints, source, target, startTimeSec, visited,
                        best);
    return best;
}

}  // namespace

// ---------------------------------------------------------------------------
// 1. Broad, cheap sweep against a single real transfer.
// ---------------------------------------------------------------------------

ORCA_TEST(HeuristicAdmissibility, AdmissibleAgainstDirectTransferAcrossRealisticGrid) {
    const TransferModel model;
    const TransferConstraints c = permissive();
    const double altitudesKm[] = {200.0, 300.0, 500.0, 800.0, 1200.0,
                                  2000.0, 5000.0, 12000.0, 20000.0, 35786.0};
    int pairsChecked = 0;
    for (double a : altitudesKm) {
        for (double b : altitudesKm) {
            const Orbit orbitA = orbitAt(a, 0.0);
            const Orbit orbitB = orbitAt(b, 1.7);
            const TransferResult direct = model.evaluate(orbitA, orbitB, 0.0, c);
            if (!direct.feasible) {
                continue;
            }
            const Seconds h = estimateRemainingTimeSec(orbitA, orbitB);
            const Seconds trueRemaining = direct.geometry.arrivalTimeSec;  // started at t=0
            CHECK(h <= trueRemaining + 1e-6);
            ++pairsChecked;
        }
    }
    // Sanity on the sweep itself: most of a 10x10 grid should have produced a feasible
    // direct transfer, or this test would be silently checking almost nothing.
    CHECK(pairsChecked >= 80);
}

// ---------------------------------------------------------------------------
// 2. Deep check: real multi-node graphs, brute-force minimum over every simple path,
// including nodes deliberately placed near Earth's surface.
// ---------------------------------------------------------------------------

ORCA_TEST(HeuristicAdmissibility, AdmissibleAgainstBruteForceMultiHopGraphOracle) {
    const TransferModel model;
    const TransferConstraints c = permissive();

    struct Case {
        std::vector<double> altitudesKm;
        std::vector<double> phasesRad;
        std::vector<std::pair<int, int>> edges;
    };

    const Case cases[] = {
        // Ordinary realistic-range graph, diamond topology.
        {{500, 650, 900, 1400},
         {0.0, 1.1, 2.6, 0.4},
         {{0, 1}, {0, 2}, {1, 3}, {2, 3}}},
        // Deliberately includes very low "altitudes" (a few km above Earth's surface --
        // not a realistic stable orbit, but a legal Orbit -- and one very high node) to
        // stress the floor-detour term where it matters most: near the floor itself.
        {{2.0, 50.0, 500.0, 3000.0, 40000.0},
         {0.0, 0.9, 1.8, 2.7, 3.6},
         {{0, 1}, {1, 2}, {2, 3}, {3, 4}, {0, 4}, {1, 3}}},
        // A star with a long detour: only one route reaches the last node, forcing the
        // oracle to actually search rather than trivially finding a direct edge.
        {{500, 700, 1000, 1300, 1600, 2200},
         {0.0, 0.5, 1.0, 1.5, 2.0, 2.5},
         {{0, 1}, {0, 2}, {0, 3}, {3, 4}, {4, 5}}},
        // A cycle back toward the source side, plus an isolated low-altitude node with no
        // path to the rest -- exercises the "no oracle value to bound against" case
        // alongside ordinary reachable ones in the same graph.
        {{500, 800, 1100, 1600, 10.0},
         {0.0, 2.2, 0.7, 3.9, 1.0},
         {{0, 1}, {1, 2}, {2, 3}, {2, 1}}},
    };

    int reachablePairsChecked = 0;
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

        // Check every node as a potential source against every other node as target --
        // not just the designed source/target -- for maximum coverage per graph.
        for (std::size_t si = 0; si < nodes.size(); ++si) {
            for (std::size_t ti = 0; ti < nodes.size(); ++ti) {
                if (si == ti) {
                    continue;
                }
                const NodeId source = nodes[si];
                const NodeId target = nodes[ti];
                const OracleResult oracle =
                    bruteForceEarliestArrival(g, model, c, source, target, 0.0);
                if (!oracle.found) {
                    continue;  // no true cost to bound against
                }
                const Seconds h = estimateRemainingTimeSec(g.node(source).orbit(),
                                                            g.node(target).orbit());
                CHECK(h <= oracle.arrivalTimeSec + 1e-6);
                ++reachablePairsChecked;
            }
        }
    }

    // Confirms the sweep actually exercised a meaningful number of reachable pairs across
    // the four graphs, not merely iterating over disconnected ones.
    CHECK(reachablePairsChecked >= 20);
}

// ---------------------------------------------------------------------------
// 3. The load-bearing lemma itself: coast(ra,x) + coast(x,rb) is minimised at the smallest
// physically valid x. Re-derived numerically here against the real hohmann::compute(),
// independent of AStarHeuristic.cpp's own implementation.
// ---------------------------------------------------------------------------

ORCA_TEST(HeuristicAdmissibility, FloorDetourMonotonicityLemma) {
    // (source radius, target radius) pairs spanning ORCA's realistic range plus one very
    // wide pair, all well clear of kEarthRadiusKm so the same-radius special case in
    // AStarHeuristic.cpp's private coastSec() (not used here) cannot interfere with this
    // sweep of the smooth formula.
    struct RadiusPair {
        Kilometres ra;
        Kilometres rb;
    };
    const RadiusPair pairs[] = {
        {kEarthRadiusKm + 500.0, kEarthRadiusKm + 1200.0},
        {kEarthRadiusKm + 300.0, kEarthRadiusKm + 35786.0},
        {kEarthRadiusKm + 800.0, kEarthRadiusKm + 900.0},
        {kEarthRadiusKm + 200.0, kEarthRadiusKm + 42164.0},
    };

    for (const RadiusPair& p : pairs) {
        const Seconds floorValue = hohmann::compute(p.ra, kEarthRadiusKm, 0.0).coastTimeSec +
                                   hohmann::compute(kEarthRadiusKm, p.rb, 0.0).coastTimeSec;

        // Sweep x from just above the floor up to well beyond both radii; every sampled x
        // must give a total >= the floor value, confirming the floor is the minimum, not
        // merely *a* low point, across a fine grid rather than a handful of spot checks.
        bool anyViolation = false;
        Kilometres worstX = 0.0;
        Seconds worstMargin = 0.0;
        for (int i = 0; i <= 500; ++i) {
            const Kilometres x =
                kEarthRadiusKm + static_cast<double>(i) * (100000.0 / 500.0);
            const Seconds total = hohmann::compute(p.ra, x, 0.0).coastTimeSec +
                                 hohmann::compute(x, p.rb, 0.0).coastTimeSec;
            const Seconds margin = total - floorValue;
            if (margin < -1e-6) {
                anyViolation = true;
                if (margin < worstMargin) {
                    worstMargin = margin;
                    worstX = x;
                }
            }
        }
        CHECK(!anyViolation);
        (void)worstX;  // retained for debugging if the assertion above ever fails
    }
}
