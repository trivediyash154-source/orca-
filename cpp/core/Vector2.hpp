#pragma once

/// @file Vector2.hpp
/// Minimal 2-vector.
///
/// This exists for one documented consumer: the engine-computed graph layout coordinates
/// shipped in `graphData.nodes[].layout` (API_CONTRACT.md §4.6). Layout is computed in
/// C++ specifically so the frontend does not run a force-directed algorithm — that would
/// be a real algorithm living in React, which ARCHITECTURE_REVIEW.md §2 identified and
/// rejected.
///
/// It is deliberately smaller than Vector3: layout coordinates are compared and offset,
/// never normalised or dotted. Operations are added when a consumer needs them.

#include <cmath>

namespace orca {

struct Vector2 {
    double x = 0.0;
    double y = 0.0;

    friend constexpr bool operator==(const Vector2&, const Vector2&) noexcept = default;

    constexpr Vector2& operator+=(const Vector2& o) noexcept {
        x += o.x; y += o.y;
        return *this;
    }
    constexpr Vector2& operator-=(const Vector2& o) noexcept {
        x -= o.x; y -= o.y;
        return *this;
    }
    constexpr Vector2& operator*=(double s) noexcept {
        x *= s; y *= s;
        return *this;
    }
};

[[nodiscard]] constexpr Vector2 operator+(Vector2 a, const Vector2& b) noexcept { return a += b; }
[[nodiscard]] constexpr Vector2 operator-(Vector2 a, const Vector2& b) noexcept { return a -= b; }
[[nodiscard]] constexpr Vector2 operator*(Vector2 v, double s) noexcept { return v *= s; }
[[nodiscard]] constexpr Vector2 operator*(double s, Vector2 v) noexcept { return v *= s; }

[[nodiscard]] constexpr double magnitudeSquared(const Vector2& v) noexcept {
    return v.x * v.x + v.y * v.y;
}

[[nodiscard]] inline double magnitude(const Vector2& v) noexcept {
    return std::sqrt(magnitudeSquared(v));
}

[[nodiscard]] inline double distance(const Vector2& a, const Vector2& b) noexcept {
    return magnitude(a - b);
}

}  // namespace orca
