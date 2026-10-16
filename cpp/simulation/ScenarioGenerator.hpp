#pragma once

/// @file ScenarioGenerator.hpp
/// Deterministic, configurable N-object scenario generation — Phase 11 brief §6-13, the
/// most important missing prerequisite Phase 10 identified: benchmarking, research
/// experiments, and instanced-rendering measurement all need scenarios larger than the
/// one hand-built canonical demo (`DemoScenario.hpp`).
///
/// ## Synthetic, not representative
///
/// These distributions are deliberately simple (uniform draws within configured bands) and
/// are NOT calibrated against any real satellite population — the same "simulation model,
/// not validated astrodynamics" scope statement `SIMULATION_MODEL.md` makes for the whole
/// project applies here. They are called **synthetic experimental distributions**
/// throughout this project's documentation and never described as realistic.
///
/// ## Determinism
///
/// `generateScenario(name, seed, config)` draws exclusively from
/// `RandomGenerator(seed).forStream(RandomStream::Scenario)` (the stream `SIMULATION_MODEL.md`
/// §7.2 already reserved for exactly this purpose, unused until now) plus per-entity
/// sub-streams (`forStream(ordinal)`) so entity count changes never perturb earlier draws.
/// The same `(name, seed, config)` always produces bit-identical entities.
///
/// ## Validity by construction, checked again explicitly
///
/// Every entity is built through its own domain constructor (`Orbit::fromAltitude()`,
/// `Satellite`, `RescueVehicle`, `DebrisObject`), each of which already throws on an
/// invalid value (negative altitude, out-of-range inclination, non-positive delta-v
/// budget, ...) — so a malformed entity cannot be constructed at all. `validateGeneratedScenario()`
/// is a second, explicit pass over the RESULT (brief §12's own request), checking that
/// every value is finite and within the configuration's own declared bounds — catching a
/// generator bug (a value silently outside its intended band despite being individually
/// "valid") that per-entity construction alone would not.

#include <string>
#include <vector>

#include "../core/RandomGenerator.hpp"
#include "../core/Types.hpp"
#include "../core/Units.hpp"
#include "../domain/Scenario.hpp"

namespace orca {

/// Every tunable band a generated scenario draws from. Defaults produce brief §9's "Normal"
/// family at whatever object count the caller requests.
struct ScenarioGeneratorConfig {
    int satelliteCount = 25;
    int vehicleCount = 3;
    int debrisCount = 2;

    /// Orbital band mix -- must sum to <= 1.0 (the remainder, if any, is treated as LEO).
    double leoFraction = 0.7;
    double meoFraction = 0.2;
    double geoFraction = 0.1;

    Kilometres leoAltitudeMinKm = 400.0;
    Kilometres leoAltitudeMaxKm = 1800.0;
    Kilometres meoAltitudeMinKm = 5000.0;
    Kilometres meoAltitudeMaxKm = 20000.0;
    Kilometres geoAltitudeMinKm = 35586.0;
    Kilometres geoAltitudeMaxKm = 35986.0;

    Degrees inclinationMinDeg = 0.0;
    Degrees inclinationMaxDeg = 98.0;

    KmPerSec vehicleDeltaVMinKms = 2.0;
    KmPerSec vehicleDeltaVMaxKms = 10.0;

    Kilometres debrisHazardRadiusMinKm = 2.0;
    Kilometres debrisHazardRadiusMaxKm = 10.0;
    double debrisDensityMin = 0.1;
    double debrisDensityMax = 0.9;

    /// Not stored on the generated `Scenario` (which has no such field, `DATA_MODEL.md`
    /// §8) -- carried here only so a benchmark/experiment driver knows how many
    /// `EmergencyGenerator::scheduleRandomFailures()` draws this family calls for.
    int emergencyCount = 1;

    Seconds durationSec = 600000.0;
    Seconds timeStepSec = 1.0;
};

/// Generates `config.satelliteCount` satellites, `config.vehicleCount` vehicles, and
/// `config.debrisCount` debris objects, all at altitude/inclination/phase/capability drawn
/// from `config`'s bands, deterministically from `seed`.
///
/// @throws std::invalid_argument if `config` itself is nonsensical (zero/negative counts,
///         `min > max` on any band, band fractions summing above 1.0) -- checked before
///         any entity is constructed, so a bad config fails fast with a clear message
///         rather than a confusing downstream `Orbit` construction error.
[[nodiscard]] Scenario generateScenario(const std::string& name, Seed seed,
                                        const ScenarioGeneratorConfig& config);

/// Re-checks a generated scenario's entities against `config`'s own declared bounds and
/// finiteness -- brief §12's explicit "validate before benchmark execution" requirement.
///
/// @throws std::invalid_argument, with a message naming the first offending entity and
///         value, if any altitude/inclination/delta-v/hazard value lies outside its
///         configured band, or any value is non-finite.
void validateGeneratedScenario(const Scenario& scenario, const ScenarioGeneratorConfig& config);

/// Brief §11's eight named scenario families, each a tuned `ScenarioGeneratorConfig` at a
/// caller-chosen object count. Every family is a real, documented distribution choice --
/// none claims to represent an actual orbital population.
namespace scenario_family {

[[nodiscard]] ScenarioGeneratorConfig sparse(int satelliteCount);          // A: few vehicles, wide spread
[[nodiscard]] ScenarioGeneratorConfig normal(int satelliteCount);          // B: balanced defaults
[[nodiscard]] ScenarioGeneratorConfig dense(int satelliteCount);           // C: tight altitude clustering
[[nodiscard]] ScenarioGeneratorConfig multiEmergency(int satelliteCount);  // D: many emergencies
[[nodiscard]] ScenarioGeneratorConfig fuelConstrained(int satelliteCount); // E: low vehicle delta-v
[[nodiscard]] ScenarioGeneratorConfig highDebris(int satelliteCount);      // F: many, dense debris
[[nodiscard]] ScenarioGeneratorConfig tradeoffRich(int satelliteCount);    // G: wide cost spread
[[nodiscard]] ScenarioGeneratorConfig stress(int satelliteCount);          // H: large N, normal shape

/// Phase 12 addition, for VISUALIZATION ONLY (`cpp/io/VisualConstellation.hpp`): a full
/// spherical shell rather than a planning-tuned distribution. The seven families above are
/// tuned to make PLANNING interesting (`dense`, for instance, deliberately caps inclination
/// at 15 deg so many transfers stay feasible) -- which, rendered, collapses every object
/// into a flat equatorial band. This family instead spreads inclination across the full
/// 0-99 deg range and mixes LEO/MEO/GEO, so the constellation reads as a real shell.
/// Deliberately NOT one of brief §11's benchmark families: it is never used for a benchmark
/// or experiment, only for the visual backdrop.
[[nodiscard]] ScenarioGeneratorConfig visualShell(int satelliteCount);

}  // namespace scenario_family

}  // namespace orca
