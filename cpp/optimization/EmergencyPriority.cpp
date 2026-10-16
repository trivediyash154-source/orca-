#include "EmergencyPriority.hpp"

#include "../core/Validation.hpp"

namespace orca {

PriorityWeights defaultPriorityWeights() noexcept {
    return PriorityWeights{0.4, 0.4, 0.2};
}

double feasibilityScore(bool feasiblePlanExists) noexcept {
    return feasiblePlanExists ? 1.0 : 0.2;
}

double computePriority(const Emergency& emergency, Seconds nowSec, bool feasiblePlanExists,
                       const PriorityWeights& weights) {
    requireThat(weights.wSeverity >= 0.0 && weights.wUrgency >= 0.0 &&
                    weights.wFeasibility >= 0.0,
                "computePriority: all weights must be >= 0");
    const double sum = weights.wSeverity + weights.wUrgency + weights.wFeasibility;
    requireThat(sum > 0.0, "computePriority: at least one weight must be > 0");

    const double severity = severityScore(emergency.severity());
    const double urgency = emergency.urgencyAt(nowSec);
    const double feasibility = feasibilityScore(feasiblePlanExists);

    return (weights.wSeverity * severity + weights.wUrgency * urgency +
           weights.wFeasibility * feasibility) /
          sum;
}

}  // namespace orca
