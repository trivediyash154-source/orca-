/// Tests for feasibility policy and the baseline transfer risk model.
///
/// Both are pure functions with no orbital mechanics in them, so these tests build
/// geometries by hand rather than computing transfers -- which is the point of keeping
/// policy separate from physics (TRANSFER_MODEL.md §12).

#include <cmath>

#include "../../cpp/transfer/TransferFeasibility.hpp"
#include "../../cpp/transfer/TransferRiskModel.hpp"
#include "../TestFramework.hpp"

using namespace orca;

namespace {

/// Constraints permissive enough that only the constraint under test can bite.
TransferConstraints permissive() {
    TransferConstraints c;
    c.maxDeltaVKms = 1000.0;
    c.deltaVBudgetKms = 1000.0;
    c.availableFuelFraction = 1.0;
    c.maxWaitSec = 1.0e12;
    c.maxPathLengthKm = 1.0e12;
    c.maxRisk = 1.0;
    c.latestArrivalSec = 1.0e18;
    return c;
}

TransferGeometry validGeometry() {
    TransferGeometry g;
    g.orbitsValid = true;
    g.rendezvousWindowExists = true;
    g.waitSec = 100.0;
    g.coastSec = 900.0;
    g.transferSec = 1000.0;
    g.departureTimeSec = 0.0;
    g.arrivalTimeSec = 1000.0;
    g.deltaVKms = 0.5;
    g.planeChangeDeltaVKms = 0.1;
    g.pathLengthKm = 20000.0;
    return g;
}

}  // namespace

// ---------------------------------------------------------------------------
// Feasibility policy (pure function, no orbital mechanics involved)
// ---------------------------------------------------------------------------

ORCA_TEST(Feasibility, AcceptsAValidTransferWithinLimits) {
    CHECK(checkFeasibility(validGeometry(), 0.2, 0.1, permissive()) ==
          TransferFailureReason::Feasible);
}

ORCA_TEST(Feasibility, RejectsInvalidOrbitsFirst) {
    TransferGeometry g = validGeometry();
    g.orbitsValid = false;
    CHECK(checkFeasibility(g, 0.2, 0.1, permissive()) ==
          TransferFailureReason::InvalidOrbit);
}

ORCA_TEST(Feasibility, ReportsPhasingBeforeAffordability) {
    // Ordering matters: no amount of fuel fixes a rendezvous that never occurs, so the
    // phasing problem is the more informative message.
    TransferGeometry g = validGeometry();
    g.rendezvousWindowExists = false;
    g.deltaVKms = 999.0;

    TransferConstraints c = permissive();
    c.maxDeltaVKms = 0.1;

    CHECK(checkFeasibility(g, 500.0, 0.1, c) == TransferFailureReason::PhasingUnavailable);
}

ORCA_TEST(Feasibility, RejectsWaitBeyondTheHorizon) {
    TransferGeometry g = validGeometry();
    g.waitSec = 90000.0;  // ~25 h, one synodic period for a 500/800 km pair

    TransferConstraints c = permissive();
    c.maxWaitSec = 21600.0;  // 6 h
    CHECK(checkFeasibility(g, 0.2, 0.1, c) == TransferFailureReason::PhasingUnavailable);
}

ORCA_TEST(Feasibility, RejectsExcessiveDeltaV) {
    TransferConstraints c = permissive();
    c.maxDeltaVKms = 0.4;
    CHECK(checkFeasibility(validGeometry(), 0.2, 0.1, c) ==
          TransferFailureReason::DeltaVExceeded);
}

ORCA_TEST(Feasibility, AcceptsExactlySufficientDeltaV) {
    // The boundary must be inclusive: a transfer needing precisely the remaining budget
    // is flyable, and two independently computed burn magnitudes will not land exactly on
    // it in floating point.
    TransferConstraints c = permissive();
    c.maxDeltaVKms = 0.5;  // geometry requires exactly 0.5
    CHECK(checkFeasibility(validGeometry(), 0.2, 0.1, c) ==
          TransferFailureReason::Feasible);
}

ORCA_TEST(Feasibility, RejectsSlightlyInsufficientDeltaV) {
    TransferConstraints c = permissive();
    c.maxDeltaVKms = 0.5 - 1e-3;
    CHECK(checkFeasibility(validGeometry(), 0.2, 0.1, c) ==
          TransferFailureReason::DeltaVExceeded);
}

ORCA_TEST(Feasibility, RejectsInsufficientFuelFraction) {
    TransferConstraints c = permissive();
    c.availableFuelFraction = 0.1;
    CHECK(checkFeasibility(validGeometry(), 0.2, 0.1, c) ==
          TransferFailureReason::FuelInsufficient);
}

ORCA_TEST(Feasibility, RejectsArrivalAfterTheDeadline) {
    TransferConstraints c = permissive();
    c.latestArrivalSec = 999.0;  // geometry arrives at 1000
    CHECK(checkFeasibility(validGeometry(), 0.2, 0.1, c) ==
          TransferFailureReason::DeadlineExceeded);
}

ORCA_TEST(Feasibility, RejectsExcessivePathLength) {
    TransferConstraints c = permissive();
    c.maxPathLengthKm = 100.0;
    CHECK(checkFeasibility(validGeometry(), 0.2, 0.1, c) ==
          TransferFailureReason::DistanceExceeded);
}

ORCA_TEST(Feasibility, RejectsExcessiveRisk) {
    TransferConstraints c = permissive();
    c.maxRisk = 0.05;
    CHECK(checkFeasibility(validGeometry(), 0.2, 0.1, c) ==
          TransferFailureReason::RiskExceeded);
}

// ---------------------------------------------------------------------------
// Risk model
// ---------------------------------------------------------------------------

ORCA_TEST(TransferRisk, StaysInUnitInterval) {
    for (double dv = 0.0; dv <= 20.0; dv += 0.37) {
        for (double share = 0.0; share <= 1.0; share += 0.1) {
            TransferRiskInputs in;
            in.totalDeltaVKms = dv;
            in.planeChangeDeltaVKms = dv * share;
            in.deltaVBudgetKms = 2.5;
            in.deltaVRemainingKms = 2.5;
            const double risk = estimateTransferRisk(in);
            CHECK(risk >= 0.0);
            CHECK(risk <= 1.0);
        }
    }
}

ORCA_TEST(TransferRisk, IsMonotonicInManoeuvreSize) {
    double previous = -1.0;
    for (double dv = 0.0; dv <= 2.5; dv += 0.05) {
        TransferRiskInputs in;
        in.totalDeltaVKms = dv;
        in.planeChangeDeltaVKms = 0.0;
        in.deltaVBudgetKms = 2.5;
        in.deltaVRemainingKms = 2.5;
        const double risk = estimateTransferRisk(in);
        CHECK(risk >= previous - 1e-12);
        previous = risk;
    }
}

ORCA_TEST(TransferRisk, PenalisesPlaneChangeShare) {
    // Two transfers of identical delta-v: the one that spends it rotating the plane is
    // rated riskier. This is the term that makes inclined routes look different from
    // equally expensive coplanar ones.
    TransferRiskInputs coplanar;
    coplanar.totalDeltaVKms = 1.0;
    coplanar.planeChangeDeltaVKms = 0.0;
    coplanar.deltaVBudgetKms = 2.5;
    coplanar.deltaVRemainingKms = 2.5;

    TransferRiskInputs inclined = coplanar;
    inclined.planeChangeDeltaVKms = 0.8;

    CHECK(estimateTransferRisk(inclined) > estimateTransferRisk(coplanar));
}

ORCA_TEST(TransferRisk, PenalisesThinArrivalMargin) {
    TransferRiskInputs full;
    full.totalDeltaVKms = 0.5;
    full.planeChangeDeltaVKms = 0.0;
    full.deltaVBudgetKms = 2.5;
    full.deltaVRemainingKms = 2.5;

    TransferRiskInputs nearlyEmpty = full;
    nearlyEmpty.deltaVRemainingKms = 0.55;

    CHECK(estimateTransferRisk(nearlyEmpty) > estimateTransferRisk(full));
}

ORCA_TEST(TransferRisk, ZeroDeltaVTransferHasNoPlaneChangeRisk) {
    // Guards the division by total delta-v: a zero-cost transfer must not produce NaN.
    TransferRiskInputs in;
    in.totalDeltaVKms = 0.0;
    in.planeChangeDeltaVKms = 0.0;
    in.deltaVBudgetKms = 2.5;
    in.deltaVRemainingKms = 2.5;
    const double risk = estimateTransferRisk(in);
    CHECK(std::isfinite(risk));
    CHECK_NEAR(risk, 0.0, 1e-12);
}

ORCA_TEST(TransferRisk, IsDeterministic) {
    TransferRiskInputs in;
    in.totalDeltaVKms = 1.234;
    in.planeChangeDeltaVKms = 0.567;
    in.deltaVBudgetKms = 2.5;
    in.deltaVRemainingKms = 2.0;
    const double first = estimateTransferRisk(in);
    for (int i = 0; i < 100; ++i) {
        CHECK(estimateTransferRisk(in) == first);
    }
}

