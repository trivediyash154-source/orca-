#pragma once
// Shared helpers for the ORCA evaluation harnesses (E0 to E4).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "algorithms/AStar.hpp"
#include "algorithms/Dijkstra.hpp"
#include "algorithms/Dominance.hpp"
#include "algorithms/NamoaStar.hpp"
#include "algorithms/ParetoSearch.hpp"
#include "graph/GraphBuilder.hpp"
#include "optimization/CandidateGeneration.hpp"
#include "simulation/ScenarioGenerator.hpp"
#include "transfer/TransferModel.hpp"

namespace exp_common {
using namespace orca;

// highDebris is omitted on purpose: debris does not enter graph construction or edge
// costs, so its search graphs are identical to `normal` (verified on all 162 benchmark rows).
inline const std::vector<std::pair<std::string, ScenarioGeneratorConfig (*)(int)>>& families() {
    static const std::vector<std::pair<std::string, ScenarioGeneratorConfig (*)(int)>> f = {
        {"sparse", scenario_family::sparse},
        {"normal", scenario_family::normal},
        {"dense", scenario_family::dense},
        {"fuelConstrained", scenario_family::fuelConstrained},
        {"tradeoffRich", scenario_family::tradeoffRich},
    };
    return f;
}

inline TransferConstraints toTransferConstraints(const SearchConstraints& s) {
    TransferConstraints tc;
    tc.maxDeltaVKms = s.vehicleDeltaVBudgetKms;
    tc.deltaVBudgetKms = s.vehicleDeltaVBudgetKms;
    tc.availableFuelFraction = 1.0;
    tc.maxWaitSec = s.maxWaitSec;
    tc.latestArrivalSec = s.latestArrivalSec;
    tc.maxRisk = s.maxRisk;
    tc.maxPathLengthKm = s.maxPathLengthKm;
    return tc;
}

// Budget-aware single-label earliest arrival: Dijkstra that refuses any relaxation whose
// cumulative delta-v exceeds the budget. This is the natural "fix" a reader would try,
// and it is incomplete: the single surviving label per node may be the thirsty one.
struct RcResult {
    bool found = false;
    double arrival = 0.0;
    ObjectiveVector cost;
};
inline RcResult budgetPrunedDijkstra(const MissionGraph& g, const TransferModel& m, NodeId s,
                                     NodeId t, Seconds start, const SearchConstraints& sc) {
    const TransferConstraints tc = toTransferConstraints(sc);
    const std::size_t n = g.nodeCount();
    std::vector<double> best(n, 1e300);
    std::vector<ObjectiveVector> cost(n);
    std::vector<char> settled(n, 0);
    using QE = std::pair<double, std::uint32_t>;
    std::vector<QE> heap;
    auto cmp = [](const QE& a, const QE& b) { return a > b; };
    best[s.value()] = start;
    heap.push_back({start, s.value()});
    while (!heap.empty()) {
        std::pop_heap(heap.begin(), heap.end(), cmp);
        const auto [time, u] = heap.back();
        heap.pop_back();
        if (settled[u] || time > best[u]) continue;
        settled[u] = 1;
        if (u == t.value()) {
            return RcResult{true, time, cost[u]};
        }
        const NodeId un(u);
        const Orbit& from = g.node(un).orbit();
        for (EdgeId eid : g.outgoingEdges(un)) {
            const GraphEdge& e = g.edge(eid);
            const TransferResult r = m.evaluate(from, g.node(e.to()).orbit(), time, tc);
            if (!r.feasible) continue;
            const ObjectiveVector c2 = compose(cost[u], r.objectives());
            if (c2.fuel > 1.0 + kDominanceNoiseEpsilon) continue;
            const double arr = r.geometry.arrivalTimeSec;
            if (arr < best[e.to().value()]) {
                best[e.to().value()] = arr;
                cost[e.to().value()] = c2;
                heap.push_back({arr, e.to().value()});
                std::push_heap(heap.begin(), heap.end(), cmp);
            }
        }
    }
    return RcResult{};
}

inline std::vector<ObjectiveVector> costsOf(const std::vector<ParetoRoute>& routes) {
    std::vector<ObjectiveVector> c;
    for (const auto& r : routes) c.push_back(r.totalCost);
    std::sort(c.begin(), c.end(), [](const ObjectiveVector& a, const ObjectiveVector& b) {
        return std::tie(a.time, a.fuel, a.risk, a.distance) <
               std::tie(b.time, b.fuel, b.risk, b.distance);
    });
    return c;
}

inline bool sameFront(std::vector<ObjectiveVector> a, std::vector<ObjectiveVector> b) {
    if (a.size() != b.size()) return false;
    std::vector<char> used(b.size(), 0);
    for (const auto& x : a) {
        bool hit = false;
        for (std::size_t j = 0; j < b.size(); ++j) {
            if (!used[j] && isEquivalent(x, b[j])) {
                used[j] = 1;
                hit = true;
                break;
            }
        }
        if (!hit) return false;
    }
    return true;
}

inline std::vector<ObjectiveVector> nonDominated(const std::vector<ObjectiveVector>& all) {
    std::vector<ObjectiveVector> out;
    for (std::size_t i = 0; i < all.size(); ++i) {
        bool dom = false;
        for (std::size_t j = 0; j < all.size() && !dom; ++j) {
            if (i != j && dominates(all[j], all[i])) dom = true;
        }
        if (dom) continue;
        bool dup = false;
        for (const auto& o : out) {
            if (isEquivalent(o, all[i])) {
                dup = true;
                break;
            }
        }
        if (!dup) out.push_back(all[i]);
    }
    return out;
}

}  // namespace exp_common
