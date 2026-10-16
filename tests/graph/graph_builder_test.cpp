/// Tests for GraphBuilder: candidate generation, eligibility filtering, and rejection
/// accounting.
///
/// Every numeric expectation here (candidate pair counts, rejection counts) was verified
/// by compiling and running this exact scenario against the real `GraphBuilder` before
/// being written into an assertion — the scenario is simple enough that the expected
/// values are also derivable by hand (4 eligible nodes -> 12 ordered pairs; the one
/// same-altitude, misaligned-phase pair is permanently unreachable in both directions ->
/// exactly 2 `PhasingUnavailable` rejections, 10 feasible edges), so this is a case where
/// hand-derivation and execution agree, which is the point.

#include "../../cpp/domain/Scenario.hpp"
#include "../../cpp/graph/GraphBuilder.hpp"
#include "../TestFramework.hpp"

using namespace orca;

namespace {

/// 4 eligible nodes (SAT-0, SAT-1, SAT-2, RV-0), 1 excluded satellite (SAT-3, terminal),
/// 1 excluded vehicle (RV-1, disabled). SAT-0 and SAT-1 share an altitude with a
/// misaligned phase and zero relative drift, so no rendezvous window will ever open
/// between them (PhasingModel.hpp) -- a permanent, build-time-decidable fact.
Scenario builderTestScenario() {
    std::vector<Satellite> sats;
    sats.emplace_back(SatelliteId{0}, "SAT-0", Orbit::fromAltitude(500.0, 0.0, 0.0, 0.0));
    sats.emplace_back(SatelliteId{1}, "SAT-1", Orbit::fromAltitude(500.0, 0.0, 0.0, 1.0));
    sats.emplace_back(SatelliteId{2}, "SAT-2", Orbit::fromAltitude(800.0, 0.0, 0.0, 0.5));
    sats.emplace_back(SatelliteId{3}, "SAT-3", Orbit::fromAltitude(900.0, 0.0, 0.0, 0.5));
    sats[3].applyDamage(Subsystem::Power, 1.0);
    sats[3].setStatus(SatelliteStatus::Rescued);  // terminal -> excluded

    std::vector<RescueVehicle> vehicles;
    vehicles.emplace_back(VehicleId{0}, "RV-0",
                          Orbit::fromAltitude(650.0, 0.0, 0.0, 0.2), 2.5);
    vehicles.emplace_back(VehicleId{1}, "RV-1",
                          Orbit::fromAltitude(650.0, 0.0, 0.0, 0.2), 2.5);
    vehicles[1].setStatus(VehicleStatus::Disabled);  // excluded

    return Scenario{"builder-test", 1,          SimulationConfig{},
                    std::move(sats), std::move(vehicles), {}};
}

}  // namespace

// ---------------------------------------------------------------------------
// Eligibility filtering
// ---------------------------------------------------------------------------

ORCA_TEST(GraphBuilder, ExcludesTerminalSatellitesAndDisabledVehicles) {
    const Scenario scenario = builderTestScenario();
    const TransferModel model;
    const auto out = GraphBuilder::build(scenario, model);

    // 4 scenario entities are eligible: SAT-0, SAT-1, SAT-2, RV-0. SAT-3 (Rescued) and
    // RV-1 (Disabled) must not appear as graph nodes at all.
    CHECK_EQ(out.graph.nodeCount(), std::size_t{4});
}

// ---------------------------------------------------------------------------
// Candidate generation and rejection accounting
// ---------------------------------------------------------------------------

ORCA_TEST(GraphBuilder, CandidatePairsIsNTimesNMinusOne) {
    const Scenario scenario = builderTestScenario();
    const TransferModel model;
    const auto out = GraphBuilder::build(scenario, model);

    // 4 eligible nodes -> 4*3 = 12 ordered pairs, self-pairs excluded.
    CHECK_EQ(out.stats.candidatePairs, 12);
}

ORCA_TEST(GraphBuilder, PhasingUnavailableIsItsOwnCategoryNeverGenericInfeasible) {
    const Scenario scenario = builderTestScenario();
    const TransferModel model;
    const auto out = GraphBuilder::build(scenario, model);

    // SAT-0 <-> SAT-1: same altitude, misaligned phase, zero drift -> unreachable in
    // BOTH directions. Exactly this reason, exactly this count -- not folded into a
    // generic bucket (GraphBuildStats.hpp's central requirement).
    CHECK_EQ(out.stats.rejectedPhasingUnavailable, 2);
    CHECK_EQ(out.stats.feasibleEdges, 10);
    CHECK_EQ(out.stats.rejectedInvalidOrbit, 0);
    CHECK_EQ(out.stats.rejectedDistanceExceeded, 0);
    CHECK_EQ(out.stats.rejectedRiskExceeded, 0);
}

ORCA_TEST(GraphBuilder, RejectionAccountingIsExhaustive) {
    // candidatePairs must equal feasibleEdges + every rejection reason summed -- no
    // candidate pair is silently unaccounted for.
    const Scenario scenario = builderTestScenario();
    const TransferModel model;
    const auto out = GraphBuilder::build(scenario, model);

    CHECK_EQ(out.stats.candidatePairs,
             out.stats.feasibleEdges + out.stats.totalRejected());
    CHECK_EQ(out.graph.edgeCount(), static_cast<std::size_t>(out.stats.feasibleEdges));
}

ORCA_TEST(GraphBuilder, CapabilityAndDeadlineAreNeverRejectedAtBuildTime) {
    // Group 1 vs Group 2/3 (GraphBuilder.hpp): capability and deadline are vehicle- and
    // time-specific, so build time -- which knows neither -- must never produce these.
    const Scenario scenario = builderTestScenario();
    const TransferModel model;
    const auto out = GraphBuilder::build(scenario, model);

    CHECK_EQ(out.stats.rejectedDeltaVExceeded, 0);
    CHECK_EQ(out.stats.rejectedFuelInsufficient, 0);
    CHECK_EQ(out.stats.rejectedDeadlineExceeded, 0);
}

ORCA_TEST(GraphBuilder, ResultingGraphIsStructurallyValid) {
    const Scenario scenario = builderTestScenario();
    const TransferModel model;
    const auto out = GraphBuilder::build(scenario, model);
    CHECK(out.graph.isStructurallyValid());
}

ORCA_TEST(GraphBuilder, BuildTimeIsRecordedAndNonNegative) {
    const Scenario scenario = builderTestScenario();
    const TransferModel model;
    const auto out = GraphBuilder::build(scenario, model);
    CHECK(out.stats.buildTimeMs >= 0.0);
}

// ---------------------------------------------------------------------------
// Reference-time invariance -- the structural consequence of TransferModel's second
// Phase 2 property (non-time objectives, and window existence, independent of time)
// ---------------------------------------------------------------------------

ORCA_TEST(GraphBuilder, EdgeSetIsInvariantToReferenceTime) {
    // Which edges exist is a Group 1 (time-independent) fact. Building the same scenario
    // at two different reference times must produce the identical (from,to) edge set --
    // only the cached referenceResult's wait/arrival fields may differ.
    const TransferModel model;

    GraphBuilder::Options optionsAtZero;
    optionsAtZero.referenceTimeSec = 0.0;
    const auto outAtZero = GraphBuilder::build(builderTestScenario(), model, optionsAtZero);

    GraphBuilder::Options optionsLater;
    optionsLater.referenceTimeSec = 5000.0;
    const auto outLater = GraphBuilder::build(builderTestScenario(), model, optionsLater);

    CHECK_EQ(outAtZero.graph.edgeCount(), outLater.graph.edgeCount());
    CHECK_EQ(outAtZero.stats.feasibleEdges, outLater.stats.feasibleEdges);
    CHECK_EQ(outAtZero.stats.rejectedPhasingUnavailable,
             outLater.stats.rejectedPhasingUnavailable);

    for (std::size_t i = 0; i < outAtZero.graph.edgeCount(); ++i) {
        const EdgeId id{static_cast<EdgeId::Value>(i)};
        CHECK(outAtZero.graph.edge(id).from() == outLater.graph.edge(id).from());
        CHECK(outAtZero.graph.edge(id).to() == outLater.graph.edge(id).to());
    }
}

// ---------------------------------------------------------------------------
// Policy limits (Group 1: legitimately enforced once, at build time)
// ---------------------------------------------------------------------------

ORCA_TEST(GraphBuilder, TightDistanceCeilingRejectsEveryGeometricTransfer) {
    const TransferModel model;
    GraphBuilder::Options options;
    options.maxPathLengthKm = 1.0;  // no real transfer arc is this short

    const auto out = GraphBuilder::build(builderTestScenario(), model, options);

    CHECK_EQ(out.stats.feasibleEdges, 0);
    CHECK_EQ(out.stats.rejectedDistanceExceeded, 10);
    // The 2 same-altitude pairs still fail for the PRIOR reason in the check order
    // (PhasingUnavailable is checked before DistanceExceeded) -- accounting stays exact.
    CHECK_EQ(out.stats.rejectedPhasingUnavailable, 2);
    CHECK_EQ(out.stats.candidatePairs,
             out.stats.feasibleEdges + out.stats.totalRejected());
}

ORCA_TEST(GraphBuilder, TightRiskCeilingRejectsEveryTransfer) {
    const TransferModel model;
    GraphBuilder::Options options;
    options.maxRisk = 0.0;  // no real transfer has exactly zero risk

    const auto out = GraphBuilder::build(builderTestScenario(), model, options);

    CHECK_EQ(out.stats.feasibleEdges, 0);
    CHECK_EQ(out.stats.rejectedRiskExceeded, 10);
    CHECK_EQ(out.stats.rejectedPhasingUnavailable, 2);
}

// ---------------------------------------------------------------------------
// Edge cases
// ---------------------------------------------------------------------------

ORCA_TEST(GraphBuilder, EmptyScenarioProducesEmptyGraph) {
    const Scenario empty{"empty", 1, SimulationConfig{}, {}, {}, {}};
    const TransferModel model;
    const auto out = GraphBuilder::build(empty, model);

    CHECK_EQ(out.graph.nodeCount(), std::size_t{0});
    CHECK_EQ(out.graph.edgeCount(), std::size_t{0});
    CHECK_EQ(out.stats.candidatePairs, 0);
    CHECK(out.graph.isStructurallyValid());
}

ORCA_TEST(GraphBuilder, SingleNodeProducesNoEdges) {
    std::vector<Satellite> sats;
    sats.emplace_back(SatelliteId{0}, "SAT-0", Orbit::fromAltitude(500.0, 0.0, 0.0, 0.0));
    const Scenario scenario{"one", 1, SimulationConfig{}, std::move(sats), {}, {}};
    const TransferModel model;
    const auto out = GraphBuilder::build(scenario, model);

    CHECK_EQ(out.graph.nodeCount(), std::size_t{1});
    CHECK_EQ(out.graph.edgeCount(), std::size_t{0});
    CHECK_EQ(out.stats.candidatePairs, 0);  // no self-pairs attempted
}

ORCA_TEST(GraphBuilder, AllTerminalSatellitesProduceEmptyGraph) {
    std::vector<Satellite> sats;
    sats.emplace_back(SatelliteId{0}, "SAT-0", Orbit::fromAltitude(500.0, 0.0, 0.0, 0.0));
    sats[0].applyDamage(Subsystem::Power, 1.0);
    sats[0].setStatus(SatelliteStatus::Lost);
    const Scenario scenario{"lost", 1, SimulationConfig{}, std::move(sats), {}, {}};
    const TransferModel model;
    const auto out = GraphBuilder::build(scenario, model);

    CHECK_EQ(out.graph.nodeCount(), std::size_t{0});
}
