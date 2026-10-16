#pragma once

/// @file MissionGraph.hpp
/// A weighted directed graph over satellites and rescue vehicles: adjacency list,
/// dense IDs.
///
/// ## Why adjacency list, not matrix
///
/// ALGORITHMS.md §1.3: after feasibility pruning the graph is sparse, and both Dijkstra
/// and label-setting only ever enumerate a node's neighbours — never ask "does edge u->v
/// exist?" — which is the one operation a matrix would win on. Adjacency list is `O(V+E)`
/// space against `O(V^2)`.
///
/// ## Why NodeId/EdgeId can index directly into the vectors
///
/// `MissionGraph` — not the caller — assigns every `NodeId` and `EdgeId`, handing them out
/// sequentially from zero. That makes `id.value()` usable directly as a `std::vector`
/// index: no hash map, no indirection, `O(1)` lookup. This is the same "dense vector
/// indexing where IDs are contiguous" convention `Scenario` already documents for
/// satellites and vehicles (DATA_MODEL.md §10) — `MissionGraph` is the same pattern one
/// layer up.
///
/// ## Ownership
///
/// Nodes and edges are owned by value in contiguous vectors. `node()`/`edge()` return
/// references into that storage, valid for the graph's lifetime — there is no `shared_ptr`
/// here, consistent with the rest of ORCA's core (DATA_MODEL.md §10).

#include <cstddef>
#include <span>
#include <vector>

#include "GraphEdge.hpp"
#include "GraphNode.hpp"

namespace orca {

class MissionGraph {
public:
    MissionGraph() = default;

    /// Add a node, assigning it the next `NodeId` in sequence. The `id` field inside
    /// `node` is overwritten to match — callers should not pre-assign IDs.
    NodeId addNode(GraphNode node) noexcept;

    /// Add a directed edge `from -> to`, assigning it the next `EdgeId` in sequence.
    ///
    /// @throws std::invalid_argument if `from == to` (no self-loops — a transfer to one's
    ///         own orbit is representable as a pure plane change between two *different*
    ///         nodes at the same radius, never as a node pointing at itself), or if either
    ///         endpoint is not a node in this graph.
    EdgeId addEdge(NodeId from, NodeId to, TransferResult referenceResult,
                   Seconds referenceDepartureTimeSec);

    [[nodiscard]] const GraphNode& node(NodeId id) const noexcept;
    [[nodiscard]] const GraphEdge& edge(EdgeId id) const noexcept;

    [[nodiscard]] bool containsNode(NodeId id) const noexcept;

    /// Outgoing edges of `id`, in insertion order. Named for what it returns — edge
    /// identifiers, not node identifiers — rather than `neighbors()`, which would suggest
    /// the latter.
    [[nodiscard]] std::span<const EdgeId> outgoingEdges(NodeId id) const noexcept;

    [[nodiscard]] std::size_t nodeCount() const noexcept { return nodes_.size(); }
    [[nodiscard]] std::size_t edgeCount() const noexcept { return edges_.size(); }

    /// Every node currently in the graph, in insertion order. For iteration by callers
    /// that need to visit all nodes (the builder's candidate sweep, tests, and eventually
    /// the graph-data export).
    [[nodiscard]] const std::vector<GraphNode>& nodes() const noexcept { return nodes_; }

    /// Structural self-check: every edge's endpoints resolve to real nodes, no self-loops,
    /// no duplicate (from,to) pair, every cached objective vector is finite and
    /// non-negative with risk in [0,1]. Intended for tests and debug builds, not the
    /// search hot path.
    [[nodiscard]] bool isStructurallyValid() const noexcept;

private:
    std::vector<GraphNode> nodes_;
    std::vector<std::vector<EdgeId>> adjacency_;  ///< parallel to nodes_, by NodeId index
    std::vector<GraphEdge> edges_;
};

}  // namespace orca
