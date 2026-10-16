#pragma once

/// @file DebrisObject.hpp
/// A tracked debris hazard.
///
/// ## The `detected` flag is the mechanism behind dynamic replanning
///
/// Debris exists in the world from scenario load, but affects nothing until it is
/// *detected*. Undetected debris is invisible to the graph builder and contributes no
/// risk. A `DebrisAlert` event flips the flag, at which point affected edges are re-costed
/// and the planner may switch routes (SYSTEM_DESIGN.md §9).
///
/// Modelling it this way — rather than spawning debris mid-run — keeps the scenario fully
/// determined at load time, which is what lets the same seed reproduce the same run. The
/// surprise is in the *detection*, not in the world.
///
/// ## `radiusKm` is a hazard radius, not a physical size
///
/// Real debris is centimetres across. This is the radius within which ORCA's risk model
/// considers a transfer corridor to be threatened, which folds together physical size,
/// tracking uncertainty and a safety margin. It is a modelling parameter.

#include "../core/Types.hpp"
#include "../core/Units.hpp"
#include "../core/Vector3.hpp"
#include "Orbit.hpp"

namespace orca {

class DebrisObject {
public:
    /// @throws std::invalid_argument if `hazardRadiusKm` is not positive or
    ///         `densityFactor` is outside [0,1].
    DebrisObject(DebrisId id,
                 Orbit orbit,
                 Kilometres hazardRadiusKm,
                 double densityFactor,
                 bool detected = false);

    [[nodiscard]] DebrisId id() const noexcept { return id_; }
    [[nodiscard]] const Orbit& orbit() const noexcept { return orbit_; }
    [[nodiscard]] Kilometres hazardRadiusKm() const noexcept { return hazardRadiusKm_; }

    /// Relative hazard weight in [0,1]. Scales this object's contribution to edge risk.
    [[nodiscard]] double densityFactor() const noexcept { return densityFactor_; }

    /// Whether the planner knows about this object yet.
    [[nodiscard]] bool isDetected() const noexcept { return detected_; }

    /// ECI position at `timeSec`. Derived from the orbit; never cached.
    [[nodiscard]] Vector3 positionAtKm(Seconds timeSec) const noexcept;

    /// Reveal this object to the planner. One-way: detection is not lost again.
    void markDetected() noexcept;

private:
    DebrisId id_;
    Orbit orbit_;
    Kilometres hazardRadiusKm_;
    double densityFactor_;
    bool detected_;
};

}  // namespace orca
