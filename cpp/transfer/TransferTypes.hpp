#pragma once

/// @file TransferTypes.hpp
/// Vocabulary types for the transfer subsystem.
///
/// ## The separation this file encodes
///
/// `TransferGeometry` is what the **physics** says: how long, how much delta-v, how far,
/// how risky. It knows nothing about whether any particular vehicle can afford it.
///
/// `TransferConstraints` is **policy**: what this vehicle, under these rules, is permitted
/// to do.
///
/// `TransferResult` is geometry plus a verdict.
///
/// Keeping these apart means a later planner can reuse the physical approximation without
/// inheriting ORCA's feasibility rules, and means the feasibility check is a pure function
/// of (geometry, constraints) that can be tested without computing a transfer.
///
/// ## Infeasible is a result, not an error
///
/// An infeasible transfer still carries **every computed number**. That is deliberate: the
/// UI needs to say *"requires 2.91 km/s; RV-02 has 2.16 km/s"*, which is impossible if the
/// numbers are discarded on failure. No sentinel values are ever used to signal failure —
/// `feasible` and `failureReason` carry that, and the numeric fields always mean what they
/// say.

#include <string_view>

#include "../core/ObjectiveVector.hpp"
#include "../core/Units.hpp"
#include "../domain/Orbit.hpp"

namespace orca {

class RescueVehicle;  // for TransferConstraints::forVehicle

// ---------------------------------------------------------------------------
// Failure reasons
// ---------------------------------------------------------------------------

/// Why a transfer is not available. `Feasible` is the success value so that a single
/// enum describes the whole outcome space.
enum class TransferFailureReason {
    Feasible,
    InvalidOrbit,        ///< non-finite or physically impossible orbital data
    DeltaVExceeded,      ///< required delta-v exceeds the vehicle's remaining budget
    FuelInsufficient,    ///< required fuel fraction exceeds the available fraction
    PhasingUnavailable,  ///< no rendezvous window within the planning horizon
    DistanceExceeded,    ///< transfer path length exceeds the permitted maximum
    RiskExceeded,        ///< estimated risk exceeds the permitted ceiling
    DeadlineExceeded,    ///< arrival would fall after the caller's latest acceptable time
};

[[nodiscard]] constexpr std::string_view toString(TransferFailureReason r) noexcept {
    switch (r) {
        case TransferFailureReason::Feasible:           return "FEASIBLE";
        case TransferFailureReason::InvalidOrbit:       return "INVALID_ORBIT";
        case TransferFailureReason::DeltaVExceeded:     return "DELTA_V_EXCEEDED";
        case TransferFailureReason::FuelInsufficient:   return "FUEL_INSUFFICIENT";
        case TransferFailureReason::PhasingUnavailable: return "PHASING_UNAVAILABLE";
        case TransferFailureReason::DistanceExceeded:   return "DISTANCE_EXCEEDED";
        case TransferFailureReason::RiskExceeded:       return "RISK_EXCEEDED";
        case TransferFailureReason::DeadlineExceeded:   return "DEADLINE_EXCEEDED";
    }
    return "UNKNOWN";
}

// ---------------------------------------------------------------------------
// Constraints (policy)
// ---------------------------------------------------------------------------

/// What a transfer is permitted to cost.
///
/// Split into two groups, and the split matters: the first two come from the *vehicle's
/// physical capability* and the rest are *planning policy*. `forVehicle()` fills the
/// former from an existing `RescueVehicle` rather than duplicating those fields here.
struct TransferConstraints {
    // --- vehicle capability -------------------------------------------------
    KmPerSec maxDeltaVKms = 2.5;          ///< remaining delta-v budget
    KmPerSec deltaVBudgetKms = 2.5;       ///< total budget, the fuel-fraction denominator
    Fraction availableFuelFraction = 1.0; ///< remaining fuel as a fraction of the budget

    // --- planning policy ----------------------------------------------------

    /// Longest a vehicle may loiter waiting for a rendezvous window.
    ///
    /// This is the constraint that makes ORCA's central trade-off bite. The synodic
    /// period between a 500 km and an 800 km orbit is 25.4 hours, so an unbounded horizon
    /// would let the planner wait a day for a fuel-optimal departure while the casualty
    /// dies. 6 hours is a default, not a physical fact; it moves to configs/ in Phase 14.
    Seconds maxWaitSec = 21600.0;

    Kilometres maxPathLengthKm = 1.0e9;   ///< effectively unbounded by default
    double maxRisk = 1.0;                 ///< risk ceiling in [0,1]

    /// Latest acceptable arrival, absolute simulation time. Defaults to "no deadline".
    /// The graph builder supplies the casualty's survival-window close time here.
    Seconds latestArrivalSec = 1.0e18;

    /// Build capability constraints from a vehicle, leaving policy fields at their
    /// defaults. Avoids duplicating the vehicle's delta-v fields into a second place.
    [[nodiscard]] static TransferConstraints forVehicle(const RescueVehicle& vehicle);
};

// ---------------------------------------------------------------------------
// Request
// ---------------------------------------------------------------------------

/// A transfer question.
///
/// Orbits are held by value: `Orbit` is four doubles, so copying is cheaper than the
/// lifetime reasoning that references would demand across the graph builder's O(V^2)
/// evaluation sweep.
struct TransferRequest {
    Orbit source;
    Orbit target;
    Seconds departureTimeSec = 0.0;
    TransferConstraints constraints;
};

// ---------------------------------------------------------------------------
// Geometry (physics, no policy)
// ---------------------------------------------------------------------------

/// What the transfer physically requires. Contains no notion of permission.
///
/// ## Time decomposition
///
/// The three time fields are kept separate because conflating them is the most likely way
/// to introduce a subtle planning bug:
///
///     waitSec       loitering in the source orbit until the rendezvous window opens
///     coastSec      flying the transfer arc, departure burn to arrival burn
///     transferSec   = waitSec + coastSec, departure decision to arrival
///
/// `transferSec` is the only one the graph uses as an edge weight. Burn durations are not
/// modelled at all (burns are impulsive), so no field represents them.
struct TransferGeometry {
    bool orbitsValid = false;

    Seconds waitSec = 0.0;
    Seconds coastSec = 0.0;
    Seconds transferSec = 0.0;

    Seconds departureTimeSec = 0.0;
    Seconds arrivalTimeSec = 0.0;

    /// Physical delta-v requirement, km/s. Independent of departure time — see
    /// TransferModel.hpp for why that property matters to Phase 3.
    KmPerSec deltaVKms = 0.0;

    /// Delta-v attributable to the plane change, km/s. Retained separately because the
    /// risk model treats inclination change as a distinct hazard, and because it is the
    /// number that explains why an apparently short transfer is expensive.
    KmPerSec planeChangeDeltaVKms = 0.0;

    /// Length of the transfer arc, km. See TRANSFER_MODEL.md §9 for exactly what this
    /// measures — it is *not* "distance travelled including loiter".
    Kilometres pathLengthKm = 0.0;

    /// Whether a rendezvous window exists at all within the drift model.
    bool rendezvousWindowExists = false;

    /// True when the two orbits are close enough in radius to be treated as a pure
    /// plane change rather than a Hohmann transfer.
    bool isPurePlaneChange = false;
};

// ---------------------------------------------------------------------------
// Result (geometry + verdict)
// ---------------------------------------------------------------------------

/// A transfer question, answered.
struct TransferResult {
    TransferGeometry geometry;
    bool feasible = false;
    TransferFailureReason failureReason = TransferFailureReason::InvalidOrbit;

    /// Fraction of the vehicle's own delta-v budget this transfer consumes.
    ///
    /// A *planning objective*, distinct from the physical `geometry.deltaVKms`. The two
    /// are never used interchangeably: delta-v is what the manoeuvre costs the universe,
    /// fuel cost is what it costs this vehicle.
    Fraction fuelCost = 0.0;

    /// Estimated transfer risk in [0,1]. A simulation risk model, not a probability.
    double risk = 0.0;

    /// Project the result into the optimizer's four-objective space.
    ///
    /// This is the entire interface Phase 3's graph builder needs: it can weight an edge
    /// without knowing that Hohmann transfers, plane changes or synodic periods exist.
    [[nodiscard]] ObjectiveVector objectives() const noexcept {
        return ObjectiveVector{geometry.transferSec, fuelCost, risk, geometry.pathLengthKm};
    }
};

}  // namespace orca
