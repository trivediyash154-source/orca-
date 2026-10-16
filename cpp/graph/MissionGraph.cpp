#include "MissionGraph.hpp"

#include <algorithm>
#include <format>

#include "../core/ObjectiveVector.hpp"
#include "../core/Validation.hpp"

namespace orca {

NodeId MissionGraph::addNode(GraphNode node) noexcept {
    const NodeId id{static_cast<NodeId::Value>(nodes_.size())};
    // GraphNode has no setter -- reconstruct with the assigned id rather than mutate,
    // which keeps GraphNode's fields const-after-construction in spirit even though the
    // language does not let the member itself be const (it lives in a std::vector).
    nodes_.push_back(std::visit(
        [&](const auto& entityId) { return GraphNode{id, entityId, node.orbit()}; },
        node.entity()));
    adjacency_.emplace_back();
    return id;
}

EdgeId MissionGraph::addEdge(NodeId from, NodeId to, TransferResult referenceResult,
                             Seconds referenceDepartureTimeSec) {
    requireThat(from != to,
                std::format("MissionGraph: self-loop rejected ({} -> {})", from.value(),
                            to.value()));
    requireThat(containsNode(from),
                std::format("MissionGraph: edge source {} is not a node in this graph",
                            from.value()));
    requireThat(containsNode(to),
                std::format("MissionGraph: edge target {} is not a node in this graph",
                            to.value()));

    const EdgeId id{static_cast<EdgeId::Value>(edges_.size())};
    edges_.emplace_back(id, from, to, referenceResult, referenceDepartureTimeSec);
    adjacency_[from.value()].push_back(id);
    return id;
}

const GraphNode& MissionGraph::node(NodeId id) const noexcept {
    return nodes_[id.value()];
}

const GraphEdge& MissionGraph::edge(EdgeId id) const noexcept {
    return edges_[id.value()];
}

bool MissionGraph::containsNode(NodeId id) const noexcept {
    return id.value() < nodes_.size();
}

std::span<const EdgeId> MissionGraph::outgoingEdges(NodeId id) const noexcept {
    return adjacency_[id.value()];
}

bool MissionGraph::isStructurallyValid() const noexcept {
    if (adjacency_.size() != nodes_.size()) {
        return false;
    }

    for (const GraphEdge& e : edges_) {
        if (!containsNode(e.from()) || !containsNode(e.to())) {
            return false;
        }
        if (e.from() == e.to()) {
            return false;
        }
        if (!isValid(e.referenceResult().objectives())) {
            return false;
        }
    }

    // No duplicate (from, to) pairs. Checked per-node against that node's own outgoing
    // list rather than globally, so this stays O(V * deg^2) worst case rather than O(E^2).
    for (std::size_t u = 0; u < nodes_.size(); ++u) {
        const auto& out = adjacency_[u];
        for (std::size_t i = 0; i < out.size(); ++i) {
            for (std::size_t j = i + 1; j < out.size(); ++j) {
                if (edges_[out[i].value()].to() == edges_[out[j].value()].to()) {
                    return false;
                }
            }
        }
    }

    return true;
}

}  // namespace orca
