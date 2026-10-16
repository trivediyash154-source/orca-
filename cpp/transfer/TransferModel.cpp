#include "TransferModel.hpp"

#include <cmath>

#include "../core/Constants.hpp"
#include "../core/Math.hpp"
#include "../domain/RescueVehicle.hpp"
#include "HohmannTransfer.hpp"
#include "PhasingModel.hpp"
#include "PlaneChange.hpp"
#include "TransferFeasibility.hpp"

namespace orca {

TransferConstraints TransferConstraints::forVehicle(const RescueVehicle& vehicle) {
    TransferConstraints c;
    c.maxDeltaVKms = vehicle.deltaVRemainingKms();
    c.deltaVBudgetKms = vehicle.deltaVBudgetKms();
    c.availableFuelFraction = vehicle.fuelFraction();
    return c;
}

TransferModel::TransferModel(TransferRiskWeights riskWeights) noexcept
    : riskWeights_(riskWeights) {}

TransferGeometry TransferModel::computeGeometry(const Orbit& source,
                                                const Orbit& target,
                                                Seconds departureTimeSec) const noexcept {
    TransferGeometry g;
    g.departureTimeSec = departureTimeSec;

    const Kilometres r1 = source.semiMajorAxisKm();
    const Kilometres r2 = target.semiMajorAxisKm();

    // Orbit guarantees radii above Earth's surface and finite angles at construction, so
    // reaching here with invalid data would mean an invariant was bypassed. The check
    // stays because computeGeometry is public and a caller could construct orbits by other
    // means in future.
    if (!std::isfinite(r1) || !std::isfinite(r2) || r1 <= 0.0 || r2 <= 0.0 ||
        !std::isfinite(departureTimeSec)) {
        g.orbitsValid = false;
        return g;
    }
    g.orbitsValid = true;

    const Radians deltaInclination = plane_change::inclinationDifferenceRad(
        source.inclinationRad(), target.inclinationRad());

    g.isPurePlaneChange = std::fabs(r1 - r2) <= kSameRadiusToleranceKm;

    Radians transferArcRad = 0.0;

    if (g.isPurePlaneChange) {
        // Same orbit radius: there is no transfer ellipse. The manoeuvre is a single burn
        // at a node, rotating the plane without changing speed. It takes no coast time and
        // covers no arc, so the vehicle arrives where it departed.
        g.coastSec = 0.0;
        g.pathLengthKm = 0.0;
        g.deltaVKms = plane_change::pureDeltaVKms(source.speedKmPerSec(), deltaInclination);
        g.planeChangeDeltaVKms = g.deltaVKms;
        transferArcRad = 0.0;
    } else {
        const hohmann::TransferEllipse ellipse =
            hohmann::compute(r1, r2, deltaInclination);
        g.coastSec = ellipse.coastTimeSec;
        g.pathLengthKm = ellipse.pathLengthKm;
        g.deltaVKms = ellipse.totalDeltaVKms;
        g.planeChangeDeltaVKms = ellipse.planeChangeDeltaVKms;
        transferArcRad = kPi;  // a Hohmann transfer sweeps 180 degrees
    }

    const phasing::RendezvousWindow window =
        phasing::nextWindow(source, target, departureTimeSec, g.coastSec, transferArcRad);

    g.rendezvousWindowExists = window.exists;
    g.waitSec = window.exists ? window.waitSec : 0.0;

    // The composition that carries the FIFO guarantee:
    //     transfer = wait + coast
    //     arrival  = departure + transfer
    // `coast` is constant for a given orbit pair, and `departure + wait` is the absolute
    // window time, which is non-decreasing in departure. See PhasingModel.hpp.
    g.transferSec = g.waitSec + g.coastSec;
    g.arrivalTimeSec = departureTimeSec + g.transferSec;

    return g;
}

TransferResult TransferModel::evaluate(const Orbit& source,
                                       const Orbit& target,
                                       Seconds departureTimeSec,
                                       const TransferConstraints& constraints) const noexcept {
    TransferResult result;
    result.geometry = computeGeometry(source, target, departureTimeSec);

    if (!result.geometry.orbitsValid) {
        result.feasible = false;
        result.failureReason = TransferFailureReason::InvalidOrbit;
        return result;
    }

    // Physical delta-v becomes a planning objective by dividing through the vehicle's own
    // budget. The two are kept as separate fields precisely so nobody conflates them:
    // delta-v is what the manoeuvre costs, fuel cost is what it costs *this vehicle*.
    const KmPerSec budget = constraints.deltaVBudgetKms > kEpsilon
                                ? constraints.deltaVBudgetKms
                                : 1.0;
    result.fuelCost = result.geometry.deltaVKms / budget;

    TransferRiskInputs riskInputs;
    riskInputs.totalDeltaVKms = result.geometry.deltaVKms;
    riskInputs.planeChangeDeltaVKms = result.geometry.planeChangeDeltaVKms;
    riskInputs.deltaVBudgetKms = budget;
    riskInputs.deltaVRemainingKms = constraints.maxDeltaVKms;
    result.risk = estimateTransferRisk(riskInputs, riskWeights_);

    result.failureReason =
        checkFeasibility(result.geometry, result.fuelCost, result.risk, constraints);
    result.feasible = result.failureReason == TransferFailureReason::Feasible;

    return result;
}

TransferResult TransferModel::evaluate(const TransferRequest& request) const noexcept {
    return evaluate(request.source, request.target, request.departureTimeSec,
                    request.constraints);
}

}  // namespace orca
