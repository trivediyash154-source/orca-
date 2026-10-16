#pragma once

/// @file Vector3.hpp
/// Minimal 3-vector for Earth-Centred Inertial positions.
///
/// This is a domain maths utility, not a linear-algebra library. It carries only the
/// operations ORCA actually performs: composition of positions, distance between
/// objects, and the normalisation used when reasoning about orbital planes. There is no
/// cross product, no matrix type, no quaternion — none of those have a consumer, and the
/// renderer will use Three.js's own vector types rather than these.
///
/// Frame: ECI. +Z along Earth's rotation axis, +X toward the scenario reference
/// direction at epoch. Non-rotating. Units are kilometres throughout.

#include <cmath>

#include "Constants.hpp"

namespace orca {

struct Vector3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    friend constexpr bool operator==(const Vector3&, const Vector3&) noexcept = default;

    constexpr Vector3& operator+=(const Vector3& o) noexcept {
        x += o.x; y += o.y; z += o.z;
        return *this;
    }
    constexpr Vector3& operator-=(const Vector3& o) noexcept {
        x -= o.x; y -= o.y; z -= o.z;
        return *this;
    }
    constexpr Vector3& operator*=(double s) noexcept {
        x *= s; y *= s; z *= s;
        return *this;
    }
    constexpr Vector3& operator/=(double s) noexcept {
        x /= s; y /= s; z /= s;
        return *this;
    }
};

[[nodiscard]] constexpr Vector3 operator+(Vector3 a, const Vector3& b) noexcept { return a += b; }
[[nodiscard]] constexpr Vector3 operator-(Vector3 a, const Vector3& b) noexcept { return a -= b; }
[[nodiscard]] constexpr Vector3 operator*(Vector3 v, double s) noexcept { return v *= s; }
[[nodiscard]] constexpr Vector3 operator*(double s, Vector3 v) noexcept { return v *= s; }
[[nodiscard]] constexpr Vector3 operator/(Vector3 v, double s) noexcept { return v /= s; }
[[nodiscard]] constexpr Vector3 operator-(const Vector3& v) noexcept {
    return Vector3{-v.x, -v.y, -v.z};
}

[[nodiscard]] constexpr double dot(const Vector3& a, const Vector3& b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

/// Squared magnitude. Preferred over `magnitude()` for comparisons — it avoids a square
/// root, and the debris proximity checks in Phase 9 run this in their inner loop.
[[nodiscard]] constexpr double magnitudeSquared(const Vector3& v) noexcept {
    return dot(v, v);
}

[[nodiscard]] inline double magnitude(const Vector3& v) noexcept {
    return std::sqrt(magnitudeSquared(v));
}

[[nodiscard]] constexpr double distanceSquared(const Vector3& a, const Vector3& b) noexcept {
    return magnitudeSquared(a - b);
}

[[nodiscard]] inline double distance(const Vector3& a, const Vector3& b) noexcept {
    return magnitude(a - b);
}

/// Unit vector in the direction of `v`.
///
/// Returns the zero vector for input of near-zero magnitude rather than producing NaN or
/// throwing. A zero-length direction has no meaningful normalisation, and every ORCA call
/// site (orbital plane reasoning) treats "no direction" as a valid, non-exceptional state.
/// Callers that need to distinguish should test `magnitudeSquared` themselves.
[[nodiscard]] inline Vector3 normalized(const Vector3& v) noexcept {
    const double m = magnitude(v);
    if (m < kEpsilon) {
        return Vector3{};
    }
    return v / m;
}

[[nodiscard]] inline bool isFinite(const Vector3& v) noexcept {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

}  // namespace orca
