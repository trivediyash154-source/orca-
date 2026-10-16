/// The dominance test matrix: every case Phase 5 brief §42 asks for, plus the property
/// tests ALGORITHMS.md §6.4 documents (irreflexive, asymmetric, transitive, partial).
///
/// This file tests `dominates()`, `isEquivalent()` and `dominatesWithEpsilon()` in
/// isolation, with hand-chosen `ObjectiveVector`s — no orbital mechanics, no graph, no
/// search. These three predicates are, per `Dominance.hpp`'s file comment, "the single
/// most reused predicate in the system," and every correctness argument `ParetoSearch.cpp`
/// makes rests on them being right independent of anything built on top of them.

#include <cmath>
#include <limits>

#include "../../cpp/algorithms/Dominance.hpp"
#include "../TestFramework.hpp"

using namespace orca;

namespace {

ObjectiveVector v(double time, double fuel, double risk, double distance) {
    return ObjectiveVector{time, fuel, risk, distance};
}

}  // namespace

// ---------------------------------------------------------------------------
// The core matrix (Phase 5 brief §42)
// ---------------------------------------------------------------------------

ORCA_TEST(Dominance, ADominatesB) {
    const ObjectiveVector a = v(10.0, 20.0, 0.1, 100.0);
    const ObjectiveVector b = v(15.0, 25.0, 0.2, 150.0);
    CHECK(dominates(a, b));
    CHECK(!dominates(b, a));
}

ORCA_TEST(Dominance, BDominatesA) {
    const ObjectiveVector a = v(15.0, 25.0, 0.2, 150.0);
    const ObjectiveVector b = v(10.0, 20.0, 0.1, 100.0);
    CHECK(dominates(b, a));
    CHECK(!dominates(a, b));
}

ORCA_TEST(Dominance, NeitherDominatesGenuineTradeOff) {
    // The brief's own worked example (§18): A is faster, B burns less fuel.
    const ObjectiveVector a = v(10.0, 30.0, 0.0, 0.0);
    const ObjectiveVector b = v(12.0, 15.0, 0.0, 0.0);
    CHECK(!dominates(a, b));
    CHECK(!dominates(b, a));
}

ORCA_TEST(Dominance, EqualVectorsNeitherDominates) {
    const ObjectiveVector a = v(10.0, 20.0, 0.1, 100.0);
    const ObjectiveVector b = v(10.0, 20.0, 0.1, 100.0);
    CHECK(!dominates(a, b));
    CHECK(!dominates(b, a));
    // Equal is not "incomparable" -- it is a distinct, named concept (Dominance.hpp).
    CHECK(isEquivalent(a, b));
}

ORCA_TEST(Dominance, OneStrictlyBetterObjectiveOthersEqual) {
    const ObjectiveVector a = v(10.0, 20.0, 0.1, 100.0);
    const ObjectiveVector b = v(10.0, 20.0, 0.1, 105.0);  // only distance differs
    CHECK(dominates(a, b));
    CHECK(!dominates(b, a));
}

ORCA_TEST(Dominance, FloatingPointNearEqualityIsNotDominance) {
    // A difference far below kDominanceNoiseEpsilon must not register as a real
    // improvement -- otherwise floating-point noise on independently computed values
    // would make dominance non-transitive (ALGORITHMS.md §6.5).
    const ObjectiveVector a = v(10.0, 20.0, 0.1, 100.0);
    const ObjectiveVector b = v(10.0 + kDominanceNoiseEpsilon * 0.1, 20.0, 0.1, 100.0);
    CHECK(!dominates(a, b));
    CHECK(!dominates(b, a));
    CHECK(isEquivalent(a, b));
}

ORCA_TEST(Dominance, FloatingPointDifferenceJustOverEpsilonIsDominance) {
    const ObjectiveVector a = v(10.0, 20.0, 0.1, 100.0);
    const ObjectiveVector b = v(10.0 + kDominanceNoiseEpsilon * 10.0, 20.0, 0.1, 100.0);
    CHECK(dominates(a, b));
    CHECK(!isEquivalent(a, b));
}

ORCA_TEST(Dominance, AllFourObjectivesParticipate) {
    // Confirms the loop genuinely covers all four dimensions, not just time/fuel.
    const ObjectiveVector a = v(10.0, 10.0, 0.1, 10.0);
    CHECK(dominates(a, v(11.0, 10.0, 0.1, 10.0)));  // time
    CHECK(dominates(a, v(10.0, 11.0, 0.1, 10.0)));  // fuel
    CHECK(dominates(a, v(10.0, 10.0, 0.2, 10.0)));  // risk
    CHECK(dominates(a, v(10.0, 10.0, 0.1, 11.0)));  // distance
}

ORCA_TEST(Dominance, NonFiniteValuesNeverDominate) {
    // dominates() is a raw function on ObjectiveVector -- it does not itself enforce
    // isValid() (that is ObjectiveVector's own invariant, checked at construction
    // boundaries elsewhere). Confirmed here: NaN/Inf comparisons are always false in both
    // directions rather than producing a nonsensical "dominates" verdict, since every
    // comparison against NaN is false in IEEE 754, which correctly makes NaN neither
    // better nor worse than anything -- never silently "wins."
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const ObjectiveVector a = v(nan, 20.0, 0.1, 100.0);
    const ObjectiveVector b = v(10.0, 20.0, 0.1, 100.0);
    CHECK(!dominates(a, b));
    CHECK(!dominates(b, a));

    const double inf = std::numeric_limits<double>::infinity();
    const ObjectiveVector c = v(inf, 20.0, 0.1, 100.0);
    CHECK(!dominates(c, b));  // infinite time is never "no worse"
    CHECK(dominates(b, c));   // b is strictly better in time, no worse elsewhere
}

// ---------------------------------------------------------------------------
// Equivalence / duplicate handling (Phase 5 brief §9)
// ---------------------------------------------------------------------------

ORCA_TEST(Dominance, EquivalenceIsNotTheSameAsIncomparability) {
    // Two genuinely incomparable vectors (a trade-off) must NOT be reported equivalent.
    const ObjectiveVector a = v(10.0, 30.0, 0.0, 0.0);
    const ObjectiveVector b = v(12.0, 15.0, 0.0, 0.0);
    CHECK(!isEquivalent(a, b));
}

ORCA_TEST(Dominance, EquivalenceIsSymmetric) {
    const ObjectiveVector a = v(10.0, 20.0, 0.1, 100.0);
    const ObjectiveVector b = v(10.0, 20.0, 0.1, 100.0);
    CHECK(isEquivalent(a, b));
    CHECK(isEquivalent(b, a));
}

// ---------------------------------------------------------------------------
// Properties (ALGORITHMS.md §6.4) -- checked directly, not merely assumed.
// ---------------------------------------------------------------------------

ORCA_TEST(Dominance, IsIrreflexive) {
    const ObjectiveVector a = v(10.0, 20.0, 0.1, 100.0);
    CHECK(!dominates(a, a));
}

ORCA_TEST(Dominance, IsAsymmetric) {
    const ObjectiveVector a = v(10.0, 20.0, 0.1, 100.0);
    const ObjectiveVector b = v(15.0, 25.0, 0.2, 150.0);
    CHECK(dominates(a, b));
    CHECK(!dominates(b, a));
}

ORCA_TEST(Dominance, IsTransitive) {
    const ObjectiveVector a = v(10.0, 20.0, 0.1, 100.0);
    const ObjectiveVector b = v(15.0, 25.0, 0.2, 150.0);
    const ObjectiveVector c = v(20.0, 30.0, 0.3, 200.0);
    CHECK(dominates(a, b));
    CHECK(dominates(b, c));
    CHECK(dominates(a, c));
}

ORCA_TEST(Dominance, IsPartialNotTotal) {
    // The entire justification for a *set* of solutions: not every pair is comparable.
    const ObjectiveVector a = v(10.0, 30.0, 0.0, 0.0);
    const ObjectiveVector b = v(12.0, 15.0, 0.0, 0.0);
    CHECK(!dominates(a, b));
    CHECK(!dominates(b, a));
    CHECK(!isEquivalent(a, b));
}

// ---------------------------------------------------------------------------
// dominatesWithEpsilon -- the explicit, opt-in approximate mode.
// ---------------------------------------------------------------------------

ORCA_TEST(DominatesWithEpsilon, ZeroEpsilonMatchesExactDominance) {
    const ObjectiveVector a = v(10.0, 30.0, 0.0, 0.0);
    const ObjectiveVector b = v(12.0, 15.0, 0.0, 0.0);
    CHECK_EQ(dominatesWithEpsilon(a, b, 0.0), dominates(a, b));
    CHECK_EQ(dominatesWithEpsilon(b, a, 0.0), dominates(b, a));

    const ObjectiveVector c = v(10.0, 20.0, 0.1, 100.0);
    const ObjectiveVector d = v(15.0, 25.0, 0.2, 150.0);
    CHECK_EQ(dominatesWithEpsilon(c, d, 0.0), dominates(c, d));
}

ORCA_TEST(DominatesWithEpsilon, LargeEpsilonCollapsesNearIdenticalLabels) {
    // A genuine trade-off under EXACT dominance (a is faster, b travels less distance --
    // neither wins outright), but the loser's margin on its bad dimension is small enough
    // that a generous epsilon treats it as no worse, collapsing the trade-off. This is
    // ε-dominance's actual approximation power: not "epsilon makes exact-dominant pairs
    // dominate" (they already do), but "epsilon makes some genuinely-incomparable pairs
    // dominate."
    const ObjectiveVector a = v(100.0, 10.0, 0.0, 50.0);  // faster, more distance
    const ObjectiveVector b = v(105.0, 10.0, 0.0, 45.0);  // slower, less distance
    CHECK(!dominates(a, b));
    CHECK(!dominates(b, a));

    // a's distance (50) is within 15% of b's (45 * 1.15 = 51.75) -- collapses.
    CHECK(dominatesWithEpsilon(a, b, 0.15));
    // ...but not within 5% (45 * 1.05 = 47.25 < 50) -- the threshold genuinely matters,
    // this is not a fixed behaviour regardless of the epsilon value passed.
    CHECK(!dominatesWithEpsilon(a, b, 0.05));
}

ORCA_TEST(DominatesWithEpsilon, IsIrreflexiveForAnyEpsilon) {
    const ObjectiveVector a = v(10.0, 20.0, 0.1, 100.0);
    CHECK(!dominatesWithEpsilon(a, a, 0.0));
    CHECK(!dominatesWithEpsilon(a, a, 0.5));
    CHECK(!dominatesWithEpsilon(a, a, 10.0));
}

ORCA_TEST(DominatesWithEpsilon, RejectsNegativeEpsilon) {
    const ObjectiveVector a = v(10.0, 20.0, 0.1, 100.0);
    const ObjectiveVector b = v(15.0, 25.0, 0.2, 150.0);
    CHECK_THROWS(dominatesWithEpsilon(a, b, -0.1));
}
