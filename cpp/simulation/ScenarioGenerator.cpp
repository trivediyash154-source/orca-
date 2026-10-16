#include "ScenarioGenerator.hpp"

#include <algorithm>
#include <cmath>
#include <format>

#include "../core/Constants.hpp"
#include "../core/Validation.hpp"
#include "../domain/DebrisObject.hpp"
#include "../domain/RescueVehicle.hpp"
#include "../domain/Satellite.hpp"

namespace orca {

namespace {

void requireSaneConfig(const ScenarioGeneratorConfig& c) {
    requireThat(c.satelliteCount > 0, "ScenarioGeneratorConfig: satelliteCount must be positive");
    requireThat(c.vehicleCount > 0, "ScenarioGeneratorConfig: vehicleCount must be positive");
    requireThat(c.debrisCount >= 0, "ScenarioGeneratorConfig: debrisCount must be non-negative");
    requireThat(c.leoFraction + c.meoFraction + c.geoFraction <= 1.0 + kEpsilon,
                "ScenarioGeneratorConfig: band fractions must not sum above 1.0");
    requireThat(c.leoAltitudeMinKm <= c.leoAltitudeMaxKm, "ScenarioGeneratorConfig: leo band inverted");
    requireThat(c.meoAltitudeMinKm <= c.meoAltitudeMaxKm, "ScenarioGeneratorConfig: meo band inverted");
    requireThat(c.geoAltitudeMinKm <= c.geoAltitudeMaxKm, "ScenarioGeneratorConfig: geo band inverted");
    requireThat(c.inclinationMinDeg <= c.inclinationMaxDeg,
                "ScenarioGeneratorConfig: inclination band inverted");
    requireThat(c.vehicleDeltaVMinKms > 0.0 && c.vehicleDeltaVMinKms <= c.vehicleDeltaVMaxKms,
                "ScenarioGeneratorConfig: delta-v band invalid");
    requireThat(c.debrisHazardRadiusMinKm > 0.0 && c.debrisHazardRadiusMinKm <= c.debrisHazardRadiusMaxKm,
                "ScenarioGeneratorConfig: debris hazard-radius band invalid");
    requireThat(c.debrisDensityMin >= 0.0 && c.debrisDensityMin <= c.debrisDensityMax &&
                    c.debrisDensityMax <= 1.0,
                "ScenarioGeneratorConfig: debris density band invalid");
}

/// One of the three configured altitude bands, picked by a single uniform draw against the
/// configured fractions -- the remainder (if the three fractions sum below 1.0) falls
/// through to LEO, documented in `ScenarioGeneratorConfig`'s own file comment.
Orbit drawOrbit(RandomGenerator& rng, const ScenarioGeneratorConfig& c) {
    const double u = rng.nextDouble();
    Kilometres altMin = c.leoAltitudeMinKm;
    Kilometres altMax = c.leoAltitudeMaxKm;
    if (u < c.leoFraction + c.meoFraction && u >= c.leoFraction) {
        altMin = c.meoAltitudeMinKm;
        altMax = c.meoAltitudeMaxKm;
    } else if (u >= c.leoFraction + c.meoFraction) {
        altMin = c.geoAltitudeMinKm;
        altMax = c.geoAltitudeMaxKm;
    }
    const Kilometres altitude = rng.nextDoubleInRange(altMin, altMax);
    const Radians inclination = degToRad(rng.nextDoubleInRange(c.inclinationMinDeg, c.inclinationMaxDeg));
    const Radians raan = rng.nextDoubleInRange(0.0, 2.0 * kPi);
    const Radians phase = rng.nextDoubleInRange(0.0, 2.0 * kPi);
    return Orbit::fromAltitude(altitude, inclination, raan, phase);
}

}  // namespace

Scenario generateScenario(const std::string& name, Seed seed, const ScenarioGeneratorConfig& config) {
    requireSaneConfig(config);
    const RandomGenerator base = RandomGenerator(seed).forStream(RandomStream::Scenario);

    std::vector<Satellite> satellites;
    satellites.reserve(static_cast<std::size_t>(config.satelliteCount));
    for (int i = 0; i < config.satelliteCount; ++i) {
        RandomGenerator entropy = base.forStream(static_cast<std::uint64_t>(i));
        const Orbit orbit = drawOrbit(entropy, config);
        satellites.emplace_back(SatelliteId(static_cast<std::uint32_t>(i + 1)),
                                std::format("SAT-{:03d}", i + 1), orbit);
    }

    std::vector<RescueVehicle> vehicles;
    vehicles.reserve(static_cast<std::size_t>(config.vehicleCount));
    for (int i = 0; i < config.vehicleCount; ++i) {
        RandomGenerator entropy = base.forStream(static_cast<std::uint64_t>(config.satelliteCount + i));
        const Orbit orbit = drawOrbit(entropy, config);
        const KmPerSec deltaV = entropy.nextDoubleInRange(config.vehicleDeltaVMinKms, config.vehicleDeltaVMaxKms);
        vehicles.emplace_back(VehicleId(static_cast<std::uint32_t>(i + 1)), std::format("RV-{:02d}", i + 1),
                              orbit, deltaV);
    }

    std::vector<DebrisObject> debris;
    debris.reserve(static_cast<std::size_t>(config.debrisCount));
    const int debrisStreamOffset = config.satelliteCount + config.vehicleCount;
    for (int i = 0; i < config.debrisCount; ++i) {
        RandomGenerator entropy = base.forStream(static_cast<std::uint64_t>(debrisStreamOffset + i));
        const Orbit orbit = drawOrbit(entropy, config);
        const Kilometres hazardRadius =
            entropy.nextDoubleInRange(config.debrisHazardRadiusMinKm, config.debrisHazardRadiusMaxKm);
        const double density = entropy.nextDoubleInRange(config.debrisDensityMin, config.debrisDensityMax);
        debris.emplace_back(DebrisId(static_cast<std::uint32_t>(i + 1)), orbit, hazardRadius, density, false);
    }

    SimulationConfig simConfig;
    simConfig.timeStepSec = config.timeStepSec;
    simConfig.durationSec = config.durationSec;
    return Scenario(name, seed, simConfig, satellites, vehicles, debris);
}

namespace {

bool inBand(double value, double lo, double hi) { return value >= lo - kEpsilon && value <= hi + kEpsilon; }

void checkOrbit(const Orbit& orbit, const ScenarioGeneratorConfig& c, const std::string& label) {
    requireThat(std::isfinite(orbit.altitudeKm()) && std::isfinite(orbit.inclinationRad()),
                std::format("validateGeneratedScenario: {} has a non-finite orbit value", label));
    const bool inAnyAltitudeBand = inBand(orbit.altitudeKm(), c.leoAltitudeMinKm, c.leoAltitudeMaxKm) ||
                                   inBand(orbit.altitudeKm(), c.meoAltitudeMinKm, c.meoAltitudeMaxKm) ||
                                   inBand(orbit.altitudeKm(), c.geoAltitudeMinKm, c.geoAltitudeMaxKm);
    requireThat(inAnyAltitudeBand,
                std::format("validateGeneratedScenario: {} altitude {:.1f} km falls outside every "
                            "configured band",
                            label, orbit.altitudeKm()));
    const Degrees inclinationDeg = radToDeg(orbit.inclinationRad());
    requireThat(inBand(inclinationDeg, c.inclinationMinDeg, c.inclinationMaxDeg),
                std::format("validateGeneratedScenario: {} inclination {:.1f} deg outside configured band",
                            label, inclinationDeg));
}

}  // namespace

void validateGeneratedScenario(const Scenario& scenario, const ScenarioGeneratorConfig& config) {
    for (const Satellite& sat : scenario.satellites()) {
        checkOrbit(sat.orbit(), config, std::format("satellite {}", toString(sat.id())));
    }
    for (const RescueVehicle& v : scenario.vehicles()) {
        checkOrbit(v.orbit(), config, std::format("vehicle {}", toString(v.id())));
        requireThat(std::isfinite(v.deltaVBudgetKms()) &&
                        inBand(v.deltaVBudgetKms(), config.vehicleDeltaVMinKms, config.vehicleDeltaVMaxKms),
                    std::format("validateGeneratedScenario: vehicle {} delta-v {:.2f} km/s outside "
                                "configured band",
                                toString(v.id()), v.deltaVBudgetKms()));
    }
    for (const DebrisObject& d : scenario.debris()) {
        checkOrbit(d.orbit(), config, std::format("debris {}", toString(d.id())));
        requireThat(std::isfinite(d.hazardRadiusKm()) &&
                        inBand(d.hazardRadiusKm(), config.debrisHazardRadiusMinKm,
                              config.debrisHazardRadiusMaxKm),
                    std::format("validateGeneratedScenario: debris {} hazard radius outside configured band",
                                toString(d.id())));
        requireThat(std::isfinite(d.densityFactor()) &&
                        inBand(d.densityFactor(), config.debrisDensityMin, config.debrisDensityMax),
                    std::format("validateGeneratedScenario: debris {} density outside configured band",
                                toString(d.id())));
    }
}

namespace scenario_family {

ScenarioGeneratorConfig sparse(int satelliteCount) {
    ScenarioGeneratorConfig c;
    c.satelliteCount = satelliteCount;
    c.vehicleCount = std::max(1, satelliteCount / 20);
    c.debrisCount = std::max(1, satelliteCount / 25);
    c.leoFraction = 0.4;
    c.meoFraction = 0.3;
    c.geoFraction = 0.3;
    c.leoAltitudeMinKm = 400.0;
    c.leoAltitudeMaxKm = 2000.0;
    return c;
}

ScenarioGeneratorConfig normal(int satelliteCount) {
    ScenarioGeneratorConfig c;
    c.satelliteCount = satelliteCount;
    c.vehicleCount = std::max(2, satelliteCount / 10);
    c.debrisCount = std::max(1, satelliteCount / 12);
    return c;
}

ScenarioGeneratorConfig dense(int satelliteCount) {
    ScenarioGeneratorConfig c;
    c.satelliteCount = satelliteCount;
    c.vehicleCount = std::max(3, satelliteCount / 8);
    c.debrisCount = std::max(1, satelliteCount / 15);
    c.leoFraction = 1.0;
    c.meoFraction = 0.0;
    c.geoFraction = 0.0;
    c.leoAltitudeMinKm = 500.0;
    c.leoAltitudeMaxKm = 700.0;  // tight clustering -> many feasible transfers
    c.inclinationMinDeg = 0.0;
    c.inclinationMaxDeg = 15.0;
    return c;
}

ScenarioGeneratorConfig multiEmergency(int satelliteCount) {
    ScenarioGeneratorConfig c = normal(satelliteCount);
    c.emergencyCount = std::max(3, satelliteCount / 5);
    return c;
}

ScenarioGeneratorConfig fuelConstrained(int satelliteCount) {
    ScenarioGeneratorConfig c = normal(satelliteCount);
    c.vehicleDeltaVMinKms = 0.5;
    c.vehicleDeltaVMaxKms = 2.0;
    return c;
}

ScenarioGeneratorConfig highDebris(int satelliteCount) {
    ScenarioGeneratorConfig c = normal(satelliteCount);
    c.debrisCount = std::max(10, satelliteCount / 3);
    c.debrisDensityMin = 0.5;
    c.debrisDensityMax = 0.95;
    return c;
}

ScenarioGeneratorConfig tradeoffRich(int satelliteCount) {
    ScenarioGeneratorConfig c = normal(satelliteCount);
    c.leoFraction = 0.34;
    c.meoFraction = 0.33;
    c.geoFraction = 0.33;
    c.inclinationMinDeg = 0.0;
    c.inclinationMaxDeg = 90.0;  // wide spread -> genuinely different plane-change costs
    c.vehicleDeltaVMinKms = 1.5;
    c.vehicleDeltaVMaxKms = 12.0;  // some vehicles fast/expensive, others slow/cheap
    return c;
}

ScenarioGeneratorConfig stress(int satelliteCount) {
    ScenarioGeneratorConfig c = normal(satelliteCount);
    c.vehicleCount = std::max(5, satelliteCount / 10);
    c.debrisCount = std::max(5, satelliteCount / 20);
    return c;
}

ScenarioGeneratorConfig visualShell(int satelliteCount) {
    ScenarioGeneratorConfig c;
    c.satelliteCount = satelliteCount;
    c.vehicleCount = std::max(4, satelliteCount / 200);
    c.debrisCount = std::max(2, satelliteCount / 100);
    // Mostly LEO (that is where a real congested shell lives), with a thin MEO/GEO
    // population so the rendered scene shows more than one shell.
    c.leoFraction = 0.88;
    c.meoFraction = 0.08;
    c.geoFraction = 0.04;
    c.leoAltitudeMinKm = 450.0;
    c.leoAltitudeMaxKm = 1400.0;
    // The whole point of this family: a full inclination spread, so RAAN (already drawn
    // uniformly over 0-2pi by `makeOrbit()`) actually produces distinct orbital PLANES
    // instead of one flat equatorial band.
    c.inclinationMinDeg = 0.0;
    c.inclinationMaxDeg = 99.0;
    return c;
}

}  // namespace scenario_family

}  // namespace orca
