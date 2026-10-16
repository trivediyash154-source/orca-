#include "Dijkstra.hpp"

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
    Seconds arrivalTime;
    NodeId node;
};

/// Min-heap on arrival time: `std::priority_queue` is a max-heap by default, so the
/// comparator is inverted.
struct EarlierArrivalFirst {
    [[nodiscard]] bool operator()(const QueueEntry& a, const QueueEntry& b) const noexcept {
        return a.arrivalTime > b.arrivalTime;
    }
};

/// Below this gap, two arrival times are treated as equal rather than one being a genuine
/// improvement over the other — guards against re-queuing on floating-point noise of the
/// kind TRANSFER_MODEL.md §13.3 measures at a few ulp. Seconds-scale absolute tolerance is
/// appropriate here (unlike the FIFO test's relative tolerance) because relaxation compares
/// two independently-computed arrival times of similar magnitude, not a value against
/// itself across a wide time range.
constexpr Seconds kRelaxationEpsilonSec = 1.0e-6;

}  // namespace

PathResult findEarliestArrival(const MissionGraph& graph,
                               const TransferModel& transferModel,
                               NodeId source,
                               NodeId target,
                               Seconds startTimeSec,
                               const SearchConstraints& searchConstraints) {
    requireThat(graph.containsNode(source),
                "findEarliestArrival: source is not a node in this graph");
    requireThat(graph.containsNode(target),
                "findEarliestArrival: target is not a node in this graph");

    SearchMetrics metrics;

    if (source == target) {
        return PathResult::trivial(source, startTimeSec, metrics);
    }

    const auto startedAt = std::chrono::steady_clock::now();

    // Every relaxation in this search shares one constraints object: the vehicle's total
    // budget as BOTH maxDeltaVKms and deltaVBudgetKms (never decremented — see Dijkstra.hpp
    // for why per-edge-against-total, not cumulative-against-remaining, is what single-label
    // search can soundly enforce), the real wait/deadline policy, and risk/distance left
    // unbounded because those were already decided permanently when the edge was built.
    TransferConstraints constraints;
    constraints.maxDeltaVKms = searchConstraints.vehicleDeltaVBudgetKms;
    constraints.deltaVBudgetKms = searchConstraints.vehicleDeltaVBudgetKms;
    constraints.availableFuelFraction = 1.0;
    constraints.maxWaitSec = searchConstraints.maxWaitSec;
    constraints.latestArrivalSec = searchConstraints.latestArrivalSec;
    constraints.maxRisk = searchConstraints.maxRisk;
    constraints.maxPathLengthKm = searchConstraints.maxPathLengthKm;

    const std::size_t n = graph.nodeCount();
    std::vector<Seconds> bestArrival(n, std::numeric_limits<Seconds>::infinity());
    std::vector<std::optional<PathLeg>> predecessor(n);
    std::vector<bool> finalised(n, false);

    std::priority_queue<QueueEntry, std::vector<QueueEntry>, EarlierArrivalFirst> open;

    bestArrival[source.value()] = startTimeSec;
    open.push(QueueEntry{startTimeSec, source});
    ++metrics.priorityQueuePushes;

    bool targetReached = false;

    while (!open.empty()) {
        const QueueEntry current = open.top();
        open.pop();
        ++metrics.priorityQueuePops;

        const std::size_t u = current.node.value();

        // Lazy deletion (ALGORITHMS.md §2.4): this entry was superseded by a better one
        // pushed later. Skip rather than maintain a decrease-key-capable heap.
        if (current.arrivalTime > bestArrival[u] + kRelaxationEpsilonSec) {
            ++metrics.staleEntriesDiscarded;
            continue;
        }
        if (finalised[u]) {
            ++metrics.staleEntriesDiscarded;
            continue;
        }
        finalised[u] = true;
        ++metrics.nodesExpanded;

        if (current.node == target) {
            targetReached = true;
            break;  // optimal: Dijkstra's invariant guarantees this label is final
        }

        const Orbit& fromOrbit = graph.node(current.node).orbit();

        for (const EdgeId edgeId : graph.outgoingEdges(current.node)) {
            const GraphEdge& e = graph.edge(edgeId);
            ++metrics.edgesEvaluated;

            const std::size_t v = e.to().value();
            if (finalised[v]) {
                continue;  // already optimal; no relaxation can improve a finalised node
            }

            // THE CRITICAL RULE: depart at the arrival time just reached, not at
            // startTimeSec. This is what makes the search time-dependent rather than a
            // static-weight shortest path wearing a time-dependent graph's clothes.
            const Orbit& toOrbit = graph.node(e.to()).orbit();
            const TransferResult result =
                transferModel.evaluate(fromOrbit, toOrbit, current.arrivalTime, constraints);

            if (!result.feasible) {
                ++metrics.relaxationsRejected;
                continue;
            }

            const Seconds candidateArrival = result.geometry.arrivalTimeSec;

            if (candidateArrival < bestArrival[v] - kRelaxationEpsilonSec) {
                bestArrival[v] = candidateArrival;
                predecessor[v] = PathLeg{edgeId, current.node, e.to(), result};
                open.push(QueueEntry{candidateArrival, e.to()});
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

    // Reconstruct by walking predecessors from target back to source, then reverse.
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
