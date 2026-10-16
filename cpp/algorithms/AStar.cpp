#include "AStar.hpp"

#include <algorithm>
#include <chrono>
#include <limits>
#include <optional>
#include <queue>
#include <vector>

#include "../core/Validation.hpp"

namespace orca {

namespace {

struct QueueEntry {
    Seconds f;
    Seconds g;
    NodeId node;
};

/// Priority order: smaller f first; ties broken by smaller g, then smaller NodeId
/// (Phase 4 brief §18, §25 — an explicit, deterministic policy, stated once here).
/// `std::priority_queue` is a max-heap, so the comparator is inverted throughout.
struct BetterCandidateFirst {
    [[nodiscard]] bool operator()(const QueueEntry& a, const QueueEntry& b) const noexcept {
        if (a.f != b.f) {
            return a.f > b.f;
        }
        if (a.g != b.g) {
            return a.g > b.g;
        }
        return a.node > b.node;
    }
};

/// Same role and value as Dijkstra.cpp's constant: floating-point noise on independently
/// computed arrival times is a few ulp (TRANSFER_MODEL.md §13.3), not a genuine tie or
/// improvement.
constexpr Seconds kRelaxationEpsilonSec = 1.0e-6;

}  // namespace

PathResult findEarliestArrivalAStar(const MissionGraph& graph,
                                    const TransferModel& transferModel,
                                    NodeId source,
                                    NodeId target,
                                    Seconds startTimeSec,
                                    const SearchConstraints& searchConstraints,
                                    AStarHeuristicFn heuristic,
                                    std::vector<AStarTraceStep>* trace) {
    requireThat(graph.containsNode(source),
                "findEarliestArrivalAStar: source is not a node in this graph");
    requireThat(graph.containsNode(target),
                "findEarliestArrivalAStar: target is not a node in this graph");

    SearchMetrics metrics;

    if (source == target) {
        return PathResult::trivial(source, startTimeSec, metrics);
    }

    const auto startedAt = std::chrono::steady_clock::now();

    // Same constraints construction as Dijkstra, for the same reasons (Dijkstra.hpp file
    // comment): total budget on every relaxation, real wait/deadline policy,
    // risk/distance left unbounded because GraphBuilder already decided those
    // permanently.
    TransferConstraints constraints;
    constraints.maxDeltaVKms = searchConstraints.vehicleDeltaVBudgetKms;
    constraints.deltaVBudgetKms = searchConstraints.vehicleDeltaVBudgetKms;
    constraints.availableFuelFraction = 1.0;
    constraints.maxWaitSec = searchConstraints.maxWaitSec;
    constraints.latestArrivalSec = searchConstraints.latestArrivalSec;
    constraints.maxRisk = searchConstraints.maxRisk;
    constraints.maxPathLengthKm = searchConstraints.maxPathLengthKm;

    const Orbit& goalOrbit = graph.node(target).orbit();

    const std::size_t n = graph.nodeCount();
    std::vector<Seconds> bestG(n, std::numeric_limits<Seconds>::infinity());
    std::vector<std::optional<PathLeg>> predecessor(n);

    std::priority_queue<QueueEntry, std::vector<QueueEntry>, BetterCandidateFirst> open;

    const Seconds hSource = heuristic(graph.node(source).orbit(), goalOrbit);
    ++metrics.heuristicEvaluations;

    bestG[source.value()] = startTimeSec;
    open.push(QueueEntry{startTimeSec + hSource, startTimeSec, source});
    ++metrics.priorityQueuePushes;

    bool targetReached = false;
    int expansionOrder = 0;

    while (!open.empty()) {
        const QueueEntry current = open.top();
        open.pop();
        ++metrics.priorityQueuePops;

        const std::size_t u = current.node.value();

        // Lazy deletion: this entry's g no longer matches the best known g for u, so a
        // better one has already superseded it. NOTE this is the only staleness test —
        // there is deliberately no permanent "finalised" flag (unlike Dijkstra.cpp): see
        // AStar.hpp's "REOPENING" section for why admissibility alone does not license
        // one here.
        if (current.g > bestG[u] + kRelaxationEpsilonSec) {
            ++metrics.staleEntriesDiscarded;
            continue;
        }

        ++metrics.nodesExpanded;

        if (trace != nullptr) {
            trace->push_back(AStarTraceStep{current.node, current.g, current.f - current.g,
                                            current.f, expansionOrder});
        }
        ++expansionOrder;

        if (current.node == target) {
            // Safe to trust on the very first pop, under admissibility alone (no
            // consistency needed) — see AStar.hpp's "WHY THE FIRST GOAL POP IS STILL
            // SAFE TO TRUST" for the full derivation.
            targetReached = true;
            break;
        }

        const Orbit& fromOrbit = graph.node(current.node).orbit();

        for (const EdgeId edgeId : graph.outgoingEdges(current.node)) {
            const GraphEdge& e = graph.edge(edgeId);
            ++metrics.edgesEvaluated;

            const std::size_t v = e.to().value();

            // THE CRITICAL RULE, same as Dijkstra: depart at the arrival time just
            // reached, never at startTimeSec.
            const Orbit& toOrbit = graph.node(e.to()).orbit();
            const TransferResult result =
                transferModel.evaluate(fromOrbit, toOrbit, current.g, constraints);

            if (!result.feasible) {
                ++metrics.relaxationsRejected;
                continue;
            }

            const Seconds candidateG = result.geometry.arrivalTimeSec;

            if (candidateG < bestG[v] - kRelaxationEpsilonSec) {
                bestG[v] = candidateG;
                predecessor[v] = PathLeg{edgeId, current.node, e.to(), result};

                const Seconds hV = heuristic(toOrbit, goalOrbit);
                ++metrics.heuristicEvaluations;

                open.push(QueueEntry{candidateG + hV, candidateG, e.to()});
                ++metrics.priorityQueuePushes;
                ++metrics.relaxationsSucceeded;
            } else {
                ++metrics.relaxationsRejected;
            }
        }
    }

    const auto finishedAt = std::chrono::steady_clock::now();
    metrics.executionTimeMs =
        std::chrono::duration<double, std::milli>(finishedAt - startedAt).count();

    if (!targetReached) {
        return PathResult::notFound(source, target, startTimeSec, metrics);
    }

    std::vector<PathLeg> legs;
    NodeId walk = target;
    while (walk != source) {
        const PathLeg& leg = *predecessor[walk.value()];
        legs.push_back(leg);
        walk = leg.from;
    }
    std::reverse(legs.begin(), legs.end());

    return PathResult::found(source, target, startTimeSec, std::move(legs), metrics);
}

}  // namespace orca
