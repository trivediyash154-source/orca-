#include "PathResult.hpp"

#include "../core/Validation.hpp"

namespace orca {

PathResult::PathResult(bool found, NodeId source, NodeId target, Seconds startTimeSec,
                       std::vector<PathLeg> legs, ObjectiveVector totalObjectives,
                       SearchMetrics metrics) noexcept
    : found_(found),
      source_(source),
      target_(target),
      startTimeSec_(startTimeSec),
      legs_(std::move(legs)),
      totalObjectives_(totalObjectives),
      metrics_(metrics) {}

PathResult PathResult::notFound(NodeId source, NodeId target, Seconds startTimeSec,
                                SearchMetrics metrics) {
    return PathResult{false, source, target, startTimeSec, {}, ObjectiveVector{}, metrics};
}

PathResult PathResult::found(NodeId source, NodeId target, Seconds startTimeSec,
                             std::vector<PathLeg> legs, SearchMetrics metrics) {
    requireThat(!legs.empty(),
                "PathResult::found requires at least one leg; use trivial() for "
                "source == target");

    ObjectiveVector total;
    for (const PathLeg& leg : legs) {
        total = compose(total, leg.result.objectives());
    }

    return PathResult{true, source, target, startTimeSec, std::move(legs), total, metrics};
}

PathResult PathResult::trivial(NodeId sourceAndTarget, Seconds startTimeSec,
                               SearchMetrics metrics) {
    return PathResult{true,          sourceAndTarget, sourceAndTarget, startTimeSec,
                      {},            ObjectiveVector{}, metrics};
}

Seconds PathResult::arrivalTimeSec() const {
    requireThat(found_, "PathResult::arrivalTimeSec: no path was found");
    return legs_.empty() ? startTimeSec_ : legs_.back().result.geometry.arrivalTimeSec;
}

const ObjectiveVector& PathResult::totalObjectives() const {
    requireThat(found_, "PathResult::totalObjectives: no path was found");
    return totalObjectives_;
}

std::vector<NodeId> PathResult::nodeSequence() const {
    requireThat(found_, "PathResult::nodeSequence: no path was found");
    std::vector<NodeId> sequence;
    sequence.reserve(legs_.size() + 1);
    sequence.push_back(source_);
    for (const PathLeg& leg : legs_) {
        sequence.push_back(leg.to);
    }
    return sequence;
}

}  // namespace orca
