#include "GraphBuilder.hpp"

#include <chrono>

namespace orca {

namespace {

/// Reference constraints for build-time evaluation: capability and timing effectively
/// unbounded, so the only rejections `TransferModel::evaluate()` can produce are the
/// Group 1 reasons documented in GraphBuilder.hpp.
///
/// Finite rather than literal infinity, deliberately: every downstream formula (risk,
/// fuel-fraction) stays in ordinary floating-point range with these values, whereas
/// infinity risks an unintended NaN the first time someone adds a new term that divides
/// by a "budget". 1e6 km/s is already far beyond any transfer ORCA's model can produce
/// (LEO<->GEO plane reversal is on the order of 15 km/s); 1e12 s is ~31,700 years.
constexpr KmPerSec kUnboundedDeltaVKms = 1.0e6;
constexpr Seconds kUnboundedWaitSec = 1.0e12;

[[nodiscard]] bool isEligible(const Satellite& satellite) noexcept {
    return !satellite.isTerminal();
}

[[nodiscard]] bool isEligible(const RescueVehicle& vehicle) noexcept {
    return vehicle.status() != VehicleStatus::Disabled;
}

}  // namespace

GraphBuilder::BuildOutput GraphBuilder::build(const Scenario& scenario,
                                              const TransferModel& transferModel,
                                              const Options& options) {
    const auto startedAt = std::chrono::steady_clock::now();

    BuildOutput out;

    TransferConstraints referenceConstraints;
    referenceConstraints.maxDeltaVKms = kUnboundedDeltaVKms;
    referenceConstraints.deltaVBudgetKms = kUnboundedDeltaVKms;
    referenceConstraints.availableFuelFraction = 1.0;
    referenceConstraints.maxWaitSec = kUnboundedWaitSec;
    referenceConstraints.latestArrivalSec = kUnboundedWaitSec;
    referenceConstraints.maxRisk = options.maxRisk;
    referenceConstraints.maxPathLengthKm = options.maxPathLengthKm;

    // Every eligible node, tagged with the orbit TransferModel needs to evaluate it.
    // Built as one list so the O(V^2) candidate sweep below does not care whether either
    // side of a pair is a satellite or a vehicle.
    std::vector<NodeId> nodeIds;
    nodeIds.reserve(scenario.satellites().size() + scenario.vehicles().size());

    for (const Satellite& sat : scenario.satellites()) {
        if (isEligible(sat)) {
            nodeIds.push_back(out.graph.addNode(GraphNode{
                NodeId{0}, sat.id(), sat.orbit()}));  // id is reassigned by addNode
        }
    }
    for (const RescueVehicle& veh : scenario.vehicles()) {
        if (isEligible(veh)) {
            nodeIds.push_back(
                out.graph.addNode(GraphNode{NodeId{0}, veh.id(), veh.orbit()}));
        }
    }

    for (const NodeId from : nodeIds) {
        const Orbit& sourceOrbit = out.graph.node(from).orbit();

        for (const NodeId to : nodeIds) {
            if (from == to) {
                continue;  // no self-loops (GRAPH_MODEL.md §4)
            }
            ++out.stats.candidatePairs;

            const Orbit& targetOrbit = out.graph.node(to).orbit();
            const TransferResult result = transferModel.evaluate(
                sourceOrbit, targetOrbit, options.referenceTimeSec, referenceConstraints);

            if (result.feasible) {
                out.graph.addEdge(from, to, result, options.referenceTimeSec);
                ++out.stats.feasibleEdges;
            } else {
                out.stats.recordRejection(result.failureReason);
            }
        }
    }

    const auto finishedAt = std::chrono::steady_clock::now();
    out.stats.buildTimeMs =
        std::chrono::duration<double, std::milli>(finishedAt - startedAt).count();

    return out;
}

}  // namespace orca
