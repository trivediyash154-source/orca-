#include "VisualConstellation.hpp"

#include "../core/Types.hpp"
#include "../core/Units.hpp"
#include "../domain/DebrisObject.hpp"
#include "../domain/RescueVehicle.hpp"
#include "../domain/Satellite.hpp"
#include "JsonValue.hpp"

namespace orca::io {

namespace {

JsonValue toJson(const Vector3& p) { return JsonValue::object().set("x", p.x).set("y", p.y).set("z", p.z); }

/// Full orbital ELEMENTS, not just a position -- Phase 12 brief §5 needs RAAN and phase to
/// draw real per-plane orbital ribbons (`orbitGeometry.ts` uses the exact same
/// `som::positionKm()` formula the engine does). Without RAAN every ribbon would be drawn in
/// the same plane, which is precisely the "orbital paths collapse into a few lines" symptom.
JsonValue toJson(const Satellite& sat, Seconds t) {
    return JsonValue::object()
        .set("id", toString(sat.id()))
        .set("name", sat.name())
        .set("status", std::string(toString(sat.status())))
        .set("position", toJson(sat.positionAtKm(t)))
        .set("altitudeKm", sat.orbit().altitudeKm())
        .set("inclinationDeg", radToDeg(sat.orbit().inclinationRad()))
        .set("raanDeg", radToDeg(sat.orbit().raanRad()))
        .set("phaseDeg", radToDeg(sat.orbit().phaseAtRad(t)))
        .set("band", std::string(toString(sat.orbit().band())));
}

JsonValue toJson(const RescueVehicle& v, Seconds t) {
    return JsonValue::object()
        .set("id", toString(v.id()))
        .set("name", v.name())
        .set("status", std::string(toString(v.status())))
        .set("position", toJson(v.positionAtKm(t)))
        .set("altitudeKm", v.orbit().altitudeKm())
        .set("inclinationDeg", radToDeg(v.orbit().inclinationRad()))
        .set("raanDeg", radToDeg(v.orbit().raanRad()))
        .set("phaseDeg", radToDeg(v.orbit().phaseAtRad(t)))
        .set("band", std::string(toString(v.orbit().band())));
}

JsonValue toJson(const DebrisObject& d, Seconds t) {
    return JsonValue::object()
        .set("id", toString(d.id()))
        .set("position", toJson(d.positionAtKm(t)))
        .set("hazardRadiusKm", d.hazardRadiusKm());
}

}  // namespace

std::string serializeVisualConstellation(const Scenario& scenario, Seconds timeSec, double generationMs) {
    JsonValue satellites = JsonValue::array();
    for (const Satellite& s : scenario.satellites()) satellites.push(toJson(s, timeSec));

    JsonValue vehicles = JsonValue::array();
    for (const RescueVehicle& v : scenario.vehicles()) vehicles.push(toJson(v, timeSec));

    JsonValue debris = JsonValue::array();
    for (const DebrisObject& d : scenario.debris()) debris.push(toJson(d, timeSec));

    return JsonValue::object()
        .set("type", "visualConstellation")
        .set("scenarioName", scenario.name())
        .set("timeSec", timeSec)
        .set("generationMs", generationMs)
        .set("satelliteCount", static_cast<int>(scenario.satellites().size()))
        .set("vehicleCount", static_cast<int>(scenario.vehicles().size()))
        .set("debrisCount", static_cast<int>(scenario.debris().size()))
        .set("satellites", std::move(satellites))
        .set("vehicles", std::move(vehicles))
        .set("debris", std::move(debris))
        .dump();
}

}  // namespace orca::io
