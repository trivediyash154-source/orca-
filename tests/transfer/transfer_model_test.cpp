/// Tests for TransferModel composition, objective projection, fuel cost and edge cases.
/// Feasibility policy and the risk model in isolation: feasibility_test.cpp

#include <cmath>
#include <limits>

#include "../../cpp/core/Constants.hpp"
#include "../../cpp/core/Units.hpp"
#include "../../cpp/domain/RescueVehicle.hpp"
#include "../../cpp/transfer/TransferModel.hpp"
#include "../TestFramework.hpp"

using namespace orca;

namespace {

Orbit orbitAt(Kilometres altitudeKm, Radians phaseRad = 0.0, Radians inclRad = 0.0) {
    return Orbit::fromAltitude(altitudeKm, inclRad, 0.0, phaseRad);
}

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

}  // namespace

// ---------------------------------------------------------------------------
// Time composition
// ---------------------------------------------------------------------------

ORCA_TEST(TransferModel, TransferTimeIsWaitPlusCoast) {
    // The three time fields are kept distinct precisely so this identity is checkable.
    const TransferModel model;
    for (const Kilometres alt : {600.0, 1500.0, 12000.0, 35786.0}) {
        const TransferGeometry g =
            model.computeGeometry(orbitAt(500.0, 0.0), orbitAt(alt, 1.7), 0.0);
        CHECK_NEAR(g.transferSec, g.waitSec + g.coastSec, 1e-9);
    }
}

ORCA_TEST(TransferModel, ArrivalIsDeparturePlusTransfer) {
    // Required by the graph layer: w(edge, t) must compose to an arrival time.
    const TransferModel model;
    for (const Seconds t : {0.0, 1.0, 412.0, 98765.0}) {
        const TransferGeometry g =
            model.computeGeometry(orbitAt(500.0), orbitAt(800.0, 1.0), t);
        CHECK_NEAR(g.arrivalTimeSec, g.departureTimeSec + g.transferSec, 1e-9);
        CHECK_NEAR(g.departureTimeSec, t, 1e-12);
    }
}

ORCA_TEST(TransferModel, CoastMatchesTheHohmannTimeForThePair) {
    // Independently computed: 2931.847068 s for 500 -> 800 km.
    const TransferModel model;
    const TransferGeometry g =
        model.computeGeometry(orbitAt(500.0), orbitAt(800.0, 1.0), 0.0);
    CHECK_NEAR(g.coastSec, 2931.847068, 1e-4);
}

// ---------------------------------------------------------------------------
// Objective projection -- the interface Phase 3 consumes
// ---------------------------------------------------------------------------

ORCA_TEST(TransferModel, ObjectiveVectorMapsTheRightFields) {
    const TransferModel model;
    const TransferResult r =
        model.evaluate(orbitAt(500.0), orbitAt(800.0, 1.0), 0.0, permissive());
    const ObjectiveVector v = r.objectives();
    CHECK_NEAR(v.time, r.geometry.transferSec, 1e-12);
    CHECK_NEAR(v.fuel, r.fuelCost, 1e-12);
    CHECK_NEAR(v.risk, r.risk, 1e-12);
    CHECK_NEAR(v.distance, r.geometry.pathLengthKm, 1e-12);
}

ORCA_TEST(TransferModel, ObjectiveVectorSatisfiesItsInvariants) {
    // Phase 3 will assert these; catching a violation here is cheaper.
    const TransferModel model;
    const Kilometres altitudes[] = {300.0, 500.0, 800.0, 5000.0, 20000.0, 35786.0};
    for (const Kilometres a : altitudes) {
        for (const Kilometres b : altitudes) {
            const TransferResult r =
                model.evaluate(orbitAt(a, 0.3, 0.1), orbitAt(b, 2.1, 0.9), 0.0,
                               permissive());
            CHECK(isValid(r.objectives()));
        }
    }
}

// ---------------------------------------------------------------------------
// Fuel cost -- a planning objective, not the physical delta-v
// ---------------------------------------------------------------------------

ORCA_TEST(TransferModel, FuelCostIsDeltaVOverBudget) {
    const TransferModel model;
    TransferConstraints c = permissive();
    c.deltaVBudgetKms = 2.5;

    const TransferResult r = model.evaluate(orbitAt(500.0), orbitAt(800.0, 1.0), 0.0, c);
    CHECK_NEAR(r.fuelCost, r.geometry.deltaVKms / 2.5, 1e-12);
    // The physical quantity is retained separately and is not the same number.
    CHECK(std::fabs(r.geometry.deltaVKms - r.fuelCost) > 1e-6);
}

ORCA_TEST(TransferModel, FuelCostScalesWithTheVehiclesOwnBudget) {
    // The same manoeuvre costs a smaller fraction of a larger tank -- which is exactly
    // why fuel fractions are not comparable across vehicles.
    const TransferModel model;
    TransferConstraints small = permissive();
    small.deltaVBudgetKms = 1.0;
    TransferConstraints large = permissive();
    large.deltaVBudgetKms = 4.0;

    const double fSmall =
        model.evaluate(orbitAt(500.0), orbitAt(800.0, 1.0), 0.0, small).fuelCost;
    const double fLarge =
        model.evaluate(orbitAt(500.0), orbitAt(800.0, 1.0), 0.0, large).fuelCost;
    CHECK_NEAR(fSmall / fLarge, 4.0, 1e-9);
}

// ---------------------------------------------------------------------------
// Constraints from a vehicle
// ---------------------------------------------------------------------------

ORCA_TEST(TransferModel, ConstraintsFromVehicleReuseExistingFields) {
    // No duplicated delta-v fields: the constraints read the vehicle rather than
    // restating its capability.
    RescueVehicle rv{VehicleId{2}, "Aegis-2", orbitAt(500.0), 2.5};
    rv.consumeDeltaV(0.34);

    const TransferConstraints c = TransferConstraints::forVehicle(rv);
    CHECK_NEAR(c.deltaVBudgetKms, 2.5, 1e-12);
    CHECK_NEAR(c.maxDeltaVKms, 2.16, 1e-12);
    CHECK_NEAR(c.availableFuelFraction, 0.864, 1e-12);
}

ORCA_TEST(Feasibility, InfeasibleResultsStillCarryTheirNumbers) {
    // The UI must be able to say "requires X, you have Y". Discarding the numbers on
    // failure would make the binding-constraint message impossible.
    const TransferModel model;
    TransferConstraints c = permissive();
    c.maxDeltaVKms = 0.001;
    c.deltaVBudgetKms = 0.001;

    const TransferResult r =
        model.evaluate(orbitAt(500.0), orbitAt(35786.0, 2.0), 0.0, c);
    CHECK(!r.feasible);
    CHECK(r.failureReason == TransferFailureReason::DeltaVExceeded);
    CHECK(r.geometry.deltaVKms > 0.0);      // the real requirement is reported
    CHECK(r.geometry.transferSec > 0.0);
    CHECK(r.geometry.pathLengthKm > 0.0);
    CHECK(std::isfinite(r.geometry.deltaVKms));
}

// ---------------------------------------------------------------------------
// Edge cases
// ---------------------------------------------------------------------------

ORCA_TEST(TransferEdge, SameOrbitSamePhaseIsFreeAndInstant) {
    const TransferModel model;
    const Orbit o = orbitAt(500.0, 1.5, 0.4);
    const TransferResult r = model.evaluate(o, o, 0.0, permissive());
    CHECK(r.feasible);
    CHECK(r.geometry.isPurePlaneChange);
    CHECK_NEAR(r.geometry.deltaVKms, 0.0, 1e-12);
    CHECK_NEAR(r.geometry.transferSec, 0.0, 1e-12);
    CHECK_NEAR(r.geometry.pathLengthKm, 0.0, 1e-12);
    CHECK_NEAR(r.fuelCost, 0.0, 1e-12);
}

ORCA_TEST(TransferEdge, SameAltitudeDifferentInclinationIsAPurePlaneChange) {
    const TransferModel model;
    const Orbit source = orbitAt(500.0, 1.0, 0.0);
    const Orbit target = orbitAt(500.0, 1.0, degToRad(10.0));  // same phase, 10 deg apart
    const TransferGeometry g = model.computeGeometry(source, target, 0.0);

    CHECK(g.isPurePlaneChange);
    CHECK_NEAR(g.coastSec, 0.0, 1e-12);
    CHECK_NEAR(g.pathLengthKm, 0.0, 1e-12);
    // Cost is exactly 2*v*sin(di/2) -- independently 1.326965039 km/s at 500 km.
    CHECK_NEAR(g.deltaVKms, 1.326965039, 1e-7);
    CHECK_NEAR(g.planeChangeDeltaVKms, g.deltaVKms, 1e-12);
}

ORCA_TEST(TransferEdge, SameAltitudeDifferentPhaseHasNoRendezvous) {
    // No relative drift and not co-located: unreachable. Reported explicitly rather than
    // as an enormous wait.
    const TransferModel model;
    const TransferResult r =
        model.evaluate(orbitAt(500.0, 0.0), orbitAt(500.0, 2.0), 0.0, permissive());
    CHECK(!r.feasible);
    CHECK(r.failureReason == TransferFailureReason::PhasingUnavailable);
    CHECK(!r.geometry.rendezvousWindowExists);
}

ORCA_TEST(TransferEdge, VeryCloseAltitudesProduceAnUnreachableWait) {
    // A 100 m altitude difference implies a synodic period of years, so under any sane
    // horizon the transfer is unavailable.
    const TransferModel model;
    TransferConstraints c = permissive();
    c.maxWaitSec = 21600.0;
    const TransferResult r =
        model.evaluate(orbitAt(500.0, 0.0), orbitAt(500.1, 3.0), 0.0, c);
    CHECK(!r.feasible);
    CHECK(r.failureReason == TransferFailureReason::PhasingUnavailable);
}

ORCA_TEST(TransferEdge, LargeAltitudeDifferenceIsHandled) {
    const TransferModel model;
    const TransferResult r =
        model.evaluate(orbitAt(300.0), orbitAt(35786.0, 1.0), 0.0, permissive());
    CHECK(std::isfinite(r.geometry.deltaVKms));
    CHECK(std::isfinite(r.geometry.transferSec));
    // The coplanar component matches the textbook LEO->GEO figure.
    CHECK(r.geometry.deltaVKms > 3.8);
    CHECK(r.geometry.deltaVKms < 4.0);
}

ORCA_TEST(TransferEdge, NearOppositeInclinationIsExpensiveButFinite) {
    const TransferModel model;
    const Orbit source = orbitAt(500.0, 0.0, degToRad(1.0));
    const Orbit target = orbitAt(800.0, 1.0, degToRad(179.0));
    const TransferGeometry g = model.computeGeometry(source, target, 0.0);
    CHECK(std::isfinite(g.deltaVKms));
    // Reversing the plane costs roughly twice orbital speed -- around 15 km/s here.
    CHECK(g.deltaVKms > 10.0);
    CHECK(g.deltaVKms < 30.0);
}

ORCA_TEST(TransferEdge, NonFiniteDepartureTimeIsRejected) {
    const TransferModel model;
    const double inf = std::numeric_limits<double>::infinity();
    const TransferResult r =
        model.evaluate(orbitAt(500.0), orbitAt(800.0, 1.0), inf, permissive());
    CHECK(!r.feasible);
    CHECK(r.failureReason == TransferFailureReason::InvalidOrbit);
}

ORCA_TEST(TransferEdge, NegativeDepartureTimeIsWellDefined) {
    // Time before the epoch is meaningful: the orbital model is analytic and reversible.
    const TransferModel model;
    const TransferGeometry g =
        model.computeGeometry(orbitAt(500.0), orbitAt(800.0, 1.0), -5000.0);
    CHECK(g.orbitsValid);
    CHECK(g.rendezvousWindowExists);
    CHECK(g.waitSec >= 0.0);
    CHECK_NEAR(g.arrivalTimeSec, -5000.0 + g.transferSec, 1e-9);
}

ORCA_TEST(TransferEdge, RequestOverloadMatchesDirectCall) {
    // Aggregate initialisation, because Orbit has no default constructor -- absence of a
    // default-constructible orbit is a Phase 1 invariant, and it propagates here.
    const TransferModel model;
    const TransferRequest request{orbitAt(500.0, 0.3, 0.2), orbitAt(1200.0, 2.9, 0.7),
                                  777.0, permissive()};

    const TransferResult viaRequest = model.evaluate(request);
    const TransferResult direct = model.evaluate(request.source, request.target,
                                                 request.departureTimeSec,
                                                 request.constraints);
    CHECK(viaRequest.feasible == direct.feasible);
    CHECK_NEAR(viaRequest.geometry.deltaVKms, direct.geometry.deltaVKms, 1e-15);
    CHECK_NEAR(viaRequest.geometry.arrivalTimeSec, direct.geometry.arrivalTimeSec, 1e-15);
}

ORCA_TEST(TransferEdge, FailureReasonWireNames) {
    CHECK_EQ(toString(TransferFailureReason::Feasible), std::string_view{"FEASIBLE"});
    CHECK_EQ(toString(TransferFailureReason::PhasingUnavailable),
             std::string_view{"PHASING_UNAVAILABLE"});
    CHECK_EQ(toString(TransferFailureReason::DeltaVExceeded),
             std::string_view{"DELTA_V_EXCEEDED"});
    CHECK_EQ(toString(TransferFailureReason::DeadlineExceeded),
             std::string_view{"DEADLINE_EXCEEDED"});
}
