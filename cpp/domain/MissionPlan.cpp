#include "MissionPlan.hpp"

#include <format>
#include <utility>

#include "../core/Validation.hpp"

namespace orca {

MissionPlan::MissionPlan(PlanId id, VehicleId vehicle, EmergencyId emergency) noexcept
    : id_(id), vehicle_(vehicle), emergency_(emergency) {}

MissionPlan MissionPlan::makeFeasible(PlanId id,
                                      VehicleId vehicle,
                                      EmergencyId emergency,
                                      std::vector<EdgeId> route,
                                      ObjectiveVector totalCost,
                                      Seconds departureTimeSec,
                                      Seconds arrivalTimeSec) {
    requireThat(!route.empty(),
                std::format("MissionPlan {}: a feasible plan must have at least one leg",
                            toString(id)));
    requireThat(isValid(totalCost),
                std::format("MissionPlan {}: totalCost must be finite, non-negative, and "
                            "have risk in [0,1] (time={:.3f}, fuel={:.4f}, risk={:.4f}, "
                            "distance={:.3f})",
                            toString(id), totalCost.time, totalCost.fuel, totalCost.risk,
                            totalCost.distance));
    requireThat(arrivalTimeSec >= departureTimeSec,
                std::format("MissionPlan {}: arrival ({:.3f}) precedes departure ({:.3f})",
                            toString(id), arrivalTimeSec, departureTimeSec));

    MissionPlan plan{id, vehicle, emergency};
    plan.route_ = std::move(route);
    plan.totalCost_ = totalCost;
    plan.departureTimeSec_ = departureTimeSec;
    plan.arrivalTimeSec_ = arrivalTimeSec;
    plan.feasible_ = true;
    return plan;
}

MissionPlan MissionPlan::makeInfeasible(PlanId id,
                                        VehicleId vehicle,
                                        EmergencyId emergency,
                                        ConstraintViolation violation) {
    requireThat(!violation.detail.empty(),
                std::format("MissionPlan {}: an infeasible plan must explain which "
                            "constraint bound",
                            toString(id)));

    MissionPlan plan{id, vehicle, emergency};
    plan.feasible_ = false;
    plan.bindingConstraint_ = std::move(violation);
    return plan;
}

}  // namespace orca
