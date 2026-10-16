/// Tests for Vector2 and Vector3.

#include <cmath>
#include <limits>

#include "../../cpp/core/Vector2.hpp"
#include "../../cpp/core/Vector3.hpp"
#include "../TestFramework.hpp"

using namespace orca;

// ---------------------------------------------------------------------------
// Vector3
// ---------------------------------------------------------------------------

ORCA_TEST(Vector3, Addition) {
    const Vector3 a{1.0, 2.0, 3.0};
    const Vector3 b{4.0, -5.0, 6.0};
    const Vector3 sum = a + b;
    CHECK_NEAR(sum.x, 5.0, 1e-12);
    CHECK_NEAR(sum.y, -3.0, 1e-12);
    CHECK_NEAR(sum.z, 9.0, 1e-12);
}

ORCA_TEST(Vector3, Subtraction) {
    const Vector3 a{1.0, 2.0, 3.0};
    const Vector3 b{4.0, -5.0, 6.0};
    const Vector3 diff = a - b;
    CHECK_NEAR(diff.x, -3.0, 1e-12);
    CHECK_NEAR(diff.y, 7.0, 1e-12);
    CHECK_NEAR(diff.z, -3.0, 1e-12);
}

ORCA_TEST(Vector3, ScalarMultiplyAndDivide) {
    const Vector3 v{2.0, -4.0, 6.0};
    const Vector3 scaled = v * 2.5;
    CHECK_NEAR(scaled.x, 5.0, 1e-12);
    CHECK_NEAR(scaled.y, -10.0, 1e-12);
    CHECK_NEAR(scaled.z, 15.0, 1e-12);

    // Scalar multiplication must commute.
    const Vector3 commuted = 2.5 * v;
    CHECK(commuted == scaled);

    const Vector3 halved = v / 2.0;
    CHECK_NEAR(halved.x, 1.0, 1e-12);
    CHECK_NEAR(halved.z, 3.0, 1e-12);
}

ORCA_TEST(Vector3, Negation) {
    const Vector3 v{1.0, -2.0, 3.0};
    const Vector3 n = -v;
    CHECK(n == Vector3{-1.0, 2.0, -3.0});
    CHECK(v + n == Vector3{0.0, 0.0, 0.0});
}

ORCA_TEST(Vector3, MagnitudeOfPythagoreanQuadruple) {
    // 1^2 + 2^2 + 2^2 = 9, so |v| = 3 exactly. Chosen so the expected value is an
    // integer known without evaluating a square root.
    CHECK_NEAR(magnitude(Vector3{1.0, 2.0, 2.0}), 3.0, 1e-12);
    // 2^2 + 3^2 + 6^2 = 49, so |v| = 7 exactly.
    CHECK_NEAR(magnitude(Vector3{2.0, 3.0, 6.0}), 7.0, 1e-12);
}

ORCA_TEST(Vector3, MagnitudeSquaredAvoidsSquareRoot) {
    const Vector3 v{1.0, 2.0, 2.0};
    CHECK_NEAR(magnitudeSquared(v), 9.0, 1e-12);
    CHECK_NEAR(magnitudeSquared(v), magnitude(v) * magnitude(v), 1e-12);
}

ORCA_TEST(Vector3, DotProduct) {
    // Hand-computed: 1*4 + 2*(-5) + 3*6 = 4 - 10 + 18 = 12.
    CHECK_NEAR(dot(Vector3{1.0, 2.0, 3.0}, Vector3{4.0, -5.0, 6.0}), 12.0, 1e-12);
    // Orthogonal axes have zero dot product.
    CHECK_NEAR(dot(Vector3{1.0, 0.0, 0.0}, Vector3{0.0, 1.0, 0.0}), 0.0, 1e-12);
    // Dot with self is magnitude squared.
    const Vector3 v{3.0, -4.0, 12.0};
    CHECK_NEAR(dot(v, v), magnitudeSquared(v), 1e-12);
}

ORCA_TEST(Vector3, DistanceToSelfIsZero) {
    // Property: distance(a, a) == 0 for arbitrary a.
    const Vector3 samples[] = {
        {0.0, 0.0, 0.0}, {1.0, 2.0, 3.0}, {-7000.5, 1234.25, -88.125}, {1e6, -1e6, 1e6}};
    for (const auto& a : samples) {
        CHECK_NEAR(distance(a, a), 0.0, 1e-12);
    }
}

ORCA_TEST(Vector3, DistanceIsSymmetric) {
    const Vector3 a{1.0, 2.0, 3.0};
    const Vector3 b{-4.0, 5.5, 0.25};
    CHECK_NEAR(distance(a, b), distance(b, a), 1e-12);
    // Hand-computed: diff = (5, -3.5, 2.75); 25 + 12.25 + 7.5625 = 44.8125.
    CHECK_NEAR(distanceSquared(a, b), 44.8125, 1e-12);
}

ORCA_TEST(Vector3, NormalizedHasUnitMagnitude) {
    // Property: |normalize(v)| ~= 1 for every non-degenerate v.
    const Vector3 samples[] = {{1.0, 0.0, 0.0},
                               {1.0, 1.0, 1.0},
                               {-3.0, 4.0, 0.0},
                               {6878.137, -1234.5, 900.25},
                               {1e-6, 1e-6, 1e-6}};
    for (const auto& v : samples) {
        CHECK_NEAR(magnitude(normalized(v)), 1.0, 1e-12);
    }
}

ORCA_TEST(Vector3, NormalizedPreservesDirection) {
    const Vector3 v{0.0, -5.0, 0.0};
    const Vector3 u = normalized(v);
    CHECK_NEAR(u.x, 0.0, 1e-12);
    CHECK_NEAR(u.y, -1.0, 1e-12);
    CHECK_NEAR(u.z, 0.0, 1e-12);
}

ORCA_TEST(Vector3, NormalizedZeroVectorReturnsZeroNotNaN) {
    // Documented behaviour: a zero-length direction has no meaningful normalisation, so
    // it returns zero rather than producing NaN. Verified explicitly because a NaN here
    // would propagate silently through every downstream calculation.
    const Vector3 z = normalized(Vector3{0.0, 0.0, 0.0});
    CHECK(z == Vector3{0.0, 0.0, 0.0});
    CHECK(isFinite(z));
}

ORCA_TEST(Vector3, IsFiniteDetectsNonFinite) {
    CHECK(isFinite(Vector3{1.0, 2.0, 3.0}));
    const double inf = std::numeric_limits<double>::infinity();
    CHECK(!isFinite(Vector3{inf, 0.0, 0.0}));
    CHECK(!isFinite(Vector3{0.0, std::nan(""), 0.0}));
}

// ---------------------------------------------------------------------------
// Vector2
// ---------------------------------------------------------------------------

ORCA_TEST(Vector2, ArithmeticAndMagnitude) {
    const Vector2 a{3.0, 4.0};
    CHECK_NEAR(magnitude(a), 5.0, 1e-12);  // 3-4-5 triangle
    CHECK_NEAR(magnitudeSquared(a), 25.0, 1e-12);

    const Vector2 b{-1.0, 2.0};
    CHECK(a + b == Vector2{2.0, 6.0});
    CHECK(a - b == Vector2{4.0, 2.0});
    CHECK(a * 2.0 == Vector2{6.0, 8.0});
    CHECK(2.0 * a == a * 2.0);
}

ORCA_TEST(Vector2, DistanceToSelfIsZero) {
    const Vector2 a{12.5, -3.25};
    CHECK_NEAR(distance(a, a), 0.0, 1e-12);
    CHECK_NEAR(distance(Vector2{0.0, 0.0}, Vector2{3.0, 4.0}), 5.0, 1e-12);
}
