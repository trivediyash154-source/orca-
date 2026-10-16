/// Tests for `computePriority()` (`EmergencyPriority.hpp`) — which casualty gets served,
/// distinct from `MissionScorer`'s route selection (the file comment's "two,
/// deliberately un-mergeable, weight systems").

#include "../../cpp/optimization/EmergencyPriority.hpp"
#include "../TestFramework.hpp"

using namespace orca;

namespace {
Emergency makeEmergency(Severity severity, Seconds detectedAtSec, Seconds windowSec) {
    return Emergency(EmergencyId(1), SatelliteId(1), EmergencyKind::PowerFailure, severity,
                     detectedAtSec, windowSec);
}
}  // namespace

ORCA_TEST(EmergencyPriority, DefaultWeightsMatchConfigFile) {
    // configs/priority.json's literal weights: severity 0.4, urgency 0.4, feasibility 0.2
    // -- checked here so the hardcoded C++ defaults and the config file cannot silently
    // drift apart (EmergencyPriority.hpp's file comment).
    const PriorityWeights w = defaultPriorityWeights();
    CHECK_NEAR(w.wSeverity, 0.4, 1e-12);
    CHECK_NEAR(w.wUrgency, 0.4, 1e-12);
    CHECK_NEAR(w.wFeasibility, 0.2, 1e-12);
}

ORCA_TEST(EmergencyPriority, FeasibilityScoreDampsNotExcludes) {
    // configs/priority.json's feasibilityScores: 1.0 if reachable, 0.2 -- not 0.0 -- if not
    // (OPTIMIZATION_MODEL.md §7.1's "damps rather than excludes").
    CHECK_NEAR(feasibilityScore(true), 1.0, 1e-12);
    CHECK_NEAR(feasibilityScore(false), 0.2, 1e-12);
    CHECK(feasibilityScore(false) > 0.0);
}

ORCA_TEST(EmergencyPriority, CriticalJustDetectedFeasibleGivesExpectedFormulaResult) {
    // severity(Critical)=1.0, urgency at detection=0.0, feasibility(true)=1.0.
    // priority = (0.4*1.0 + 0.4*0.0 + 0.2*1.0) / 1.0 = 0.6.
    const Emergency e = makeEmergency(Severity::Critical, 0.0, 1000.0);
    const double p = computePriority(e, 0.0, true);
    CHECK_NEAR(p, 0.6, 1e-9);
}

ORCA_TEST(EmergencyPriority, LowSeverityAtDeadlineInfeasibleGivesExpectedFormulaResult) {
    // severity(Low)=0.25, urgency at deadline=1.0, feasibility(false)=0.2.
    // priority = (0.4*0.25 + 0.4*1.0 + 0.2*0.2) / 1.0 = (0.1+0.4+0.04) = 0.54.
    const Emergency e = makeEmergency(Severity::Low, 0.0, 1000.0);
    const double p = computePriority(e, 1000.0, false);
    CHECK_NEAR(p, 0.54, 1e-9);
}

ORCA_TEST(EmergencyPriority, IncreasesWithSeverityAllElseEqual) {
    const Emergency low = makeEmergency(Severity::Low, 0.0, 1000.0);
    const Emergency crit = makeEmergency(Severity::Critical, 0.0, 1000.0);
    CHECK(computePriority(crit, 0.0, true) > computePriority(low, 0.0, true));
}

ORCA_TEST(EmergencyPriority, IncreasesWithUrgencyAsDeadlineApproaches) {
    const Emergency e = makeEmergency(Severity::Moderate, 0.0, 1000.0);
    const double early = computePriority(e, 0.0, true);
    const double late = computePriority(e, 900.0, true);
    CHECK(late > early);
}

ORCA_TEST(EmergencyPriority, FeasibleAlwaysOutranksInfeasibleAllElseEqual) {
    const Emergency e = makeEmergency(Severity::Moderate, 0.0, 1000.0);
    CHECK(computePriority(e, 500.0, true) > computePriority(e, 500.0, false));
}

ORCA_TEST(EmergencyPriority, AlwaysWithinUnitInterval) {
    const Severity severities[] = {Severity::Low, Severity::Moderate, Severity::High,
                                   Severity::Critical};
    for (Severity s : severities) {
        const Emergency e = makeEmergency(s, 0.0, 1000.0);
        for (Seconds t : {0.0, 250.0, 500.0, 750.0, 999.0}) {
            for (bool feasible : {true, false}) {
                const double p = computePriority(e, t, feasible);
                CHECK(p >= 0.0 && p <= 1.0);
            }
        }
    }
}

ORCA_TEST(EmergencyPriority, RejectsAllZeroWeights) {
    const Emergency e = makeEmergency(Severity::Moderate, 0.0, 1000.0);
    const PriorityWeights zero{0.0, 0.0, 0.0};
    CHECK_THROWS(computePriority(e, 0.0, true, zero));
}

ORCA_TEST(EmergencyPriority, RejectsNegativeWeight) {
    const Emergency e = makeEmergency(Severity::Moderate, 0.0, 1000.0);
    const PriorityWeights negative{-0.1, 0.5, 0.6};
    CHECK_THROWS(computePriority(e, 0.0, true, negative));
}

ORCA_TEST(EmergencyPriority, NormalisesEvenWhenWeightsDoNotSumToOne) {
    // Same relative weighting (2:2:1), scaled up -- normalised result must match the
    // defaults exactly, since computePriority divides by the actual weight sum.
    const Emergency e = makeEmergency(Severity::Critical, 0.0, 1000.0);
    const PriorityWeights scaled{4.0, 4.0, 2.0};
    const double defaultResult = computePriority(e, 0.0, true, defaultPriorityWeights());
    const double scaledResult = computePriority(e, 0.0, true, scaled);
    CHECK_NEAR(defaultResult, scaledResult, 1e-9);
}
