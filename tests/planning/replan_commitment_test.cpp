/// Replanning must start from the end of an in-progress leg, at the time that leg actually
/// arrives, and the replacement plan must keep that leg so its delta-v is charged.
///
/// Regression test for a bug where a mid-leg switch restarted the route from the in-progress
/// leg's end node at the CURRENT time: the rest of that leg's flight time was skipped and its
/// delta-v was never charged.

#include <stdexcept>

#include "../../cpp/domain/Emergency.hpp"
#include "../../cpp/domain/Mission.hpp"
#include "../../cpp/domain/RescueVehicle.hpp"
#include "../../cpp/graph/MissionGraph.hpp"
#include "../../cpp/optimization/CandidateGeneration.hpp"
#include "../../cpp/planning/DynamicReplanner.hpp"
#include "../../cpp/planning/MissionValidity.hpp"
#include "../../cpp/simulation/MissionExecutor.hpp"
#include "../TestFramework.hpp"

using namespace orca;

namespace {

TransferConstraints permissive() {
    TransferConstraints c;
    c.maxDeltaVKms = 1.0e6;
    c.deltaVBudgetKms = 1.0e6;
    c.availableFuelFraction = 1.0;
    c.maxWaitSec = 1.0e13;
    c.latestArrivalSec = 1.0e18;
    return c;
}

/// V1 -> M -> E1, with a detour M -> N -> E1 available once M -> E1 is lost.
struct ChainFixture {
    MissionGraph graph;
    TransferModel model;
    RescueVehicle vehicle;
    Emergency emergency;
    EdgeId v1ToM, mToE1, mToN, nToE1;
};

ChainFixture buildChain() {
    MissionGraph g;
    TransferModel model;
    const TransferConstraints c = permissive();
    const Orbit v1o = Orbit::fromAltitude(500.0, degToRad(5.0), 0.0, 0.0);
    const Orbit mo = Orbit::fromAltitude(560.0, degToRad(6.0), 0.0, degToRad(0.4));
    const Orbit no = Orbit::fromAltitude(600.0, degToRad(6.5), 0.0, degToRad(1.1));
    const Orbit e1o = Orbit::fromAltitude(650.0, degToRad(7.0), 0.0, degToRad(2.0));
    const NodeId v1 = g.addNode(GraphNode{NodeId{0}, VehicleId(1), v1o});
    const NodeId e1 = g.addNode(GraphNode{NodeId{0}, SatelliteId(101), e1o});
    const NodeId m = g.addNode(GraphNode{NodeId{0}, SatelliteId(201), mo});
    const NodeId n = g.addNode(GraphNode{NodeId{0}, SatelliteId(202), no});
    auto add = [&](NodeId a, NodeId b) {
        return g.addEdge(a, b, model.evaluate(g.node(a).orbit(), g.node(b).orbit(), 0.0, c), 0.0);
    };
    const EdgeId e0 = add(v1, m);
    const EdgeId e1e = add(m, e1);
    const EdgeId e2 = add(m, n);
    const EdgeId e3 = add(n, e1);
    RescueVehicle rv(VehicleId(1), "RV-1", v1o, 10.0);
    Emergency em(EmergencyId(101), SatelliteId(101), EmergencyKind::PowerFailure, Severity::High,
                 0.0, 5.0e7);
    return ChainFixture{std::move(g), std::move(model), std::move(rv), std::move(em),
                        e0, e1e, e2, e3};
}

/// The two-leg plan V1 -> M -> E1 from the real candidate front.
MissionPlan directPlan(ChainFixture& f) {
    PlanId next(1);
    const EntityNodeIndex index = buildEntityNodeIndex(f.graph);
    const CandidateFront front =
        generateCandidateFront(f.graph, f.model, index, f.vehicle, f.emergency, 0.0, next);
    for (const MissionPlan& p : front.front.solutions) {
        if (p.route() == std::vector<EdgeId>{f.v1ToM, f.mToE1}) return p;
    }
    for (const MissionPlan& p : front.front.dominated) {
        if (p.route() == std::vector<EdgeId>{f.v1ToM, f.mToE1}) return p;
    }
    throw std::runtime_error("fixture: no V1->M->E1 plan on the candidate front");
}

}  // namespace

ORCA_TEST(ReplanCommitment, MidLegSwitchKeepsInProgressLegAndItsTiming) {
    ChainFixture f = buildChain();
    const MissionPlan plan = directPlan(f);
    Mission mission(MissionId(1), plan, 0.0);
    mission.setState(MissionState::Launched);
    mission.setState(MissionState::InTransit);

    const auto timings = computeLegTimings(f.graph, f.model, plan.route(), plan.departureTimeSec());
    const Seconds now = 0.5 * (timings[0].departureSec + timings[0].arrivalSec);
    mission.setProgressOnLeg(0.5);  // halfway through V1 -> M

    DynamicReplanner replanner;
    PlanId next(1000);
    const MissionChange lost{.kind = ChangeKind::RouteInvalidated, .atSec = now,
                             .invalidatedEdge = f.mToE1};
    const ReplanDecision d = replanner.evaluate(mission, f.vehicle, f.emergency, f.graph, f.model,
                                                now, lost, OptimizationProfile::balanced(), next);

    CHECK(d.decision == ReplanDecisionType::Replan);
    CHECK(d.newPlan.has_value());
    const MissionPlan& np = *d.newPlan;
    // The in-progress leg is kept, and the new plan departs when the old one did.
    CHECK(np.route() == (std::vector<EdgeId>{f.v1ToM, f.mToN, f.nToE1}));
    CHECK_NEAR(np.departureTimeSec(), plan.departureTimeSec(), 1e-9);
    // Re-timing the new plan reproduces the in-progress leg exactly, and the detour departs
    // from M only once the vehicle actually gets there.
    const auto nt = computeLegTimings(f.graph, f.model, np.route(), np.departureTimeSec());
    CHECK_NEAR(nt[0].arrivalSec, timings[0].arrivalSec, 1e-6);
    CHECK(nt[1].departureSec >= timings[0].arrivalSec - 1e-6);
    // The committed leg's delta-v is part of the new plan's cost.
    CHECK(np.totalCost().fuel * f.vehicle.deltaVBudgetKms() >= timings[0].deltaVKms - 1e-9);
}

ORCA_TEST(ReplanCommitment, ValidityTimesRemainingRouteFromEndOfInProgressLeg) {
    ChainFixture f = buildChain();
    const MissionPlan plan = directPlan(f);
    Mission mission(MissionId(1), plan, 0.0);
    mission.setState(MissionState::Launched);
    mission.setState(MissionState::InTransit);
    const auto timings = computeLegTimings(f.graph, f.model, plan.route(), plan.departureTimeSec());
    const Seconds now = 0.5 * (timings[0].departureSec + timings[0].arrivalSec);
    mission.setProgressOnLeg(0.5);

    const CommitmentPoint cp =
        commitmentPoint(mission, f.graph, f.model, now, f.vehicle.deltaVBudgetKms());
    CHECK(cp.inProgressEdge.has_value());
    CHECK(cp.startLeg == 1);
    CHECK_NEAR(cp.readyAtSec, timings[0].arrivalSec, 1e-6);
    CHECK_NEAR(cp.committedDeltaVKms, timings[0].deltaVKms, 1e-12);

    const MissionValidity v = evaluateMissionValidity(mission, f.vehicle, f.emergency, f.graph,
                                                      f.model, now, defaultReplanPolicy());
    CHECK(v.remaining.feasible);
    CHECK_NEAR(v.remaining.arrivalTimeSec, timings[1].arrivalSec, 1e-6);
}

ORCA_TEST(ReplanCommitment, NotMidLegMeansFreeToChangeNow) {
    ChainFixture f = buildChain();
    const MissionPlan plan = directPlan(f);
    Mission mission(MissionId(1), plan, 0.0);
    const CommitmentPoint cp =
        commitmentPoint(mission, f.graph, f.model, 0.0, f.vehicle.deltaVBudgetKms());
    CHECK(!cp.inProgressEdge.has_value());
    CHECK(cp.startLeg == 0);
    CHECK_NEAR(cp.readyAtSec, 0.0, 1e-12);
    CHECK_NEAR(cp.committedDeltaVKms, 0.0, 1e-12);
}
