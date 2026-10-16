/// Tests for front-relative normalisation (OPTIMIZATION_MODEL.md §5): the formula itself,
/// the degenerate (constant-objective) case, and `computeIdeal`/`computeNadir`.

#include "../../cpp/optimization/ObjectiveNormalizer.hpp"
#include "../TestFramework.hpp"

using namespace orca;

namespace {

ObjectiveVector v(double time, double fuel, double risk, double distance) {
    return ObjectiveVector{time, fuel, risk, distance};
}

}  // namespace

ORCA_TEST(ObjectiveNormalizer, ComputeIdealIsComponentwiseMinimum) {
    const std::vector<ObjectiveVector> front = {v(10, 5, 0.1, 100), v(20, 2, 0.3, 50),
                                                v(15, 8, 0.05, 200)};
    const ObjectiveVector ideal = computeIdeal(front);
    CHECK_NEAR(ideal.time, 10.0, 1e-12);
    CHECK_NEAR(ideal.fuel, 2.0, 1e-12);
    CHECK_NEAR(ideal.risk, 0.05, 1e-12);
    CHECK_NEAR(ideal.distance, 50.0, 1e-12);
}

ORCA_TEST(ObjectiveNormalizer, ComputeNadirIsComponentwiseMaximum) {
    const std::vector<ObjectiveVector> front = {v(10, 5, 0.1, 100), v(20, 2, 0.3, 50),
                                                v(15, 8, 0.05, 200)};
    const ObjectiveVector nadir = computeNadir(front);
    CHECK_NEAR(nadir.time, 20.0, 1e-12);
    CHECK_NEAR(nadir.fuel, 8.0, 1e-12);
    CHECK_NEAR(nadir.risk, 0.3, 1e-12);
    CHECK_NEAR(nadir.distance, 200.0, 1e-12);
}

ORCA_TEST(ObjectiveNormalizer, IdealAndNadirOfEmptyFrontAreZero) {
    const std::vector<ObjectiveVector> front;
    CHECK(computeIdeal(front) == ObjectiveVector{});
    CHECK(computeNadir(front) == ObjectiveVector{});
}

ORCA_TEST(ObjectiveNormalizer, MidpointNormalisesToOneHalf) {
    const ObjectiveVector ideal = v(0, 0, 0, 0);
    const ObjectiveVector nadir = v(100, 10, 1.0, 1000);
    const ObjectiveVector cost = v(50, 5, 0.5, 500);
    const NormalizedObjectives n = normalize(cost, ideal, nadir);
    CHECK_NEAR(n.time, 0.5, 1e-12);
    CHECK_NEAR(n.fuel, 0.5, 1e-12);
    CHECK_NEAR(n.risk, 0.5, 1e-12);
    CHECK_NEAR(n.distance, 0.5, 1e-12);
}

ORCA_TEST(ObjectiveNormalizer, IdealNormalisesToZeroNadirToOne) {
    const ObjectiveVector ideal = v(10, 1, 0.1, 100);
    const ObjectiveVector nadir = v(50, 9, 0.9, 500);
    const NormalizedObjectives atIdeal = normalize(ideal, ideal, nadir);
    CHECK_NEAR(atIdeal.time, 0.0, 1e-12);
    CHECK_NEAR(atIdeal.fuel, 0.0, 1e-12);
    CHECK_NEAR(atIdeal.risk, 0.0, 1e-12);
    CHECK_NEAR(atIdeal.distance, 0.0, 1e-12);

    const NormalizedObjectives atNadir = normalize(nadir, ideal, nadir);
    CHECK_NEAR(atNadir.time, 1.0, 1e-12);
    CHECK_NEAR(atNadir.fuel, 1.0, 1e-12);
    CHECK_NEAR(atNadir.risk, 1.0, 1e-12);
    CHECK_NEAR(atNadir.distance, 1.0, 1e-12);
}

ORCA_TEST(ObjectiveNormalizer, DegenerateConstantObjectiveNormalisesToZeroNotNaN) {
    // Single-member front: every objective is trivially constant (ideal == nadir on all
    // four dimensions). OPTIMIZATION_MODEL.md §5.2's specified answer: norm = 0, not NaN
    // or a divide-by-zero crash, and the plan is selected regardless of weights on that
    // dimension (verified at the MissionScorer level too).
    const ObjectiveVector onlyPlan = v(500.0, 0.3, 0.2, 1000.0);
    const NormalizedObjectives n = normalize(onlyPlan, onlyPlan, onlyPlan);
    CHECK_NEAR(n.time, 0.0, 1e-12);
    CHECK_NEAR(n.fuel, 0.0, 1e-12);
    CHECK_NEAR(n.risk, 0.0, 1e-12);
    CHECK_NEAR(n.distance, 0.0, 1e-12);
}

ORCA_TEST(ObjectiveNormalizer, DegenerateOnJustOneDimensionStillNormalisesOthers) {
    // time is constant across the front (ideal.time == nadir.time), but the other three
    // vary -- only time should collapse to 0, the rest normalise normally.
    const ObjectiveVector ideal = v(100.0, 0.0, 0.0, 0.0);
    const ObjectiveVector nadir = v(100.0, 10.0, 1.0, 1000.0);
    const ObjectiveVector cost = v(100.0, 5.0, 0.5, 500.0);
    const NormalizedObjectives n = normalize(cost, ideal, nadir);
    CHECK_NEAR(n.time, 0.0, 1e-12);  // degenerate dimension
    CHECK_NEAR(n.fuel, 0.5, 1e-12);
    CHECK_NEAR(n.risk, 0.5, 1e-12);
    CHECK_NEAR(n.distance, 0.5, 1e-12);
}

ORCA_TEST(ObjectiveNormalizer, RejectsInconsistentBounds) {
    // nadir strictly less than ideal on some dimension is a caller error -- ideal/nadir
    // are always extrema of the same front, so this should never happen legitimately.
    const ObjectiveVector ideal = v(50.0, 0.0, 0.0, 0.0);
    const ObjectiveVector nadir = v(10.0, 10.0, 1.0, 1000.0);  // nadir.time < ideal.time
    const ObjectiveVector cost = v(30.0, 5.0, 0.5, 500.0);
    CHECK_THROWS(normalize(cost, ideal, nadir));
}
