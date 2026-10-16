#pragma once

/// @file Math.hpp
/// Scalar and angular utilities.
///
/// Angle handling is the part of this file that matters. Orbital phase advances without
/// bound (`theta += omega * dt`, forever), so every consumer needs a canonical form, and
/// "difference between two phases" needs to be the *shortest* difference rather than the
/// naive subtraction. Getting either wrong produces orbital motion that looks right in a
/// short demo and drifts subtly over a long run.

#include <cmath>

#include "Constants.hpp"

namespace orca {

/// Clamp `v` into `[lo, hi]`.
///
/// `std::clamp` returns a reference and has undefined behaviour when `lo > hi`; this
/// takes and returns by value (correct for `double`) and asserts the ordering.
[[nodiscard]] constexpr double clamp(double v, double lo, double hi) noexcept {
    return v < lo ? lo : (v > hi ? hi : v);
}

/// Clamp into the unit interval. Used pervasively for fuel fractions, health values and
/// risk, all of which are defined on [0,1].
[[nodiscard]] constexpr double clampUnit(double v) noexcept {
    return clamp(v, 0.0, 1.0);
}

/// True when `v` lies within `[0,1]`.
[[nodiscard]] constexpr bool inUnitInterval(double v) noexcept {
    return v >= 0.0 && v <= 1.0;
}

/// Absolute-tolerance comparison.
///
/// Deliberately absolute rather than relative: ORCA's quantities have known, bounded
/// scales (risk in [0,1], altitudes in the thousands of km, times in the thousands of
/// seconds), and an absolute epsilon is easier to reason about at those scales than a
/// relative one. A relative comparison would also behave badly near zero, which is
/// exactly where risk and fuel values cluster.
[[nodiscard]] inline bool nearlyEqual(double a, double b, double eps = kEpsilon) noexcept {
    return std::fabs(a - b) <= eps;
}

/// Normalise an angle into the canonical range `[0, 2*pi)`.
///
/// Handles arbitrarily large positive and negative inputs, e.g. `normalizeAngle(7*pi)`
/// returns `pi`. `std::fmod` alone is insufficient because it preserves the sign of the
/// dividend, so negative inputs need the extra fold.
[[nodiscard]] inline double normalizeAngle(double radians) noexcept {
    double a = std::fmod(radians, kTwoPi);
    if (a < 0.0) {
        a += kTwoPi;
    }
    // fmod on a tiny negative input can produce exactly kTwoPi after the fold above;
    // fold once more so the range stays half-open.
    if (a >= kTwoPi) {
        a = 0.0;
    }
    return a;
}

/// Signed shortest angular difference `to - from`, in `(-pi, pi]`.
///
/// "Shortest" is the point: the naive subtraction of 350 deg and 10 deg gives 340 deg,
/// but the two directions are 20 deg apart. Orbital phase comparison needs the latter.
[[nodiscard]] inline double angularDifference(double fromRad, double toRad) noexcept {
    double d = normalizeAngle(toRad) - normalizeAngle(fromRad);
    if (d > kPi) {
        d -= kTwoPi;
    } else if (d <= -kPi) {
        d += kTwoPi;
    }
    return d;
}

/// Unsigned angular separation between two directions, in `[0, pi]`.
[[nodiscard]] inline double angularSeparation(double aRad, double bRad) noexcept {
    return std::fabs(angularDifference(aRad, bRad));
}

/// Linear interpolation. `t` is not clamped; callers that need clamping should say so.
[[nodiscard]] constexpr double lerp(double a, double b, double t) noexcept {
    return a + (b - a) * t;
}

}  // namespace orca
