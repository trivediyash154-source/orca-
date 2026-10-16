#include "ParetoSearch.hpp"

#include <algorithm>
#include <chrono>
#include <queue>
#include <utility>

#include "../core/Validation.hpp"
#include "Dominance.hpp"
#include "ParetoLabel.hpp"

namespace orca {

namespace {

struct QueueEntry {
    Seconds arrivalTimeSec;
    LabelId label;
};

/// Priority order: earliest arrival time first (the same, simplest-correct processing
/// policy Dijkstra/A* already use), tied broken by smaller `LabelId` — which reflects
/// creation order, itself deterministic. This is a PROCESSING order only: it decides which
/// retained label is expanded next, never which labels survive dominance. See
/// `ParetoSearch.hpp`'s file comment and Phase 5 brief §17.
struct EarlierArrivalFirst {
    [[nodiscard]] bool operator()(const QueueEntry& a, const QueueEntry& b) const noexcept {
        if (a.arrivalTimeSec != b.arrivalTimeSec) {
            return a.arrivalTimeSec > b.arrivalTimeSec;
        }
        return a.label.value() > b.label.value();
    }
};

enum class InsertOutcome { Inserted, DiscardedDominated, DiscardedDuplicate };

/// `insertedId` is set only when `outcome == Inserted` — returning the actual pool index
/// the label was assigned, rather than making the caller infer it from `pool.size()`
/// before the call. That inference would happen to be correct today (the only push_back on
/// the Inserted path is the final one), but is exactly the kind of implicit invariant a
/// later edit to this function could silently break; returning the real ID removes the
/// assumption entirely.
struct InsertResult {
    InsertOutcome outcome;
    std::optional<LabelId> insertedId;
};

/// Implements ALGORITHMS.md §5.4's four-step dominance update, generalised into one
/// reusable routine: (1) discard `candidate` if an existing label at its node dominates
/// it; (2) discard it if it is equivalent (a duplicate) to an existing label, keeping
/// whichever arrived first; (3) otherwise remove every existing label `candidate`
/// dominates; (4) insert `candidate`.
///
/// When `candidate.node == target`, every label that never becomes (or stops being) part
/// of `target`'s non-dominated set is still recorded in `dominatedAtTargetIds` — including
/// `candidate` itself, if discarded — purely for later visualisation
/// (`ParetoSearchResult::dominated`, `OPTIMIZATION_MODEL.md` §4.2). For every other node,
/// discarded candidates are simply never inserted into the pool at all.
InsertResult insertCandidate(ParetoLabel candidate, NodeId target,
                             std::vector<std::vector<LabelId>>& labelsAtNode,
                             std::vector<ParetoLabel>& pool, ParetoSearchMetrics& metrics,
                             const ParetoSearchOptions& options,
                             std::vector<LabelId>& dominatedAtTargetIds) {
    const bool isTarget = (candidate.node == target);
    const bool useEpsilon = options.epsilonDominance.has_value();
    std::vector<LabelId>& existing = labelsAtNode[candidate.node.value()];

    for (LabelId existingId : existing) {
        const ParetoLabel& existingLabel = pool[existingId.value()];

        ++metrics.dominanceComparisons;
        const bool existingDominatesCandidate =
            useEpsilon ? dominatesWithEpsilon(existingLabel.cost, candidate.cost,
                                              *options.epsilonDominance)
                       : dominates(existingLabel.cost, candidate.cost);
        if (existingDominatesCandidate) {
            ++metrics.labelsDominated;
            if (isTarget) {
                const auto candidateId = LabelId(static_cast<LabelId::Value>(pool.size()));
                pool.push_back(std::move(candidate));
                dominatedAtTargetIds.push_back(candidateId);
            }
            return InsertResult{InsertOutcome::DiscardedDominated, std::nullopt};
        }

        ++metrics.dominanceComparisons;
        if (isEquivalent(existingLabel.cost, candidate.cost)) {
            ++metrics.labelsDuplicate;
            if (isTarget) {
                const auto candidateId = LabelId(static_cast<LabelId::Value>(pool.size()));
                pool.push_back(std::move(candidate));
                dominatedAtTargetIds.push_back(candidateId);
            }
            return InsertResult{InsertOutcome::DiscardedDuplicate, std::nullopt};
        }
    }

    // Candidate survives: remove everything it dominates (Step 3), then insert (Step 4).
    std::vector<LabelId> survivors;
    survivors.reserve(existing.size() + 1);
    for (LabelId existingId : existing) {
        ParetoLabel& existingLabel = pool[existingId.value()];
        ++metrics.dominanceComparisons;
        const bool candidateDominatesExisting =
            useEpsilon ? dominatesWithEpsilon(candidate.cost, existingLabel.cost,
                                              *options.epsilonDominance)
                       : dominates(candidate.cost, existingLabel.cost);
        if (candidateDominatesExisting) {
            existingLabel.active = false;
            ++metrics.labelsSuperseded;
            if (isTarget) {
                dominatedAtTargetIds.push_back(existingId);
            }
        } else {
            survivors.push_back(existingId);
        }
    }

    const auto newId = LabelId(static_cast<LabelId::Value>(pool.size()));
    pool.push_back(std::move(candidate));
    survivors.push_back(newId);
    metrics.maximumLabelsAtNode =
        std::max(metrics.maximumLabelsAtNode, static_cast<int>(survivors.size()));
    ++metrics.labelsInserted;
    existing = std::move(survivors);
    return InsertResult{InsertOutcome::Inserted, newId};
}

/// Walks a label's predecessor chain back to the root, producing the same `PathLeg`
/// sequence Dijkstra/A* would for this specific route.
ParetoRoute reconstructRoute(LabelId labelId, const std::vector<ParetoLabel>& pool) {
    ParetoRoute route;
    const ParetoLabel* current = &pool[labelId.value()];
    route.totalCost = current->cost;
    route.arrivalTimeSec = current->arrivalTimeSec;

    std::vector<PathLeg> legsReversed;
    while (current->predecessor.has_value()) {
        legsReversed.push_back(PathLeg{*current->viaEdge, pool[current->predecessor->value()].node,
                                       current->node, *current->legResult});
        current = &pool[current->predecessor->value()];
    }
    route.legs.assign(legsReversed.rbegin(), legsReversed.rend());
    return route;
}

}  // namespace

ParetoSearchResult findParetoRoutes(const MissionGraph& graph,
                                    const TransferModel& transferModel, NodeId source,
                                    NodeId target, Seconds startTimeSec,
                                    const SearchConstraints& searchConstraints,
                                    const ParetoSearchOptions& options) {
    requireThat(graph.containsNode(source),
                "findParetoRoutes: source is not a node in this graph");
    requireThat(graph.containsNode(target),
                "findParetoRoutes: target is not a node in this graph");

    ParetoSearchResult out{.source = source, .target = target, .startTimeSec = startTimeSec};

    if (source == target) {
        // The trivial zero-leg route, mirroring Dijkstra/A*'s trivial() case: reachable at
        // the requested start time with no cost, and no search needed.
        ParetoRoute trivial;
        trivial.totalCost = ObjectiveVector{};
        trivial.arrivalTimeSec = startTimeSec;
        out.solutions.push_back(std::move(trivial));
        out.metrics.targetLabelCount = 1;
        return out;
    }

    const auto startedAt = std::chrono::steady_clock::now();

    TransferConstraints transferConstraints;
    transferConstraints.maxDeltaVKms = searchConstraints.vehicleDeltaVBudgetKms;
    transferConstraints.deltaVBudgetKms = searchConstraints.vehicleDeltaVBudgetKms;
    transferConstraints.availableFuelFraction = 1.0;
    transferConstraints.maxWaitSec = searchConstraints.maxWaitSec;
    transferConstraints.latestArrivalSec = searchConstraints.latestArrivalSec;
    transferConstraints.maxRisk = searchConstraints.maxRisk;
    transferConstraints.maxPathLengthKm = searchConstraints.maxPathLengthKm;

    const std::size_t n = graph.nodeCount();
    std::vector<std::vector<LabelId>> labelsAtNode(n);
    std::vector<ParetoLabel> pool;
    std::vector<LabelId> dominatedAtTargetIds;

    std::priority_queue<QueueEntry, std::vector<QueueEntry>, EarlierArrivalFirst> open;

    ParetoLabel root{.node = source, .cost = ObjectiveVector{}, .arrivalTimeSec = startTimeSec};
    pool.push_back(root);
    const LabelId rootId{0};
    labelsAtNode[source.value()].push_back(rootId);
    open.push(QueueEntry{startTimeSec, rootId});
    ++out.metrics.queuePushes;
    ++out.metrics.labelsInserted;
    out.metrics.maximumLabelsAtNode = 1;

    while (!open.empty()) {
        if (static_cast<int>(pool.size()) >= options.maxLabels) {
            out.metrics.truncated = true;
            break;
        }

        const QueueEntry entry = open.top();
        open.pop();
        ++out.metrics.queuePops;

        // Lazy deletion: this label may have been superseded (deactivated) by a later
        // dominance update after being pushed but before being popped. Not "reopening" --
        // see ParetoSearch.hpp's file comment.
        if (!pool[entry.label.value()].active) {
            continue;
        }
        ++out.metrics.labelsExpanded;

        // The label that was current when this was pushed is stable to read here (no
        // insertion has happened between push and pop that would move pool's storage
        // location this iteration cares about being consistent -- pool grows but this
        // reference is re-taken fresh here, after any growth from earlier iterations).
        const ParetoLabel current = pool[entry.label.value()];

        // Never expand outgoing edges of the target itself: this search wants routes TO
        // target, and continuing past it does not produce a "route to target."
        if (current.node == target) {
            continue;
        }

        const Orbit& fromOrbit = graph.node(current.node).orbit();
        for (const EdgeId edgeId : graph.outgoingEdges(current.node)) {
            const GraphEdge& e = graph.edge(edgeId);
            const Orbit& toOrbit = graph.node(e.to()).orbit();

            // THE CRITICAL RULE, identical to Dijkstra/A*: depart at the arrival time this
            // label actually reached, never at startTimeSec (GRAPH_MODEL.md §6.1).
            const TransferResult result =
                transferModel.evaluate(fromOrbit, toOrbit, current.arrivalTimeSec,
                                       transferConstraints);
            if (!result.feasible) {
                continue;  // per-edge (Group 2/3) infeasibility, same as Dijkstra/A*
            }

            const ObjectiveVector candidateCost = compose(current.cost, result.objectives());

            // THE CUMULATIVE DELTA-V HARD CONSTRAINT (ParetoSearch.hpp's file comment):
            // candidateCost.fuel is the accumulated delta-v over the vehicle's total
            // budget across every leg so far, including this one -- reject outright if it
            // would exceed the budget, regardless of how it compares to any other label.
            if (candidateCost.fuel > 1.0 + kDominanceNoiseEpsilon) {
                continue;
            }

            ++out.metrics.labelsGenerated;

            ParetoLabel candidate{.node = e.to(),
                                  .cost = candidateCost,
                                  .predecessor = entry.label,
                                  .viaEdge = edgeId,
                                  .legResult = result,
                                  .arrivalTimeSec = result.geometry.arrivalTimeSec};

            const Seconds candidateArrivalTimeSec = candidate.arrivalTimeSec;
            const InsertResult insertResult =
                insertCandidate(std::move(candidate), target, labelsAtNode, pool,
                                out.metrics, options, dominatedAtTargetIds);
            if (insertResult.outcome == InsertOutcome::Inserted) {
                open.push(QueueEntry{candidateArrivalTimeSec, *insertResult.insertedId});
                ++out.metrics.queuePushes;
            }
        }
    }

    for (LabelId labelId : labelsAtNode[target.value()]) {
        if (pool[labelId.value()].active) {
            out.solutions.push_back(reconstructRoute(labelId, pool));
        }
    }
    for (LabelId labelId : dominatedAtTargetIds) {
        out.dominated.push_back(reconstructRoute(labelId, pool));
    }

    out.metrics.targetLabelCount = static_cast<int>(out.solutions.size());
    const auto finishedAt = std::chrono::steady_clock::now();
    out.metrics.executionTimeMs =
        std::chrono::duration<double, std::milli>(finishedAt - startedAt).count();

    return out;
}

}  // namespace orca
