#pragma once

/// @file Constants.hpp
/// Compiled-in constants.
///
/// This file contains PHYSICAL FACTS AND NUMERICAL TOLERANCES ONLY.
///
/// Everything that is a *modelling choice* — risk weights, phase penalties, severity
/// factors, profile weights, label caps, switching margins — lives in `configs/` and is
/// loaded at runtime. That separation is deliberate: it is what lets SIMULATION_MODEL.md
/// §11 claim "no magic numbers in code", and it means tuning the model never requires a
/// rebuild.
///
/// If you are about to add a constant here, ask whether it would ever be reasonable to
/// change it to explore the model. If yes, it belongs in `configs/`.

namespace orca {

// ---------------------------------------------------------------------------
// Physical constants (WGS-84)
// ---------------------------------------------------------------------------

/// Earth's standard gravitational parameter, km^3/s^2.
inline constexpr double kMuEarth = 398600.4418;

/// Earth's mean equatorial radius, km.
inline constexpr double kEarthRadiusKm = 6378.137;

// ---------------------------------------------------------------------------
// Mathematical constants
// ---------------------------------------------------------------------------

inline constexpr double kPi = 3.14159265358979323846;
inline constexpr double kTwoPi = 2.0 * kPi;
inline constexpr double kHalfPi = 0.5 * kPi;

// ---------------------------------------------------------------------------
// Numerical tolerances
// ---------------------------------------------------------------------------

/// General-purpose comparison tolerance for simulation-scale quantities.
///
/// Used by nearlyEqual() and, critically, by the dominance predicate in Phase 2+.
/// ALGORITHMS.md §6.5 explains why dominance needs an epsilon band: without one,
/// floating-point noise makes dominance non-transitive, which silently corrupts the
/// Pareto front. This is a correctness constant, not a cosmetic one.
inline constexpr double kEpsilon = 1e-9;

/// Tolerance for angular comparisons, radians.
inline constexpr double kAngleEpsilon = 1e-12;

// ---------------------------------------------------------------------------
// Orbital band boundaries
// ---------------------------------------------------------------------------
//
// Classification only — these carry no behaviour and exist so the UI can label a ring
// "LEO" or "GEO". They are conventional altitude boundaries, not physical thresholds.

/// Upper altitude bound of low Earth orbit, km.
inline constexpr double kLeoUpperAltitudeKm = 2000.0;

/// Upper altitude bound of medium Earth orbit, km.
inline constexpr double kMeoUpperAltitudeKm = 35000.0;

}  // namespace orca
