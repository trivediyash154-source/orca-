#pragma once

/// @file GraphNode.hpp
/// A point in the mission graph — a satellite or a rescue vehicle, at the orbit it
/// occupies.
///
/// ## Why only two node kinds
///
/// DATA_MODEL.md §4 originally sketched four kinds (`VEHICLE_SOURCE`, `SATELLITE`,
/// `CASUALTY_SINK`, `WAYPOINT`). Phase 3 implements two, matching the underlying entity
/// type, and DATA_MODEL.md has been corrected accordingly.
///
/// The finer distinctions collapse into existing entity-level predicates instead of
/// separate node kinds: "is this node a valid rescue target" is
/// `scenario.findSatellite(id)->requiresRescue()`, and "is this node dispatchable right
/// now" is `scenario.findVehicle(id)->isAvailable()`. Those predicates already exist on
/// `Satellite`/`RescueVehicle` (Phase 1) and answer the question more precisely than a
/// static node-kind tag could — a satellite's rescue-worthiness changes over the course of
/// a run, while a stored `NodeKind::CasualtySink` would not. Duplicating that as graph
/// state would be a second source of truth for something the domain entity already knows.
///
/// There is deliberately no synthetic `WAYPOINT` kind either: SYSTEM_DESIGN.md's staging
/// concept ("healthy satellites -> staging nodes") is realised by ordinary satellite
/// nodes having outgoing edges, not by inventing extra graph-only entities
/// (IMPLEMENTATION_ROADMAP.md's guidance against artificial transfer nodes, §4 of the
/// Phase 3 brief).
///
/// ## Why the node owns a copy of the Orbit
///
/// `Orbit` is an immutable value type (Phase 1): four doubles, and ORCA models no
/// perturbations, so an orbit does not change over a scenario's duration — only the
/// *phase along it* does, and that is a pure function of time
/// (`Orbit::positionAtKm`/`phaseAtRad`). Storing a copy is therefore not a stale snapshot
/// the way a cached *position* would be; it is the complete truth for all time. This is
/// why GRAPH_MODEL.md §5 argues no separate "graph snapshot" concept is needed for Phase 3.

#include <variant>

#include "../core/Types.hpp"
#include "../domain/Orbit.hpp"

namespace orca {

enum class NodeKind { Satellite, RescueVehicle };

[[nodiscard]] constexpr std::string_view toString(NodeKind k) noexcept {
    switch (k) {
        case NodeKind::Satellite:     return "SATELLITE";
        case NodeKind::RescueVehicle: return "RESCUE_VEHICLE";
    }
    return "UNKNOWN";
}

class GraphNode {
public:
    GraphNode(NodeId id, SatelliteId satelliteId, Orbit orbit) noexcept
        : id_(id), entity_(satelliteId), orbit_(orbit) {}

    GraphNode(NodeId id, VehicleId vehicleId, Orbit orbit) noexcept
        : id_(id), entity_(vehicleId), orbit_(orbit) {}

    [[nodiscard]] NodeId id() const noexcept { return id_; }
    [[nodiscard]] const Orbit& orbit() const noexcept { return orbit_; }

    [[nodiscard]] NodeKind kind() const noexcept {
        return std::holds_alternative<SatelliteId>(entity_) ? NodeKind::Satellite
                                                             : NodeKind::RescueVehicle;
    }

    /// The underlying entity identity. Callers needing the concrete ID use
    /// `std::get<SatelliteId>(node.entity())`, guarded by `kind()` or a `std::visit`.
    [[nodiscard]] const std::variant<SatelliteId, VehicleId>& entity() const noexcept {
        return entity_;
    }

private:
    NodeId id_;
    std::variant<SatelliteId, VehicleId> entity_;
    Orbit orbit_;
};

}  // namespace orca
