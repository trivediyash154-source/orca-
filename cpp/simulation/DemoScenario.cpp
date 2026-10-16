#include "DemoScenario.hpp"

#include "../domain/DebrisObject.hpp"
#include "../domain/RescueVehicle.hpp"
#include "../domain/Satellite.hpp"

namespace orca {

Scenario buildDemoScenario() {
    const Orbit v1o = Orbit::fromAltitude(500.0, degToRad(5.0), 0.0, 0.0);
    const Orbit e1o = Orbit::fromAltitude(500.0, degToRad(5.0), 0.0, degToRad(2.0));
    const Orbit midAo = Orbit::fromAltitude(650.0, degToRad(8.0), 0.0, degToRad(1.0));
    const Orbit midBo = Orbit::fromAltitude(750.0, degToRad(10.0), 0.0, degToRad(1.5));

    std::vector<Satellite> satellites;
    satellites.emplace_back(SatelliteId(201), "MID-A", midAo);
    satellites.emplace_back(SatelliteId(202), "MID-B", midBo);
    satellites.emplace_back(SatelliteId(101), "SAT-101", e1o);

    std::vector<RescueVehicle> vehicles;
    vehicles.emplace_back(VehicleId(1), "RV-01", v1o, 10.0);

    std::vector<DebrisObject> debris;
    // Matches MID-A's altitude exactly -- threatens the route through it specifically.
    const Orbit debrisOrbit = Orbit::fromAltitude(650.0, degToRad(8.0), 0.0, degToRad(3.0));
    debris.emplace_back(DebrisId(1), debrisOrbit, 5.0, 0.5, false);

    SimulationConfig config;
    config.timeStepSec = 1.0;
    config.durationSec = 600000.0;

    return Scenario("Propulsion Failure Demo", Seed(555), config, satellites, vehicles, debris);
}

std::vector<ScriptedFailure> demoScenarioScriptedFailures() {
    return {ScriptedFailure{
        .atSec = 5.0,
        .satellite = SatelliteId(101),
        .kind = EmergencyKind::PowerFailure,
        .severity = Severity::High,
        .survivalWindowSec = 5.0e7,
    }};
}

Event demoScenarioDebrisAlertEvent() {
    return Event{
        .id = EventId(0),
        .timestampSec = 25.0,
        .kind = EventKind::DebrisAlert,
        .debris = DebrisId(1),
        .detail = "debris detected",
    };
}

}  // namespace orca
