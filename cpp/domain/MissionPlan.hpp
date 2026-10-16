#pragma once

/// @file MissionPlan.hpp
/// A candidate rescue plan — the output of planning, before commitment.
///
/// ## This class computes nothing
///
/// A `MissionPlan` is a *result*. It does not search, score, normalise, or compare.
/// `ParetoSearch` produces plans, `MissionScorer` ranks them, and `ParetoFront` holds
/// them; all of those arrive in later phases. Putting any of that here would make the
/// entity depend on the algorithms that construct it.
///
/// ## Feasibility is structural, not a flag you can forget
///
/// A plan is built through one of two named constructors. An infeasible plan *cannot* be
/// constructed without naming the binding constraint, because that is the only way to
/// reach the infeasible state. This makes the DATA_MODEL.md §5 invariant
/// (`feasible == false` implies `bindingConstraint.has_value()`) impossible to violate
/// rather than merely asserted.
///
/// The binding constraint matters: OPTIMIZATION_MODEL.md §9 treats constraints as hard
/// rejections rather than score penalties, which means "no feasible plan" is a normal and
/// frequent outcome. It is only actionable if the system can say *which* limit bound and
/// by how much.

#include <optional>
#include <string>
#include <vector>

#include "../core/ObjectiveVector.hpp"
#include "../core/Types.hpp"
#include "../core/Units.hpp"

namespace orca {

/// Why a plan is infeasible. Surfaces directly in the UI (API_CONTRACT.md §4.4).
struct ConstraintViolation {
    ConstraintKind kind;
    std::string detail;      ///< human-readable, names the numbers
    double shortfall = 0.0;  ///< how far past the limit, in `unit`
    std::string unit;        ///< e.g. "km/s", "s"
};

class MissionPlan {
public:
    /// Build a feasible plan.
    ///
    /// @throws std::invalid_argument if the route is empty, the cost vector violates its
    ///         invariants, or arrival precedes departure.
    static MissionPlan makeFeasible(PlanId id,
                                    VehicleId vehicle,
                                    EmergencyId emergency,
                                    std::vector<EdgeId> route,
                                    ObjectiveVector totalCost,
                                    Seconds departureTimeSec,
                                    Seconds arrivalTimeSec);

    /// Build an infeasible plan, naming the constraint that bound.
    ///
    /// @throws std::invalid_argument if the violation detail is empty.
    static MissionPlan makeInfeasible(PlanId id,
                                      VehicleId vehicle,
                                      EmergencyId emergency,
                                      ConstraintViolation violation);

    [[nodiscard]] PlanId id() const noexcept { return id_; }
    [[nodiscard]] VehicleId vehicle() const noexcept { return vehicle_; }
    [[nodiscard]] EmergencyId emergency() const noexcept { return emergency_; }
    [[nodiscard]] const std::vector<EdgeId>& route() const noexcept { return route_; }
    [[nodiscard]] const ObjectiveVector& totalCost() const noexcept { return totalCost_; }
    [[nodiscard]] Seconds departureTimeSec() const noexcept { return departureTimeSec_; }
    [[nodiscard]] Seconds arrivalTimeSec() const noexcept { return arrivalTimeSec_; }
    [[nodiscard]] bool isFeasible() const noexcept { return feasible_; }

    [[nodiscard]] const std::optional<ConstraintViolation>& bindingConstraint() const noexcept {
        return bindingConstraint_;
    }

    /// Number of transfer legs. Zero for an infeasible plan.
    [[nodiscard]] std::size_t legCount() const noexcept { return route_.size(); }

private:
    MissionPlan(PlanId id, VehicleId vehicle, EmergencyId emergency) noexcept;

    PlanId id_;
    VehicleId vehicle_;
    EmergencyId emergency_;
    std::vector<EdgeId> route_;
    ObjectiveVector totalCost_;
    Seconds departureTimeSec_ = 0.0;
    Seconds arrivalTimeSec_ = 0.0;
    bool feasible_ = false;
    std::optional<ConstraintViolation> bindingConstraint_;
};

}  // namespace orca
