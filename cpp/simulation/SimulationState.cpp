#include "SimulationState.hpp"

namespace orca {

namespace {
template <typename Map, typename Id>
auto* findIn(Map& map, Id id) noexcept {
    const auto it = map.find(id.value());
    return it == map.end() ? nullptr : &it->second;
}
}  // namespace

Satellite* SimulationState::findSatellite(SatelliteId id) noexcept {
    return findIn(satellites, id);
}
const Satellite* SimulationState::findSatellite(SatelliteId id) const noexcept {
    return findIn(satellites, id);
}
RescueVehicle* SimulationState::findVehicle(VehicleId id) noexcept { return findIn(vehicles, id); }
const RescueVehicle* SimulationState::findVehicle(VehicleId id) const noexcept {
    return findIn(vehicles, id);
}
DebrisObject* SimulationState::findDebris(DebrisId id) noexcept { return findIn(debris, id); }
const DebrisObject* SimulationState::findDebris(DebrisId id) const noexcept {
    return findIn(debris, id);
}
Emergency* SimulationState::findEmergency(EmergencyId id) noexcept {
    return findIn(emergencies, id);
}
const Emergency* SimulationState::findEmergency(EmergencyId id) const noexcept {
    return findIn(emergencies, id);
}
Mission* SimulationState::findMission(MissionId id) noexcept { return findIn(missions, id); }
const Mission* SimulationState::findMission(MissionId id) const noexcept {
    return findIn(missions, id);
}
const MissionPlan* SimulationState::findMissionPlan(MissionId id) const noexcept {
    return findIn(missionPlans, id);
}

void SimulationState::recordEvent(Event event) {
    recentEvents.push_back(std::move(event));
    while (recentEvents.size() > kMaxRecentEvents) {
        recentEvents.pop_front();
    }
}

void SimulationState::recordAlgorithmRun(AlgorithmRunRecord record) {
    algorithmRuns.push_back(std::move(record));
    while (algorithmRuns.size() > kMaxAlgorithmRuns) {
        algorithmRuns.pop_front();
    }
}

std::vector<VehicleId> SimulationState::availableVehicleIds() const {
    std::vector<VehicleId> ids;
    for (const auto& [rawId, vehicle] : vehicles) {
        if (vehicle.isAvailable()) {
            ids.push_back(vehicle.id());
        }
    }
    return ids;
}

std::vector<EmergencyId> SimulationState::pendingEmergencyIds() const {
    std::vector<EmergencyId> ids;
    for (const auto& [rawId, emergency] : emergencies) {
        if (emergency.state() == EmergencyState::Pending) {
            ids.push_back(emergency.id());
        }
    }
    return ids;
}

std::vector<MissionId> SimulationState::activeMissionIds() const {
    std::vector<MissionId> ids;
    for (const auto& [rawId, mission] : missions) {
        if (mission.isActive()) {
            ids.push_back(mission.id());
        }
    }
    return ids;
}

SimulationState makeInitialState(const Scenario& scenario) {
    SimulationState state;
    for (const Satellite& satellite : scenario.satellites()) {
        state.satellites.emplace(satellite.id().value(), satellite);
    }
    for (const RescueVehicle& vehicle : scenario.vehicles()) {
        state.vehicles.emplace(vehicle.id().value(), vehicle);
    }
    for (const DebrisObject& d : scenario.debris()) {
        state.debris.emplace(d.id().value(), d);
    }
    return state;
}

}  // namespace orca
