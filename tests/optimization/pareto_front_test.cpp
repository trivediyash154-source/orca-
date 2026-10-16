/// Tests for `buildParetoFront()`: the conversion from a raw, domain-agnostic
/// `ParetoSearchResult` into the `MissionPlan`-bearing `ParetoFront` `DATA_MODEL.md` §7
/// specifies. Search correctness itself is `pareto_search_test.cpp`'s job; this file
/// checks only the conversion layer.

#include "../../cpp/optimization/ParetoFront.hpp"
#include "../../cpp/graph/MissionGraph.hpp"
#include "../TestFramework.hpp"

using namespace orca;

namespace {

Orbit orbitAt(Kilometres altitudeKm, Radians phaseRad, Radians inclRad = 0.0) {
    return Orbit::fromAltitude(altitudeKm, inclRad, 0.0, phaseRad);
}

TransferConstraints permissive() {
    TransferConstraints c;
    c.maxDeltaVKms = 1.0e6;
    c.deltaVBudgetKms = 1.0e6;
    c.availableFuelFraction = 1.0;
    c.maxWaitSec = 1.0e13;
    c.latestArrivalSec = 1.0e18;
    return c;
}

SearchConstraints permissiveOptions() {
    SearchConstraints o;
    o.vehicleDeltaVBudgetKms = 1.0e6;
    o.maxWaitSec = 1.0e13;
    o.latestArrivalSec = 1.0e18;
    return o;
}

}  // namespace

ORCA_TEST(ParetoFront, TrivialSearchProducesEmptyFrontNotAThrownError) {
    // MissionPlan::makeFeasible rejects an empty route -- source == target has nothing to
    // plan, so the front must come back empty rather than attempting (and failing) to
    // build a zero-leg MissionPlan.
    MissionGraph g;
    const NodeId a = g.addNode(GraphNode{NodeId{0}, SatelliteId{0}, orbitAt(500.0, 0.0)});
    const TransferModel model;

    const ParetoSearchResult raw =
        findParetoRoutes(g, model, a, a, 0.0, permissiveOptions());
    CHECK_EQ(raw.solutions.size(), std::size_t{1});  // the search itself DOES produce one

    const ParetoFront front = buildParetoFront(raw, VehicleId(1), EmergencyId(1), PlanId(1));
    CHECK(front.isEmpty());
    CHECK(front.solutions.empty());
    CHECK(front.dominated.empty());
}

ORCA_TEST(ParetoFront, ConvertsRealSearchResultIntoFeasibleMissionPlans) {
    MissionGraph g;
    const Orbit s0 = orbitAt(500.0, 0.0);
    const Orbit fastMid = orbitAt(600.0, 0.3);
    const Orbit cheapMid = orbitAt(520.0, 1.5);
    const Orbit tgt = orbitAt(1500.0, 2.7);
    const NodeId ns = g.addNode(GraphNode{NodeId{0}, SatelliteId{0}, s0});
    const NodeId nFast = g.addNode(GraphNode{NodeId{0}, SatelliteId{1}, fastMid});
    const NodeId nCheap = g.addNode(GraphNode{NodeId{0}, SatelliteId{2}, cheapMid});
    const NodeId nt = g.addNode(GraphNode{NodeId{0}, SatelliteId{3}, tgt});
    const TransferModel model;
    const TransferConstraints c = permissive();
    g.addEdge(ns, nFast, model.evaluate(s0, fastMid, 0.0, c), 0.0);
    g.addEdge(ns, nCheap, model.evaluate(s0, cheapMid, 0.0, c), 0.0);
    g.addEdge(nFast, nt, model.evaluate(fastMid, tgt, 0.0, c), 0.0);
    g.addEdge(nCheap, nt, model.evaluate(cheapMid, tgt, 0.0, c), 0.0);

    const ParetoSearchResult raw =
        findParetoRoutes(g, model, ns, nt, 0.0, permissiveOptions());
    CHECK_EQ(raw.solutions.size(), std::size_t{2});

    const VehicleId vehicle(7);
    const EmergencyId emergency(3);
    const ParetoFront front = buildParetoFront(raw, vehicle, emergency, PlanId(100));

    CHECK_EQ(front.size(), std::size_t{2});
    for (const MissionPlan& plan : front.solutions) {
        CHECK(plan.isFeasible());
        CHECK(plan.vehicle() == vehicle);
        CHECK(plan.emergency() == emergency);
        CHECK(plan.legCount() >= 1);
        CHECK_NEAR(plan.departureTimeSec(), 0.0, 1e-9);
    }

    // Sequential PlanId assignment starting at the given firstPlanId.
    CHECK_EQ(front.solutions[0].id().value(), std::uint32_t{100});
    CHECK_EQ(front.solutions[1].id().value(), std::uint32_t{101});

    // Ideal/nadir must be the real componentwise extrema across the two solutions.
    const ObjectiveVector& a = front.solutions[0].totalCost();
    const ObjectiveVector& b = front.solutions[1].totalCost();
    CHECK_NEAR(front.ideal.time, std::min(a.time, b.time), 1e-6);
    CHECK_NEAR(front.nadir.time, std::max(a.time, b.time), 1e-6);
    CHECK_NEAR(front.ideal.distance, std::min(a.distance, b.distance), 1e-6);
    CHECK_NEAR(front.nadir.distance, std::max(a.distance, b.distance), 1e-6);
}

ORCA_TEST(ParetoFront, UnreachableTargetProducesEmptyFront) {
    MissionGraph g;
    const NodeId a = g.addNode(GraphNode{NodeId{0}, SatelliteId{0}, orbitAt(500.0, 0.0)});
    const NodeId isolated =
        g.addNode(GraphNode{NodeId{0}, SatelliteId{1}, orbitAt(2000.0, 0.0)});
    const TransferModel model;

    const ParetoSearchResult raw =
        findParetoRoutes(g, model, a, isolated, 0.0, permissiveOptions());
    const ParetoFront front = buildParetoFront(raw, VehicleId(1), EmergencyId(1), PlanId(1));
    CHECK(front.isEmpty());
}

ORCA_TEST(ParetoFront, MetricsPassThroughFromRawSearchResult) {
    MissionGraph g;
    const NodeId a = g.addNode(GraphNode{NodeId{0}, SatelliteId{0}, orbitAt(500.0, 0.0)});
    const NodeId b = g.addNode(GraphNode{NodeId{0}, SatelliteId{1}, orbitAt(800.0, 1.0)});
    const TransferModel model;
    const TransferConstraints c = permissive();
    g.addEdge(a, b, model.evaluate(orbitAt(500.0, 0.0), orbitAt(800.0, 1.0), 0.0, c), 0.0);

    const ParetoSearchResult raw =
        findParetoRoutes(g, model, a, b, 0.0, permissiveOptions());
    const ParetoFront front = buildParetoFront(raw, VehicleId(1), EmergencyId(1), PlanId(1));
    CHECK_EQ(front.truncated, raw.metrics.truncated);
    CHECK_EQ(front.labelsGenerated, raw.metrics.labelsGenerated);
    CHECK_EQ(front.labelsPruned,
            raw.metrics.labelsDominated + raw.metrics.labelsDuplicate);
}
