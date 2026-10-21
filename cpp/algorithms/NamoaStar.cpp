#include "NamoaStar.hpp"

#include <chrono>
#include <cmath>
#include <optional>
#include <queue>
#include <tuple>
#include <vector>

#include "../core/Validation.hpp"
#include "AStarHeuristic.hpp"
#include "Dominance.hpp"

namespace orca {

namespace {

struct Label {
    NodeId node;
    ObjectiveVector g;
    Seconds arrivalTimeSec = 0.0;
    std::optional<std::size_t> predecessor;
    std::optional<EdgeId> viaEdge;
    std::optional<TransferResult> legResult;
    bool open = true;   ///< still waiting to be expanded
    bool alive = true;  ///< not superseded or filtered
};

struct Entry {
    ObjectiveVector f;
    std::size_t label;
};

/// Lexicographic order on f, then creation order. std::priority_queue is a max-heap, so
/// "less" here means "lower priority".
struct LexGreater {
    [[nodiscard]] bool operator()(const Entry& a, const Entry& b) const noexcept {
        const auto ka = std::make_tuple(a.f.time, a.f.fuel, a.f.risk, a.f.distance, a.label);
        const auto kb = std::make_tuple(b.f.time, b.f.fuel, b.f.risk, b.f.distance, b.label);
        return ka > kb;
    }
};

[[nodiscard]] bool dominatesOrEquals(const ObjectiveVector& a, const ObjectiveVector& b) noexcept {
    return dominates(a, b) || isEquivalent(a, b);
}

ParetoRoute rebuild(std::size_t id, const std::vector<Label>& pool) {
    ParetoRoute route;
    route.totalCost = pool[id].g;
    route.arrivalTimeSec = pool[id].arrivalTimeSec;
    std::vector<PathLeg> reversed;
    std::size_t cur = id;
    while (pool[cur].predecessor.has_value()) {
        const std::size_t prev = *pool[cur].predecessor;
        reversed.push_back(PathLeg{*pool[cur].viaEdge, pool[prev].node, pool[cur].node,
                                   *pool[cur].legResult});
        cur = prev;
    }
    route.legs.assign(reversed.rbegin(), reversed.rend());
    return route;
}

}  // namespace

NamoaResult findParetoRoutesNamoa(const MissionGraph& graph, const TransferModel& transferModel,
                                  NodeId source, NodeId target, Seconds startTimeSec,
                                  const SearchConstraints& searchConstraints, int maxLabels) {
    requireThat(graph.containsNode(source), "findParetoRoutesNamoa: source not in graph");
    requireThat(graph.containsNode(target), "findParetoRoutesNamoa: target not in graph");

    NamoaResult out;
    if (source == target) {
        ParetoRoute trivial;
        trivial.arrivalTimeSec = startTimeSec;
        out.solutions.push_back(trivial);
        out.metrics.targetLabelCount = 1;
        return out;
    }

    const auto startedAt = std::chrono::steady_clock::now();

    TransferConstraints tc;
    tc.maxDeltaVKms = searchConstraints.vehicleDeltaVBudgetKms;
    tc.deltaVBudgetKms = searchConstraints.vehicleDeltaVBudgetKms;
    tc.availableFuelFraction = 1.0;
    tc.maxWaitSec = searchConstraints.maxWaitSec;
    tc.latestArrivalSec = searchConstraints.latestArrivalSec;
    tc.maxRisk = searchConstraints.maxRisk;
    tc.maxPathLengthKm = searchConstraints.maxPathLengthKm;

    const std::size_t n = graph.nodeCount();
    const Orbit& goalOrbit = graph.node(target).orbit();
    const Kilometres goalRadius = goalOrbit.semiMajorAxisKm();

    // h is a pure function of the node, so it is evaluated at most once per node.
    std::vector<std::optional<std::pair<Seconds, Kilometres>>> hCache(n);
    auto heuristic = [&](NodeId node) -> std::pair<Seconds, Kilometres> {
        auto& slot = hCache[node.value()];
        if (!slot.has_value()) {
            ++out.metrics.heuristicEvaluations;
            const Orbit& o = graph.node(node).orbit();
            slot = std::make_pair(astar_heuristic::estimateRemainingTimeSec(o, goalOrbit),
                                  std::fabs(o.semiMajorAxisKm() - goalRadius));
        }
        return *slot;
    };
    auto fOf = [&](const ObjectiveVector& g, NodeId node) {
        const auto [ht, hd] = heuristic(node);
        return ObjectiveVector{g.time + ht, g.fuel, g.risk, g.distance + hd};
    };

    std::vector<Label> pool;
    std::vector<std::vector<std::size_t>> liveAtNode(n);
    std::vector<std::size_t> solutionIds;  // live target labels (COSTS)
    std::priority_queue<Entry, std::vector<Entry>, LexGreater> open;

    pool.push_back(Label{.node = source, .g = ObjectiveVector{}, .arrivalTimeSec = startTimeSec});
    liveAtNode[source.value()].push_back(0);
    open.push(Entry{fOf(ObjectiveVector{}, source), 0});
    ++out.metrics.labelsInserted;

    auto filteredBySolutions = [&](const ObjectiveVector& f) {
        for (std::size_t sid : solutionIds) {
            ++out.metrics.dominanceComparisons;
            if (dominatesOrEquals(pool[sid].g, f)) {
                return true;
            }
        }
        return false;
    };

    while (!open.empty()) {
        if (static_cast<int>(pool.size()) >= maxLabels) {
            out.metrics.truncated = true;
            break;
        }
        const Entry entry = open.top();
        open.pop();
        Label& popped = pool[entry.label];
        if (!popped.alive || !popped.open) {
            continue;
        }
        if (filteredBySolutions(entry.f)) {
            popped.alive = false;
            ++out.metrics.labelsFilteredBySolutions;
            continue;
        }
        popped.open = false;
        ++out.metrics.labelsExpanded;

        if (popped.node == target) {
            solutionIds.push_back(entry.label);
            continue;
        }

        const Label current = popped;  // copy: pool may reallocate below
        const Orbit& fromOrbit = graph.node(current.node).orbit();
        for (const EdgeId edgeId : graph.outgoingEdges(current.node)) {
            const GraphEdge& e = graph.edge(edgeId);
            const TransferResult result = transferModel.evaluate(
                fromOrbit, graph.node(e.to()).orbit(), current.arrivalTimeSec, tc);
            if (!result.feasible) {
                continue;
            }
            const ObjectiveVector g2 = compose(current.g, result.objectives());
            if (g2.fuel > 1.0 + kDominanceNoiseEpsilon) {
                continue;  // cumulative delta-v budget, identical to findParetoRoutes
            }
            ++out.metrics.labelsGenerated;

            const ObjectiveVector f2 = fOf(g2, e.to());
            if (filteredBySolutions(f2)) {
                ++out.metrics.labelsFilteredBySolutions;
                continue;
            }

            std::vector<std::size_t>& live = liveAtNode[e.to().value()];
            bool rejected = false;
            for (std::size_t other : live) {
                ++out.metrics.dominanceComparisons;
                if (dominatesOrEquals(pool[other].g, g2)) {
                    rejected = true;
                    break;
                }
            }
            if (rejected) {
                ++out.metrics.labelsDominatedAtNode;
                continue;
            }

            std::vector<std::size_t> survivors;
            survivors.reserve(live.size() + 1);
            for (std::size_t other : live) {
                ++out.metrics.dominanceComparisons;
                if (dominates(g2, pool[other].g)) {
                    pool[other].alive = false;
                } else {
                    survivors.push_back(other);
                }
            }
            const std::size_t newId = pool.size();
            pool.push_back(Label{.node = e.to(),
                                 .g = g2,
                                 .arrivalTimeSec = result.geometry.arrivalTimeSec,
                                 .predecessor = entry.label,
                                 .viaEdge = edgeId,
                                 .legResult = result});
            survivors.push_back(newId);
            live = std::move(survivors);
            open.push(Entry{f2, newId});
            ++out.metrics.labelsInserted;
        }
    }

    // Final non-dominated filter over the target labels (a no-op with a consistent
    // heuristic, kept so the result is correct even if consistency were ever lost).
    std::vector<std::size_t> finalIds;
    for (std::size_t a : solutionIds) {
        if (!pool[a].alive) {
            continue;
        }
        bool dominated = false;
        for (std::size_t b : solutionIds) {
            if (a != b && pool[b].alive && dominates(pool[b].g, pool[a].g)) {
                dominated = true;
                break;
            }
        }
        if (!dominated) {
            finalIds.push_back(a);
        }
    }
    for (std::size_t id : finalIds) {
        out.solutions.push_back(rebuild(id, pool));
    }
    out.metrics.targetLabelCount = static_cast<int>(out.solutions.size());
    out.metrics.executionTimeMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - startedAt)
            .count();
    return out;
}

}  // namespace orca
