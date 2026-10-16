#pragma once

/// @file SimplifiedOrbitalModel.hpp
/// ORCA's orbital mechanics — deliberately named to state what it is.
///
/// # THIS IS A SIMULATION MODEL, NOT AN ASTRODYNAMICS ENGINE
///
/// It exists to produce a physically plausible and internally consistent optimization
/// problem. It is not validated for operational use and must never be described as if it
/// were. The name of this file is part of that commitment: an API called
/// `OrbitalMechanics` invites the reader to assume more than ORCA delivers.
///
/// ## The model
///
/// Circular, two-body, analytically propagated. No numerical integration.
///
///     T(a) = 2*pi * sqrt(a^3 / mu)          orbital period, s
///     w(a) = 2*pi / T(a) = sqrt(mu / a^3)   mean motion, rad/s
///     v(a) = sqrt(mu / a)                   circular speed, km/s
///     theta(t) = theta_0 + w * t            phase at time t
///
/// ## Why analytic rather than stepped
///
/// `theta(t)` is computed from `t` directly rather than accumulated as
/// `theta += w * dt`. Three consequences, all of which ORCA depends on:
///
///   1. **No integration drift.** The state at t = 5000 is identical whether reached in
///      one step or five thousand. This is asserted by a test.
///   2. **Determinism is free.** There is no accumulated floating-point history to
///      diverge between runs or platforms.
///   3. **Time is reversible.** The presentation mode can scrub backwards, and replanning
///      can evaluate a transfer departing at any time without re-simulating to get there.
///
/// It is also O(1) per object per tick, which matters at the 1000-object benchmark point.
///
/// ## What this model does not do
///
/// No eccentricity, no J2, no drag, no solar radiation pressure, no third-body effects,
/// no finite burns. Orbits never decay and never precess. The full list of simplifications
/// and their consequences is SIMULATION_MODEL.md §8.
///
/// ## Validation
///
/// The formulae below reproduce published textbook values (REPOSITORY_AUDIT.md §4):
///
///   | Quantity                    | Model     | Reference  |
///   |-----------------------------|-----------|------------|
///   | Circular velocity @ 500 km  | 7.6126 km/s | ~7.61 km/s |
///   | Orbital period @ 500 km     | 94.62 min   | ~94.6 min  |
///   | Orbital period @ 800 km     | 100.87 min  | ~100.9 min |
///
/// These are pinned by tests with independently-derived expected values.

#include "../core/Constants.hpp"
#include "../core/Types.hpp"
#include "../core/Units.hpp"
#include "../core/Vector3.hpp"

namespace orca::simplified_orbital_model {

/// Orbital period of a circular orbit of radius `radiusKm`, in seconds.
///
///     T = 2*pi * sqrt(r^3 / mu)
///
/// Requires `radiusKm > 0`.
[[nodiscard]] Seconds orbitalPeriodSec(Kilometres radiusKm) noexcept;

/// Mean motion (angular velocity) of a circular orbit, in radians per second.
///
///     w = sqrt(mu / r^3)
///
/// Computed directly rather than as `2*pi / T` so that it does not inherit the rounding
/// of a separately-computed period.
[[nodiscard]] double meanMotionRadPerSec(Kilometres radiusKm) noexcept;

/// Speed of a circular orbit, in km/s.
///
///     v = sqrt(mu / r)
[[nodiscard]] KmPerSec circularSpeedKmPerSec(Kilometres radiusKm) noexcept;

/// Phase at `timeSec`, given the phase at epoch and the orbit radius.
///
/// Computed from `timeSec` directly, not accumulated. Result is normalised to
/// `[0, 2*pi)`.
[[nodiscard]] Radians phaseAtRad(Radians phaseAtEpochRad,
                                 Kilometres radiusKm,
                                 Seconds timeSec) noexcept;

/// Position in the Earth-Centred Inertial frame, in kilometres.
///
///     x = r * (cos(raan)*cos(theta) - sin(raan)*sin(theta)*cos(i))
///     y = r * (sin(raan)*cos(theta) + cos(raan)*sin(theta)*cos(i))
///     z = r * (sin(theta)*sin(i))
///
/// For a circular orbit the magnitude of the result is exactly `r` for all angles — the
/// cross terms cancel algebraically. A test asserts this, which catches any future
/// transcription error in the rotation.
[[nodiscard]] Vector3 positionKm(Kilometres radiusKm,
                                 Radians inclinationRad,
                                 Radians raanRad,
                                 Radians phaseRad) noexcept;

/// Classify an orbit by altitude. Presentation only; carries no behaviour.
[[nodiscard]] OrbitBand classifyBand(Kilometres radiusKm) noexcept;

}  // namespace orca::simplified_orbital_model
