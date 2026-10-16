#pragma once

/// @file Scenario.hpp
/// The world a simulation run takes place in.
///
/// ## Representation only
///
/// This is the loaded, validated contents of a scenario: the objects, the configuration,
/// and the seed. It does not *generate* scenarios — `EmergencyGenerator` and the scenario
/// builder arrive in Phase 11 — and it does not simulate. It is the input, not the run.
///
/// ## Why the seed lives here
///
/// The seed is part of the scenario, not part of the run, because "same seed + same
/// config produces an identical run" (FR-N5) requires both to be captured together. A
/// scenario without its seed is not reproducible, so they are not separable.
///
/// ## Storage
///
/// Entities live in contiguous `std::vector`s and are referred to by strongly-typed IDs
/// rather than pointers (DATA_MODEL.md §10). This gives stable references across
/// reallocation, cache-friendly iteration for the graph builder's O(V^2) candidate sweep,
/// and no ownership ambiguity — which is why `shared_ptr` appears nowhere in ORCA's core.

#include <optional>
#include <string>
#include <vector>

#include "../core/Types.hpp"
#include "../core/Units.hpp"
#include "DebrisObject.hpp"
#include "RescueVehicle.hpp"
#include "Satellite.hpp"

namespace orca {

/// Simulation timing configuration.
struct SimulationConfig {
    Seconds timeStepSec = 1.0;      ///< fixed integration step
    Seconds durationSec = 3600.0;   ///< total simulated time
    double snapshotRateHz = 10.0;   ///< how often state is emitted, independent of step
};

class Scenario {
public:
    /// @throws std::invalid_argument if the name is empty, the configuration is
    ///         non-positive, or any entity ID is duplicated.
    Scenario(std::string name,
             Seed seed,
             SimulationConfig config,
             std::vector<Satellite> satellites,
             std::vector<RescueVehicle> vehicles,
             std::vector<DebrisObject> debris);

    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] Seed seed() const noexcept { return seed_; }
    [[nodiscard]] const SimulationConfig& config() const noexcept { return config_; }

    [[nodiscard]] const std::vector<Satellite>& satellites() const noexcept {
        return satellites_;
    }
    [[nodiscard]] const std::vector<RescueVehicle>& vehicles() const noexcept {
        return vehicles_;
    }
    [[nodiscard]] const std::vector<DebrisObject>& debris() const noexcept {
        return debris_;
    }

    [[nodiscard]] std::size_t objectCount() const noexcept;

    // --- Lookup -------------------------------------------------------------
    //
    // Return pointers rather than references or optionals: a missing ID is a normal
    // question to ask (an emergency may reference a satellite the caller has not
    // confirmed), and `nullptr` expresses that without exceptions or wrapper overhead.

    [[nodiscard]] const Satellite* findSatellite(SatelliteId id) const noexcept;
    [[nodiscard]] const RescueVehicle* findVehicle(VehicleId id) const noexcept;
    [[nodiscard]] const DebrisObject* findDebris(DebrisId id) const noexcept;

private:
    std::string name_;
    Seed seed_;
    SimulationConfig config_;
    std::vector<Satellite> satellites_;
    std::vector<RescueVehicle> vehicles_;
    std::vector<DebrisObject> debris_;
};

}  // namespace orca
