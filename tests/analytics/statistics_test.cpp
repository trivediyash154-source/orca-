/// `computeStatistics()` tests — Phase 11 brief §29, against hand-computed reference
/// values, independent of any benchmark/scenario machinery.

#include "../../cpp/analytics/Statistics.hpp"
#include "../TestFramework.hpp"

using namespace orca::analytics;

ORCA_TEST(Statistics, SingleValueHasZeroStddevAndEqualsItselfEverywhere) {
    const Statistics s = computeStatistics({42.0});
    CHECK_NEAR(s.mean, 42.0, 1e-12);
    CHECK_NEAR(s.median, 42.0, 1e-12);
    CHECK_NEAR(s.minimum, 42.0, 1e-12);
    CHECK_NEAR(s.maximum, 42.0, 1e-12);
    CHECK_NEAR(s.stddev, 0.0, 1e-12);
    CHECK_EQ(s.count, std::size_t{1});
}

ORCA_TEST(Statistics, MatchesHandComputedReferenceForFiveValues) {
    // {2, 4, 4, 4, 5, 5, 7, 9} -- Wikipedia's own worked sample standard deviation example.
    const Statistics s = computeStatistics({2.0, 4.0, 4.0, 4.0, 5.0, 5.0, 7.0, 9.0});
    CHECK_NEAR(s.mean, 5.0, 1e-12);
    CHECK_NEAR(s.stddev, 2.13809, 1e-4);
    CHECK_NEAR(s.minimum, 2.0, 1e-12);
    CHECK_NEAR(s.maximum, 9.0, 1e-12);
    CHECK_EQ(s.count, std::size_t{8});
}

ORCA_TEST(Statistics, MedianOfOddCountIsTheMiddleValue) {
    const Statistics s = computeStatistics({3.0, 1.0, 2.0});
    CHECK_NEAR(s.median, 2.0, 1e-12);
}

ORCA_TEST(Statistics, MedianOfEvenCountIsTheAverageOfTheTwoMiddleValues) {
    const Statistics s = computeStatistics({1.0, 2.0, 3.0, 4.0});
    CHECK_NEAR(s.median, 2.5, 1e-12);
}

ORCA_TEST(Statistics, InputOrderDoesNotAffectTheResult) {
    const Statistics sorted = computeStatistics({1.0, 2.0, 3.0, 4.0, 5.0});
    const Statistics shuffled = computeStatistics({4.0, 1.0, 5.0, 3.0, 2.0});
    CHECK_NEAR(sorted.mean, shuffled.mean, 1e-12);
    CHECK_NEAR(sorted.median, shuffled.median, 1e-12);
    CHECK_NEAR(sorted.stddev, shuffled.stddev, 1e-12);
}

ORCA_TEST(Statistics, EmptyInputThrows) { CHECK_THROWS(computeStatistics({})); }
