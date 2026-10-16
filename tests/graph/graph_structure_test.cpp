/// Tests for GraphNode, GraphEdge and MissionGraph's structural API.
///
/// These tests exercise the graph as pure structure: node/edge insertion, lookup,
/// adjacency, directedness. None of them involve orbital mechanics beyond what is needed
/// to construct a valid `Orbit` to attach to a node — the *content* of transfers is
/// GraphBuilder's and Dijkstra's concern, tested separately.

#include <cmath>

#include "../../cpp/core/Constants.hpp"
#include "../../cpp/graph/MissionGraph.hpp"
#include "../../cpp/transfer/TransferModel.hpp"
#include "../TestFramework.hpp"

using namespace orca;

namespace {

Orbit orbitAt(Kilometres altitudeKm, Radians phaseRad = 0.0) {
    return Orbit::fromAltitude(altitudeKm, 0.0, 0.0, phaseRad);
}

/// A structurally valid but otherwise arbitrary TransferResult, for tests that only care
/// about graph topology and never read the edge's cached values.
TransferResult arbitraryResult() {
    TransferModel model;
    TransferConstraints c;
    c.maxDeltaVKms = 1.0e6;
    c.deltaVBudgetKms = 1.0e6;
    c.maxWaitSec = 1.0e13;
    c.latestArrivalSec = 1.0e18;
    return model.evaluate(orbitAt(500.0), orbitAt(800.0, 1.0), 0.0, c);
}

}  // namespace

// ---------------------------------------------------------------------------
// GraphNode
// ---------------------------------------------------------------------------

ORCA_TEST(GraphNode, StoresSatelliteIdentity) {
    const GraphNode node{NodeId{0}, SatelliteId{7}, orbitAt(500.0)};
    CHECK(node.kind() == NodeKind::Satellite);
    CHECK(std::holds_alternative<SatelliteId>(node.entity()));
    CHECK(std::get<SatelliteId>(node.entity()) == SatelliteId{7});
}

ORCA_TEST(GraphNode, StoresVehicleIdentity) {
    const GraphNode node{NodeId{0}, VehicleId{2}, orbitAt(500.0)};
    CHECK(node.kind() == NodeKind::RescueVehicle);
    CHECK(std::get<VehicleId>(node.entity()) == VehicleId{2});
}

ORCA_TEST(GraphNode, OrbitIsRetrievable) {
    const Orbit o = orbitAt(650.0, 1.2);
    const GraphNode node{NodeId{0}, SatelliteId{1}, o};
    CHECK_NEAR(node.orbit().altitudeKm(), 650.0, 1e-9);
    CHECK_NEAR(node.orbit().phaseAtEpochRad(), 1.2, 1e-12);
}

// ---------------------------------------------------------------------------
// MissionGraph — node management
// ---------------------------------------------------------------------------

ORCA_TEST(MissionGraph, StartsEmpty) {
    const MissionGraph g;
    CHECK_EQ(g.nodeCount(), std::size_t{0});
    CHECK_EQ(g.edgeCount(), std::size_t{0});
}

ORCA_TEST(MissionGraph, AddNodeAssignsSequentialIds) {
    MissionGraph g;
    const NodeId a = g.addNode(GraphNode{NodeId{0}, SatelliteId{0}, orbitAt(500.0)});
    const NodeId b = g.addNode(GraphNode{NodeId{0}, SatelliteId{1}, orbitAt(600.0)});
    const NodeId c = g.addNode(GraphNode{NodeId{0}, SatelliteId{2}, orbitAt(700.0)});
    CHECK_EQ(a.value(), std::uint32_t{0});
    CHECK_EQ(b.value(), std::uint32_t{1});
    CHECK_EQ(c.value(), std::uint32_t{2});
    CHECK_EQ(g.nodeCount(), std::size_t{3});
}

ORCA_TEST(MissionGraph, AddNodeIgnoresCallerSuppliedId) {
    // The graph, not the caller, assigns identity -- so passing a bogus id in must not
    // leak through.
    MissionGraph g;
    const NodeId assigned =
        g.addNode(GraphNode{NodeId{999}, SatelliteId{0}, orbitAt(500.0)});
    CHECK_EQ(assigned.value(), std::uint32_t{0});
    CHECK(g.node(assigned).id() == assigned);
}

ORCA_TEST(MissionGraph, ContainsNodeReflectsWhatWasAdded) {
    MissionGraph g;
    const NodeId a = g.addNode(GraphNode{NodeId{0}, SatelliteId{0}, orbitAt(500.0)});
    CHECK(g.containsNode(a));
    CHECK(!g.containsNode(NodeId{99}));
}

ORCA_TEST(MissionGraph, NodeLookupReturnsWhatWasStored) {
    MissionGraph g;
    const NodeId id = g.addNode(GraphNode{NodeId{0}, VehicleId{3}, orbitAt(550.0, 0.4)});
    const GraphNode& stored = g.node(id);
    CHECK(stored.kind() == NodeKind::RescueVehicle);
    CHECK(std::get<VehicleId>(stored.entity()) == VehicleId{3});
    CHECK_NEAR(stored.orbit().altitudeKm(), 550.0, 1e-9);
}

ORCA_TEST(MissionGraph, NodesAccessorListsInInsertionOrder) {
    MissionGraph g;
    g.addNode(GraphNode{NodeId{0}, SatelliteId{5}, orbitAt(500.0)});
    g.addNode(GraphNode{NodeId{0}, SatelliteId{9}, orbitAt(600.0)});
    CHECK_EQ(g.nodes().size(), std::size_t{2});
    CHECK(std::get<SatelliteId>(g.nodes()[0].entity()) == SatelliteId{5});
    CHECK(std::get<SatelliteId>(g.nodes()[1].entity()) == SatelliteId{9});
}

// ---------------------------------------------------------------------------
// MissionGraph — edges and directedness
// ---------------------------------------------------------------------------

ORCA_TEST(MissionGraph, AddEdgeAssignsSequentialIds) {
    MissionGraph g;
    const NodeId a = g.addNode(GraphNode{NodeId{0}, SatelliteId{0}, orbitAt(500.0)});
    const NodeId b = g.addNode(GraphNode{NodeId{0}, SatelliteId{1}, orbitAt(600.0)});
    const NodeId c = g.addNode(GraphNode{NodeId{0}, SatelliteId{2}, orbitAt(700.0)});

    const EdgeId e1 = g.addEdge(a, b, arbitraryResult(), 0.0);
    const EdgeId e2 = g.addEdge(b, c, arbitraryResult(), 0.0);
    CHECK_EQ(e1.value(), std::uint32_t{0});
    CHECK_EQ(e2.value(), std::uint32_t{1});
    CHECK_EQ(g.edgeCount(), std::size_t{2});
}

ORCA_TEST(MissionGraph, EdgeLookupReturnsCorrectEndpoints) {
    MissionGraph g;
    const NodeId a = g.addNode(GraphNode{NodeId{0}, SatelliteId{0}, orbitAt(500.0)});
    const NodeId b = g.addNode(GraphNode{NodeId{0}, SatelliteId{1}, orbitAt(600.0)});
    const EdgeId e = g.addEdge(a, b, arbitraryResult(), 42.0);

    CHECK(g.edge(e).from() == a);
    CHECK(g.edge(e).to() == b);
    CHECK_NEAR(g.edge(e).referenceDepartureTimeSec(), 42.0, 1e-12);
}

ORCA_TEST(MissionGraph, OutgoingEdgesReflectsOnlyThatNodesSourcedEdges) {
    MissionGraph g;
    const NodeId a = g.addNode(GraphNode{NodeId{0}, SatelliteId{0}, orbitAt(500.0)});
    const NodeId b = g.addNode(GraphNode{NodeId{0}, SatelliteId{1}, orbitAt(600.0)});
    const NodeId c = g.addNode(GraphNode{NodeId{0}, SatelliteId{2}, orbitAt(700.0)});

    g.addEdge(a, b, arbitraryResult(), 0.0);
    g.addEdge(a, c, arbitraryResult(), 0.0);
    g.addEdge(b, c, arbitraryResult(), 0.0);

    CHECK_EQ(g.outgoingEdges(a).size(), std::size_t{2});
    CHECK_EQ(g.outgoingEdges(b).size(), std::size_t{1});
    CHECK_EQ(g.outgoingEdges(c).size(), std::size_t{0});  // c has no outgoing edges
}

ORCA_TEST(MissionGraph, EdgeIsDirectedNotBidirectional) {
    // A -> B existing must not imply B -> A exists. This is the property
    // ALGORITHMS.md §1.4 requires and that the whole time-dependent design depends on.
    MissionGraph g;
    const NodeId a = g.addNode(GraphNode{NodeId{0}, SatelliteId{0}, orbitAt(500.0)});
    const NodeId b = g.addNode(GraphNode{NodeId{0}, SatelliteId{1}, orbitAt(600.0)});
    g.addEdge(a, b, arbitraryResult(), 0.0);

    CHECK_EQ(g.outgoingEdges(a).size(), std::size_t{1});
    CHECK_EQ(g.outgoingEdges(b).size(), std::size_t{0});
    CHECK_EQ(g.edgeCount(), std::size_t{1});
}

ORCA_TEST(MissionGraph, RejectsSelfLoop) {
    MissionGraph g;
    const NodeId a = g.addNode(GraphNode{NodeId{0}, SatelliteId{0}, orbitAt(500.0)});
    CHECK_THROWS(g.addEdge(a, a, arbitraryResult(), 0.0));
}

ORCA_TEST(MissionGraph, RejectsEdgeToUnknownNode) {
    MissionGraph g;
    const NodeId a = g.addNode(GraphNode{NodeId{0}, SatelliteId{0}, orbitAt(500.0)});
    CHECK_THROWS(g.addEdge(a, NodeId{99}, arbitraryResult(), 0.0));
    CHECK_THROWS(g.addEdge(NodeId{99}, a, arbitraryResult(), 0.0));
}

// ---------------------------------------------------------------------------
// Structural validation
// ---------------------------------------------------------------------------

ORCA_TEST(MissionGraph, EmptyGraphIsStructurallyValid) {
    const MissionGraph g;
    CHECK(g.isStructurallyValid());
}

ORCA_TEST(MissionGraph, WellFormedGraphIsStructurallyValid) {
    MissionGraph g;
    const NodeId a = g.addNode(GraphNode{NodeId{0}, SatelliteId{0}, orbitAt(500.0)});
    const NodeId b = g.addNode(GraphNode{NodeId{0}, SatelliteId{1}, orbitAt(600.0)});
    const NodeId c = g.addNode(GraphNode{NodeId{0}, SatelliteId{2}, orbitAt(700.0)});
    g.addEdge(a, b, arbitraryResult(), 0.0);
    g.addEdge(b, c, arbitraryResult(), 0.0);
    g.addEdge(a, c, arbitraryResult(), 0.0);
    CHECK(g.isStructurallyValid());
}

ORCA_TEST(MissionGraph, DetectsDuplicateEdges) {
    MissionGraph g;
    const NodeId a = g.addNode(GraphNode{NodeId{0}, SatelliteId{0}, orbitAt(500.0)});
    const NodeId b = g.addNode(GraphNode{NodeId{0}, SatelliteId{1}, orbitAt(600.0)});
    g.addEdge(a, b, arbitraryResult(), 0.0);
    g.addEdge(a, b, arbitraryResult(), 100.0);  // same (from,to), different sample time
    CHECK(!g.isStructurallyValid());
}

ORCA_TEST(MissionGraph, LargishGraphRemainsConsistent) {
    // A moderately sized fully-connected-minus-self-loops graph, as a sanity check that
    // dense construction does not corrupt indexing.
    MissionGraph g;
    std::vector<NodeId> ids;
    for (std::uint32_t i = 0; i < 12; ++i) {
        ids.push_back(g.addNode(
            GraphNode{NodeId{0}, SatelliteId{i}, orbitAt(500.0 + static_cast<double>(i) * 37.0)}));
    }
    for (const NodeId u : ids) {
        for (const NodeId v : ids) {
            if (u != v) {
                g.addEdge(u, v, arbitraryResult(), 0.0);
            }
        }
    }
    CHECK_EQ(g.nodeCount(), std::size_t{12});
    CHECK_EQ(g.edgeCount(), std::size_t{12 * 11});
    CHECK(g.isStructurallyValid());
    for (const NodeId u : ids) {
        CHECK_EQ(g.outgoingEdges(u).size(), std::size_t{11});
    }
}
