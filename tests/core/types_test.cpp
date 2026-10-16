/// Tests for strongly-typed IDs and the objective vector.

#include <cmath>
#include <limits>
#include <string>
#include <type_traits>
#include <unordered_set>

#include "../../cpp/core/ObjectiveVector.hpp"
#include "../../cpp/core/Types.hpp"
#include "../TestFramework.hpp"

using namespace orca;

// ---------------------------------------------------------------------------
// Identifiers
// ---------------------------------------------------------------------------

ORCA_TEST(Ids, DistinctTypesAreNotInterchangeable) {
    // The entire justification for wrapping IDs. If these ever became convertible, the
    // graph builder could silently pass a vehicle id where a satellite id is expected.
    static_assert(!std::is_convertible_v<SatelliteId, VehicleId>);
    static_assert(!std::is_convertible_v<VehicleId, SatelliteId>);
    static_assert(!std::is_convertible_v<EmergencyId, MissionId>);
    static_assert(!std::is_convertible_v<NodeId, EdgeId>);

    // And a raw integer cannot become an ID implicitly either.
    static_assert(!std::is_convertible_v<std::uint32_t, SatelliteId>);
    CHECK(true);
}

ORCA_TEST(Ids, HaveNoDefaultConstructor) {
    // Absence is std::optional<Id>, never a sentinel. A default-constructible ID would
    // read as a real entity (SAT-000) when it is actually uninitialised.
    static_assert(!std::is_default_constructible_v<SatelliteId>);
    static_assert(!std::is_default_constructible_v<MissionId>);
    CHECK(true);
}

ORCA_TEST(Ids, AreZeroOverhead) {
    // The wrapper must cost nothing at runtime, or the safety argument is a trade rather
    // than a free win.
    static_assert(sizeof(SatelliteId) == sizeof(std::uint32_t));
    static_assert(std::is_trivially_copyable_v<SatelliteId>);
    CHECK(true);
}

ORCA_TEST(Ids, CompareByValue) {
    CHECK(SatelliteId{17} == SatelliteId{17});
    CHECK(!(SatelliteId{17} == SatelliteId{18}));
    CHECK(SatelliteId{17} < SatelliteId{18});
    CHECK_EQ(SatelliteId{17}.value(), std::uint32_t{17});
}

ORCA_TEST(Ids, FormatWithPrefixAndPadding) {
    // These strings appear in logs and error messages, and mirror the wire format.
    CHECK_EQ(toString(SatelliteId{17}), std::string{"SAT-017"});
    CHECK_EQ(toString(SatelliteId{0}), std::string{"SAT-000"});
    CHECK_EQ(toString(SatelliteId{1234}), std::string{"SAT-1234"});  // overflows the pad
    CHECK_EQ(toString(VehicleId{2}), std::string{"RV-02"});
    CHECK_EQ(toString(EmergencyId{3}), std::string{"E-003"});
    CHECK_EQ(toString(MissionId{1}), std::string{"M-001"});
    CHECK_EQ(toString(DebrisId{4}), std::string{"D-04"});
    CHECK_EQ(toString(PlanId{7}), std::string{"P-0007"});
    CHECK_EQ(toString(NodeId{42}), std::string{"N-0042"});
}

ORCA_TEST(Ids, AreHashable) {
    std::unordered_set<SatelliteId> seen;
    seen.insert(SatelliteId{1});
    seen.insert(SatelliteId{2});
    seen.insert(SatelliteId{1});
    CHECK_EQ(seen.size(), std::size_t{2});
}

// ---------------------------------------------------------------------------
// Enum wire names
// ---------------------------------------------------------------------------

ORCA_TEST(Enums, WireNamesMatchApiContract) {
    // These exact strings appear in API_CONTRACT.md §4 fixtures. A silent rename here
    // would break the frontend without breaking the build.
    CHECK_EQ(toString(SatelliteStatus::Failed), std::string_view{"FAILED"});
    CHECK_EQ(toString(SatelliteStatus::Nominal), std::string_view{"NOMINAL"});
    CHECK_EQ(toString(VehicleStatus::Transferring), std::string_view{"TRANSFERRING"});
    CHECK_EQ(toString(VehicleStatus::OnStation), std::string_view{"ON_STATION"});
    CHECK_EQ(toString(EmergencyKind::PropulsionFailure),
             std::string_view{"PROPULSION_FAILURE"});
    CHECK_EQ(toString(Severity::Critical), std::string_view{"CRITICAL"});
    CHECK_EQ(toString(EmergencyState::InProgress), std::string_view{"IN_PROGRESS"});
    CHECK_EQ(toString(MissionState::InTransit), std::string_view{"IN_TRANSIT"});
    CHECK_EQ(toString(MissionState::Success), std::string_view{"SUCCESS"});
    CHECK_EQ(toString(OrbitBand::Leo), std::string_view{"LEO"});
    CHECK_EQ(toString(ConstraintKind::DeltaVBudget), std::string_view{"DELTA_V_BUDGET"});
}

ORCA_TEST(Enums, SeverityScoresMatchOptimizationModel) {
    // Values fixed by OPTIMIZATION_MODEL.md §7.
    CHECK_NEAR(severityScore(Severity::Low), 0.25, 1e-15);
    CHECK_NEAR(severityScore(Severity::Moderate), 0.50, 1e-15);
    CHECK_NEAR(severityScore(Severity::High), 0.75, 1e-15);
    CHECK_NEAR(severityScore(Severity::Critical), 1.00, 1e-15);
}

ORCA_TEST(Enums, SeverityScoresAreMonotonic) {
    CHECK(severityScore(Severity::Low) < severityScore(Severity::Moderate));
    CHECK(severityScore(Severity::Moderate) < severityScore(Severity::High));
    CHECK(severityScore(Severity::High) < severityScore(Severity::Critical));
}

// ---------------------------------------------------------------------------
// ObjectiveVector
// ---------------------------------------------------------------------------

ORCA_TEST(ObjectiveVector, IndexedAccessMatchesNamedFields) {
    // Dominance loops over dimensions generically, so the index mapping must be right.
    const ObjectiveVector v{1.0, 2.0, 0.3, 4.0};
    CHECK_NEAR(v[0], v.time, 1e-15);
    CHECK_NEAR(v[1], v.fuel, 1e-15);
    CHECK_NEAR(v[2], v.risk, 1e-15);
    CHECK_NEAR(v[3], v.distance, 1e-15);
    CHECK_EQ(kObjectiveCount, std::size_t{4});
}

ORCA_TEST(ObjectiveVector, MutableIndexedAccess) {
    ObjectiveVector v{};
    for (std::size_t i = 0; i < kObjectiveCount; ++i) {
        v[i] = static_cast<double>(i) + 1.0;
    }
    CHECK_NEAR(v.time, 1.0, 1e-15);
    CHECK_NEAR(v.fuel, 2.0, 1e-15);
    CHECK_NEAR(v.risk, 3.0, 1e-15);
    CHECK_NEAR(v.distance, 4.0, 1e-15);
}

ORCA_TEST(ObjectiveVector, ValidityInvariant) {
    CHECK(isValid(ObjectiveVector{0.0, 0.0, 0.0, 0.0}));
    CHECK(isValid(ObjectiveVector{100.0, 0.5, 1.0, 2000.0}));
    CHECK(!isValid(ObjectiveVector{-1.0, 0.0, 0.0, 0.0}));      // negative time
    CHECK(!isValid(ObjectiveVector{0.0, 0.0, 1.5, 0.0}));        // risk above 1
    CHECK(!isValid(ObjectiveVector{0.0, 0.0, -0.1, 0.0}));       // risk below 0
    const double inf = std::numeric_limits<double>::infinity();
    CHECK(!isValid(ObjectiveVector{inf, 0.0, 0.0, 0.0}));
    CHECK(!isValid(ObjectiveVector{0.0, std::nan(""), 0.0, 0.0}));
}

ORCA_TEST(ObjectiveVector, AdditiveObjectivesAdd) {
    const ObjectiveVector a{100.0, 0.1, 0.0, 500.0};
    const ObjectiveVector b{200.0, 0.2, 0.0, 700.0};
    const ObjectiveVector c = compose(a, b);
    CHECK_NEAR(c.time, 300.0, 1e-12);
    CHECK_NEAR(c.fuel, 0.3, 1e-12);
    CHECK_NEAR(c.distance, 1200.0, 1e-12);
}

ORCA_TEST(ObjectiveVector, RiskComposesAsIndependentSurvival) {
    // The documented example: two 10% legs give 19%, not 20%.
    const ObjectiveVector a{0.0, 0.0, 0.1, 0.0};
    const ObjectiveVector b{0.0, 0.0, 0.1, 0.0};
    CHECK_NEAR(compose(a, b).risk, 0.19, 1e-15);
}

ORCA_TEST(ObjectiveVector, RiskCompositionIsBounded) {
    // Property: however many legs are composed, risk cannot exceed 1. This is what stops
    // a long route from producing a nonsensical risk of 5.0.
    ObjectiveVector acc{};
    const ObjectiveVector leg{0.0, 0.0, 0.5, 0.0};
    for (int i = 0; i < 200; ++i) {
        acc = compose(acc, leg);
        CHECK(acc.risk >= 0.0);
        CHECK(acc.risk <= 1.0);
    }
}

ORCA_TEST(ObjectiveVector, RiskCompositionIsMonotonic) {
    // Property: adding a leg never *reduces* path risk.
    //
    // This is not cosmetic. ALGORITHMS.md §5.6's proof that dominance pruning is sound
    // depends on every objective being monotonically non-decreasing along a path. If this
    // test fails, the Pareto search may silently discard optimal solutions.
    ObjectiveVector acc{};
    const double legRisks[] = {0.0, 0.01, 0.3, 0.001, 0.5, 0.0, 0.99, 0.2};
    for (const double r : legRisks) {
        const double before = acc.risk;
        acc = compose(acc, ObjectiveVector{1.0, 0.01, r, 10.0});
        CHECK(acc.risk >= before - 1e-15);
        CHECK(acc.time >= 0.0);
        CHECK(acc.fuel >= 0.0);
        CHECK(acc.distance >= 0.0);
    }
}

ORCA_TEST(ObjectiveVector, RiskCompositionIsOrderIndependent) {
    // Property: composing legs in a different order gives the same total. Multiplication
    // commutes, so leg ordering must not affect path risk.
    const ObjectiveVector legs[] = {{10.0, 0.01, 0.10, 100.0},
                                    {20.0, 0.02, 0.25, 200.0},
                                    {30.0, 0.03, 0.05, 300.0},
                                    {40.0, 0.04, 0.40, 400.0}};

    ObjectiveVector forward{};
    for (const auto& leg : legs) {
        forward = compose(forward, leg);
    }

    ObjectiveVector reverse{};
    for (int i = 3; i >= 0; --i) {
        reverse = compose(reverse, legs[i]);
    }

    CHECK_NEAR(forward.time, reverse.time, 1e-12);
    CHECK_NEAR(forward.fuel, reverse.fuel, 1e-12);
    CHECK_NEAR(forward.risk, reverse.risk, 1e-12);
    CHECK_NEAR(forward.distance, reverse.distance, 1e-12);
}

ORCA_TEST(ObjectiveVector, ComposingWithZeroIsIdentity) {
    const ObjectiveVector v{123.0, 0.45, 0.6, 789.0};
    const ObjectiveVector zero{};
    CHECK(compose(v, zero) == v);
    CHECK(compose(zero, v) == v);
}

ORCA_TEST(ObjectiveVector, CertainRiskStaysCertain) {
    // A leg with risk 1.0 saturates the path: nothing can bring it back down.
    const ObjectiveVector certain{0.0, 0.0, 1.0, 0.0};
    const ObjectiveVector safe{0.0, 0.0, 0.0, 0.0};
    CHECK_NEAR(compose(certain, safe).risk, 1.0, 1e-15);
    CHECK_NEAR(compose(safe, certain).risk, 1.0, 1e-15);
}

ORCA_TEST(ObjectiveVector, ComposedResultRemainsValid) {
    // Property: composing valid vectors yields a valid vector.
    const ObjectiveVector a{100.0, 0.3, 0.7, 500.0};
    const ObjectiveVector b{50.0, 0.2, 0.8, 250.0};
    CHECK(isValid(compose(a, b)));
}
