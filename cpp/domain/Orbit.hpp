#pragma once

/// @file Orbit.hpp
/// A circular orbit, as ORCA models it.
///
/// ## One source of truth
///
/// Only four values are stored: semi-major axis, inclination, RAAN, and phase at epoch.
/// Period, mean motion, speed, altitude and band are all **derived on demand** and never
/// cached in mutable state.
///
/// This is deliberate. An `Orbit` that stored both `altitudeKm` and `periodSec` would have
/// two fields that must agree, and nothing in the type system forcing them to. Someone
/// would eventually update one and not the other, and the resulting bug would look like a
/// physics error rather than a bookkeeping error. Deriving costs a `sqrt` per call, which
/// is irrelevant next to being unable to represent an inconsistent orbit at all.
///
/// ## No stored position
///
/// `Orbit` deliberately does not hold a position, and neither does `Satellite`. Position
/// is a *function of orbit and time* — `positionAtKm(t)` — so there is no per-tick
/// propagation state to advance, drift, or forget to update. A snapshot at t = 5000 is
/// identical whether the simulation reached it in one step or five thousand.
///
/// The consequence worth noting: `Orbit` is an immutable value type. Nothing about an
/// orbit changes over the course of a scenario, because ORCA models no perturbations
/// (SIMULATION_MODEL.md §8, items 1 and 2).

#include "../core/Types.hpp"
#include "../core/Units.hpp"
#include "../core/Vector3.hpp"

namespace orca {

class Orbit {
public:
    /// Construct from orbital radius (distance from Earth's centre).
    ///
    /// @throws std::invalid_argument if the radius is not above Earth's surface, or the
    ///         inclination is outside [0, pi], or any argument is non-finite.
    ///
    /// RAAN and phase are normalised into `[0, 2*pi)`; they are cyclic, so an
    /// out-of-range value is a representation detail rather than an error. Inclination is
    /// conventionally bounded and is rejected instead.
    static Orbit fromRadius(Kilometres semiMajorAxisKm,
                            Radians inclinationRad,
                            Radians raanRad,
                            Radians phaseAtEpochRad);

    /// Construct from altitude above Earth's mean equatorial radius.
    ///
    /// This is the form scenario files use, because altitude is how orbits are described
    /// in practice. It converts to radius immediately; radius is what is stored.
    ///
    /// @throws std::invalid_argument on the same conditions as `fromRadius`.
    static Orbit fromAltitude(Kilometres altitudeKm,
                              Radians inclinationRad,
                              Radians raanRad,
                              Radians phaseAtEpochRad);

    // --- Stored state -------------------------------------------------------

    [[nodiscard]] Kilometres semiMajorAxisKm() const noexcept { return semiMajorAxisKm_; }
    [[nodiscard]] Radians inclinationRad() const noexcept { return inclinationRad_; }
    [[nodiscard]] Radians raanRad() const noexcept { return raanRad_; }
    [[nodiscard]] Radians phaseAtEpochRad() const noexcept { return phaseAtEpochRad_; }

    // --- Derived ------------------------------------------------------------

    [[nodiscard]] Kilometres altitudeKm() const noexcept;
    [[nodiscard]] Seconds periodSec() const noexcept;
    [[nodiscard]] double meanMotionRadPerSec() const noexcept;
    [[nodiscard]] KmPerSec speedKmPerSec() const noexcept;
    [[nodiscard]] OrbitBand band() const noexcept;

    // --- Propagation --------------------------------------------------------

    /// Phase at `timeSec`, normalised to `[0, 2*pi)`. Computed from `timeSec` directly.
    [[nodiscard]] Radians phaseAtRad(Seconds timeSec) const noexcept;

    /// ECI position at `timeSec`, in kilometres. Magnitude always equals
    /// `semiMajorAxisKm()`.
    [[nodiscard]] Vector3 positionAtKm(Seconds timeSec) const noexcept;

    friend bool operator==(const Orbit&, const Orbit&) noexcept = default;

private:
    Orbit(Kilometres semiMajorAxisKm,
          Radians inclinationRad,
          Radians raanRad,
          Radians phaseAtEpochRad) noexcept;

    Kilometres semiMajorAxisKm_;
    Radians inclinationRad_;
    Radians raanRad_;
    Radians phaseAtEpochRad_;
};

}  // namespace orca
