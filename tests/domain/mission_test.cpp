/// Tests for MissionPlan, Mission and Scenario.

#include <vector>

#include "../../cpp/domain/Mission.hpp"
#include "../../cpp/domain/MissionPlan.hpp"
#include "../../cpp/domain/Scenario.hpp"
#include "../TestFramework.hpp"

using namespace orca;

namespace {

ObjectiveVector sampleCost() {
    // The API_CONTRACT.md §4.4 recommended plan P-0007.
    return ObjectiveVector{708.4, 0.136, 0.072, 1842.6};
}

MissionPlan feasiblePlan(std::uint32_t planId = 7) {
    return MissionPlan::makeFeasible(PlanId{planId}, VehicleId{2}, EmergencyId{3},
                                     {EdgeId{11}, EdgeId{42}}, sampleCost(), 412.0,
                                     1120.4);
}

MissionPlan infeasiblePlan() {
    return MissionPlan::makeInfeasible(
        PlanId{9}, VehicleId{2}, EmergencyId{7},
        ConstraintViolation{ConstraintKind::DeltaVBudget,
                            "Best route requires 2.91 km/s; RV-02 has 2.16 km/s remaining",
                            0.75, "km/s"});
}

}  // namespace

// ---------------------------------------------------------------------------
// MissionPlan
// ---------------------------------------------------------------------------

ORCA_TEST(MissionPlan, FeasiblePlanCarriesRouteAndCost) {
    const MissionPlan plan = feasiblePlan();
    CHECK(plan.isFeasible());
    CHECK(plan.id() == PlanId{7});
    CHECK(plan.vehicle() == VehicleId{2});
    CHECK(plan.emergency() == EmergencyId{3});
    CHECK_EQ(plan.legCount(), std::size_t{2});
    CHECK_NEAR(plan.totalCost().time, 708.4, 1e-12);
    CHECK_NEAR(plan.totalCost().fuel, 0.136, 1e-12);
    CHECK(!plan.bindingConstraint().has_value());
}

ORCA_TEST(MissionPlan, InfeasiblePlanMustNameTheBindingConstraint) {
    // The invariant is structural: reaching the infeasible state requires supplying a
    // violation, so `feasible == false` implies `bindingConstraint.has_value()` by
    // construction rather than by assertion.
    const MissionPlan plan = infeasiblePlan();
    CHECK(!plan.isFeasible());
    CHECK(plan.bindingConstraint().has_value());
    CHECK(plan.bindingConstraint()->kind == ConstraintKind::DeltaVBudget);
    CHECK_NEAR(plan.bindingConstraint()->shortfall, 0.75, 1e-12);
    CHECK_EQ(plan.bindingConstraint()->unit, std::string{"km/s"});
    CHECK_EQ(plan.legCount(), std::size_t{0});
}

ORCA_TEST(MissionPlan, RejectsFeasiblePlanWithNoRoute) {
    CHECK_THROWS(MissionPlan::makeFeasible(PlanId{1}, VehicleId{1}, EmergencyId{1}, {},
                                           sampleCost(), 0.0, 100.0));
}

ORCA_TEST(MissionPlan, RejectsInvalidCostVector) {
    // Negative time, and risk outside [0,1], are both rejected.
    CHECK_THROWS(MissionPlan::makeFeasible(PlanId{1}, VehicleId{1}, EmergencyId{1},
                                           {EdgeId{1}},
                                           ObjectiveVector{-1.0, 0.1, 0.1, 1.0}, 0.0,
                                           100.0));
    CHECK_THROWS(MissionPlan::makeFeasible(PlanId{1}, VehicleId{1}, EmergencyId{1},
                                           {EdgeId{1}},
                                           ObjectiveVector{1.0, 0.1, 1.5, 1.0}, 0.0,
                                           100.0));
}

ORCA_TEST(MissionPlan, RejectsArrivalBeforeDeparture) {
    CHECK_THROWS(MissionPlan::makeFeasible(PlanId{1}, VehicleId{1}, EmergencyId{1},
                                           {EdgeId{1}}, sampleCost(), 500.0, 400.0));
    // Equal times are allowed: a zero-duration transfer is degenerate but not invalid.
    CHECK_NO_THROW(MissionPlan::makeFeasible(PlanId{1}, VehicleId{1}, EmergencyId{1},
                                             {EdgeId{1}}, sampleCost(), 500.0, 500.0));
}

ORCA_TEST(MissionPlan, RejectsInfeasiblePlanWithoutExplanation) {
    CHECK_THROWS(MissionPlan::makeInfeasible(
        PlanId{1}, VehicleId{1}, EmergencyId{1},
        ConstraintViolation{ConstraintKind::NoAsset, "", 0.0, ""}));
}

// ---------------------------------------------------------------------------
// Mission
// ---------------------------------------------------------------------------

ORCA_TEST(Mission, CommitsFromFeasiblePlan) {
    const Mission m{MissionId{1}, feasiblePlan(), 412.0};
    CHECK(m.id() == MissionId{1});
    CHECK(m.sourcePlan() == PlanId{7});
    CHECK(m.vehicle() == VehicleId{2});
    CHECK(m.emergency() == EmergencyId{3});
    CHECK(m.state() == MissionState::Planned);
    CHECK_EQ(m.currentRoute().size(), std::size_t{2});
    CHECK_EQ(m.currentLeg(), std::size_t{0});
    CHECK_NEAR(m.progressOnLeg(), 0.0, 1e-15);
    CHECK_EQ(m.replanCount(), 0);
    CHECK(!m.isTerminal());
    CHECK(!m.isActive());
}

ORCA_TEST(Mission, RefusesToCommitInfeasiblePlan) {
    // The failure mode the whole hard-constraint design exists to prevent.
    CHECK_THROWS(Mission(MissionId{1}, infeasiblePlan(), 0.0));
}

ORCA_TEST(Mission, TransitionTable) {
    using M = MissionState;
    CHECK(isValidTransition(M::Planned, M::Launched));
    CHECK(isValidTransition(M::Planned, M::Aborted));
    CHECK(!isValidTransition(M::Planned, M::Success));
    CHECK(isValidTransition(M::Launched, M::InTransit));
    CHECK(isValidTransition(M::InTransit, M::Replanning));
    CHECK(isValidTransition(M::InTransit, M::Rendezvous));
    CHECK(isValidTransition(M::Replanning, M::InTransit));
    CHECK(isValidTransition(M::Replanning, M::Aborted));
    CHECK(isValidTransition(M::Rendezvous, M::Success));
    CHECK(isValidTransition(M::Rendezvous, M::Failed));
    CHECK(!isValidTransition(M::Success, M::Failed));
    CHECK(!isValidTransition(M::Aborted, M::InTransit));
}

ORCA_TEST(Mission, HappyPathLifecycle) {
    Mission m{MissionId{1}, feasiblePlan(), 412.0};
    m.setState(MissionState::Launched);
    CHECK(m.isActive());
    m.setState(MissionState::InTransit);
    m.setState(MissionState::Rendezvous);
    m.setState(MissionState::Success);
    CHECK(m.isTerminal());
    CHECK(!m.isActive());
    CHECK_THROWS(m.setState(MissionState::InTransit));
}

ORCA_TEST(Mission, ProgressIsBounded) {
    Mission m{MissionId{1}, feasiblePlan(), 412.0};
    CHECK_NO_THROW(m.setProgressOnLeg(0.0));
    CHECK_NO_THROW(m.setProgressOnLeg(1.0));
    CHECK_NO_THROW(m.setProgressOnLeg(0.34));
    CHECK_NEAR(m.progressOnLeg(), 0.34, 1e-15);
    CHECK_THROWS(m.setProgressOnLeg(-0.1));
    CHECK_THROWS(m.setProgressOnLeg(1.1));
}

ORCA_TEST(Mission, AdvanceLegResetsProgress) {
    Mission m{MissionId{1}, feasiblePlan(), 412.0};
    m.setProgressOnLeg(0.9);
    CHECK(!m.isOnFinalLeg());
    m.advanceLeg();
    CHECK_EQ(m.currentLeg(), std::size_t{1});
    CHECK_NEAR(m.progressOnLeg(), 0.0, 1e-15);
    CHECK(m.isOnFinalLeg());
}

ORCA_TEST(Mission, CannotAdvancePastFinalLeg) {
    // The route has two legs; after advancing once, the mission is on the final leg and
    // must transition to Rendezvous rather than advancing again.
    Mission m{MissionId{1}, feasiblePlan(), 412.0};
    m.advanceLeg();
    CHECK(m.isOnFinalLeg());
    CHECK_THROWS(m.advanceLeg());
    CHECK_EQ(m.currentLeg(), std::size_t{1});
}

ORCA_TEST(Mission, DeltaVAccumulates) {
    Mission m{MissionId{1}, feasiblePlan(), 412.0};
    m.recordDeltaVSpent(0.2);
    m.recordDeltaVSpent(0.14);
    CHECK_NEAR(m.deltaVSpentKms(), 0.34, 1e-12);
    CHECK_THROWS(m.recordDeltaVSpent(-0.1));
    CHECK_NEAR(m.deltaVSpentKms(), 0.34, 1e-12);  // unchanged
}

ORCA_TEST(Mission, AdoptRouteRecordsRejectedAlternativeAndResets) {
    Mission m{MissionId{1}, feasiblePlan(7), 412.0};
    m.advanceLeg();
    m.setProgressOnLeg(0.5);

    const MissionPlan replacement = MissionPlan::makeFeasible(
        PlanId{19}, VehicleId{2}, EmergencyId{3}, {EdgeId{5}, EdgeId{6}, EdgeId{7}},
        ObjectiveVector{1024.7, 0.098, 0.041, 2210.3}, 598.5, 1623.2);

    m.adoptRoute(replacement);

    CHECK(m.sourcePlan() == PlanId{19});
    CHECK_EQ(m.currentRoute().size(), std::size_t{3});
    CHECK_EQ(m.replanCount(), 1);
    // Progress resets because the new route starts from the vehicle's current position.
    CHECK_EQ(m.currentLeg(), std::size_t{0});
    CHECK_NEAR(m.progressOnLeg(), 0.0, 1e-15);
    // The abandoned plan is retained so the UI can say what ORCA chose against.
    CHECK_EQ(m.rejectedAlternatives().size(), std::size_t{1});
    CHECK(m.rejectedAlternatives()[0] == PlanId{7});
}

ORCA_TEST(Mission, RefusesToAdoptInfeasibleRoute) {
    Mission m{MissionId{1}, feasiblePlan(), 412.0};
    CHECK_THROWS(m.adoptRoute(infeasiblePlan()));
    CHECK_EQ(m.replanCount(), 0);
}

ORCA_TEST(Mission, CompletionTimeCannotPrecedeStart) {
    Mission m{MissionId{1}, feasiblePlan(), 412.0};
    CHECK_THROWS(m.recordCompletion(400.0));
    CHECK_NO_THROW(m.recordCompletion(1120.4));
    CHECK(m.completedAtSec().has_value());
    CHECK_NEAR(*m.completedAtSec(), 1120.4, 1e-12);
}

// ---------------------------------------------------------------------------
// Scenario
// ---------------------------------------------------------------------------

namespace {

Scenario tinyScenario() {
    std::vector<Satellite> sats;
    sats.emplace_back(SatelliteId{0}, "Alpha", Orbit::fromAltitude(500.0, 0.9, 0.0, 0.0));
    sats.emplace_back(SatelliteId{1}, "Beta", Orbit::fromAltitude(800.0, 0.9, 0.1, 1.0));

    std::vector<RescueVehicle> vehicles;
    vehicles.emplace_back(VehicleId{0}, "Aegis-1",
                          Orbit::fromAltitude(550.0, 0.9, 0.0, 0.5), 2.5);

    std::vector<DebrisObject> debris;
    debris.emplace_back(DebrisId{0}, Orbit::fromAltitude(600.0, 0.9, 0.2, 2.0), 18.0, 0.7);

    return Scenario{"tiny", 928371, SimulationConfig{}, std::move(sats),
                    std::move(vehicles), std::move(debris)};
}

}  // namespace

ORCA_TEST(Scenario, HoldsEntitiesAndSeed) {
    const Scenario s = tinyScenario();
    CHECK_EQ(s.name(), std::string{"tiny"});
    CHECK_EQ(s.seed(), Seed{928371});
    CHECK_EQ(s.satellites().size(), std::size_t{2});
    CHECK_EQ(s.vehicles().size(), std::size_t{1});
    CHECK_EQ(s.debris().size(), std::size_t{1});
    CHECK_EQ(s.objectCount(), std::size_t{4});
}

ORCA_TEST(Scenario, LookupFindsPresentAndMissing) {
    const Scenario s = tinyScenario();
    const Satellite* found = s.findSatellite(SatelliteId{1});
    CHECK(found != nullptr);
    if (found != nullptr) {
        CHECK_EQ(found->name(), std::string{"Beta"});
    }
    // A missing id is a normal question, answered with nullptr rather than an exception.
    CHECK(s.findSatellite(SatelliteId{99}) == nullptr);
    CHECK(s.findVehicle(VehicleId{0}) != nullptr);
    CHECK(s.findVehicle(VehicleId{99}) == nullptr);
    CHECK(s.findDebris(DebrisId{0}) != nullptr);
    CHECK(s.findDebris(DebrisId{99}) == nullptr);
}

ORCA_TEST(Scenario, RejectsDuplicateIds) {
    // Duplicated ids would make lookup ambiguous and, worse, would silently produce two
    // graph nodes for one physical object.
    std::vector<Satellite> sats;
    sats.emplace_back(SatelliteId{0}, "Alpha", Orbit::fromAltitude(500.0, 0.0, 0.0, 0.0));
    sats.emplace_back(SatelliteId{0}, "Clone", Orbit::fromAltitude(600.0, 0.0, 0.0, 0.0));
    CHECK_THROWS(Scenario("dup", 1, SimulationConfig{}, std::move(sats), {}, {}));
}

ORCA_TEST(Scenario, RejectsInvalidConfiguration) {
    CHECK_THROWS(Scenario("", 1, SimulationConfig{}, {}, {}, {}));
    CHECK_THROWS(Scenario("s", 1, SimulationConfig{0.0, 100.0, 10.0}, {}, {}, {}));
    CHECK_THROWS(Scenario("s", 1, SimulationConfig{1.0, 0.0, 10.0}, {}, {}, {}));
    CHECK_THROWS(Scenario("s", 1, SimulationConfig{1.0, 100.0, 0.0}, {}, {}, {}));
}

ORCA_TEST(Scenario, EmptyScenarioIsValid) {
    // An empty world is a legitimate edge case, not an error -- TEST_PLAN.md §12 lists
    // it, and the graph builder must handle it in Phase 2.
    CHECK_NO_THROW(Scenario("empty", 1, SimulationConfig{}, {}, {}, {}));
}
