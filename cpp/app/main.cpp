/// @file main.cpp
/// ORCA engine entry point.
///
/// Two modes:
///   `orca_engine --version`  -- report the version, same as every prior phase.
///   `orca_engine --serve`    -- Phase 9's NDJSON protocol loop (`INTEGRATION_ARCHITECTURE.md`):
///                               boot the canonical demo scenario, print a `ready` message,
///                               then read one JSON command per line from stdin and write one
///                               JSON response per line to stdout, until `shutdown` or EOF.
///   `orca_engine --serve <family>:<size>[:<seed>]` -- Phase 11: the same protocol loop, but
///                               booted from a generated scenario (`ScenarioGenerator.hpp`'s
///                               families) instead of the small fixed demo one, e.g.
///                               `--serve dense:500` -- for measuring frontend rendering at
///                               scale (brief §32-35).
///   `orca_engine --visualScenario <family>:<size>[:<seed>] --time <sec>` -- Phase 12 brief
///                               §5/§19: one-shot, NOT a persistent protocol session --
///                               generates a scenario, projects real positions at the given
///                               time, prints one JSON object, exits. Deliberately bypasses
///                               `SimulationEngine`/`GraphBuilder` entirely (see
///                               `cpp/io/VisualConstellation.hpp`) so it stays cheap at
///                               thousands of objects, a scale the planning graph was never
///                               meant to run at.
///
/// `--serve` was deferred in every earlier phase's version of this file under a stale
/// "Phase 14" label (the same kind of pre-Phase-8 renumbering staleness
/// `IMPLEMENTATION_ROADMAP.md` has already corrected elsewhere) -- it is Phase 9's actual
/// scope, built here for the first time.
///
/// stdout carries protocol only (API_CONTRACT.md §1.6); this file never writes anything
/// else to it. Anything diagnostic goes to stderr.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <functional>
#include <iostream>
#include <string>

#include <vector>

#include "../algorithms/AStar.hpp"
#include "../algorithms/Dijkstra.hpp"
#include "../algorithms/ParetoSearch.hpp"
#include "../analytics/AlgorithmRun.hpp"
#include "../analytics/AssignmentBenchmark.hpp"
#include "../analytics/BenchmarkRunner.hpp"
#include "../analytics/CsvWriter.hpp"
#include "../core/Constants.hpp"
#include "../domain/Orbit.hpp"
#include "../io/CommandProtocol.hpp"
#include "../io/JsonParser.hpp"
#include "../io/JsonSerializer.hpp"
#include "../io/ViewProjection.hpp"
#include "../io/VisualConstellation.hpp"
#include "../optimization/CandidateGeneration.hpp"
#include "../optimization/MissionScorer.hpp"
#include "../optimization/OptimizationProfile.hpp"
#include "../planning/GlobalReplanner.hpp"
#include "../simulation/DemoScenario.hpp"
#include "../simulation/EmergencyGenerator.hpp"
#include "../simulation/ScenarioGenerator.hpp"
#include "../simulation/SimulationEngine.hpp"

namespace {

constexpr const char* kVersion = "0.9.0";
constexpr int kSchemaVersion = 1;

void printVersion() { std::printf("orca_engine %s\n", kVersion); }

void printStatus() {
    std::printf(
        "ORCA engine %s -- Phase 12 (visual constellation, on-demand algorithm queries)\n"
        "\n"
        "Built: domain foundation, mission graph, path search, Pareto optimization,\n"
        "       assignment, dynamic replanning, continuous simulation engine, NDJSON\n"
        "       snapshot/command protocol, deterministic scenario generation, algorithm\n"
        "       telemetry capture, the benchmark runner, and a lightweight large-scale\n"
        "       visual constellation projection (this phase).\n"
        "\n"
        "Run with --serve to start the protocol loop (optionally --serve <family>:<size>[:<seed>]\n"
        "to boot a large generated scenario instead of the small demo one, e.g.\n"
        "--serve dense:500), --bench to run the benchmark suite (writes to\n"
        "benchmarks/results/), --visualScenario <family>:<size>[:<seed>] --time <sec> for a\n"
        "one-shot large constellation projection (no engine/graph construction), or run the\n"
        "test suite to verify the engine: scripts/run_tests.sh\n"
        "\n"
        "Reference constants: mu = %.4f km^3/s^2, R_earth = %.3f km\n",
        kVersion, orca::kMuEarth, orca::kEarthRadiusKm);
}

/// Emits one line of protocol to stdout, flushed immediately (brief §25 -- Node must never
/// see this appear "frozen" because output stayed buffered).
void emitLine(const std::string& line) { std::cout << line << '\n' << std::flush; }

/// Parses the engine's own `PREFIX-DDDD` wire form (`toString(Id<Tag>)`,
/// `INTEGRATION_ARCHITECTURE.md` §4.1) back into a raw value -- the one place this project
/// needs the reverse direction, since every ID otherwise only ever flows engine-to-wire.
///
/// @throws std::invalid_argument if `text` has no '-' or the suffix is not a valid integer.
std::uint32_t parseIdSuffix(const std::string& text) {
    const std::size_t dash = text.rfind('-');
    if (dash == std::string::npos || dash + 1 >= text.size()) {
        throw std::invalid_argument("parseIdSuffix: '" + text + "' is not a PREFIX-DDDD id");
    }
    return static_cast<std::uint32_t>(std::stoul(text.substr(dash + 1)));
}

orca::OptimizationProfile resolveNamedProfile(const std::string& name) {
    if (name == "FASTEST") return orca::OptimizationProfile::fastest();
    if (name == "FUEL_SAVING") return orca::OptimizationProfile::fuelSaving();
    if (name == "SAFETY_FIRST") return orca::OptimizationProfile::safetyFirst();
    if (name == "BALANCED") return orca::OptimizationProfile::balanced();
    throw std::invalid_argument("resolveNamedProfile: unknown profile '" + name +
                                "' (expected FASTEST | FUEL_SAVING | SAFETY_FIRST | BALANCED)");
}

/// Phase 11 brief §50-51: re-scores an ALREADY-CAPTURED Pareto front under a different
/// profile -- `MissionScorer::selectRecommendedPlan()` only, no search, no new transfer
/// evaluation. `SimulationEngine.hpp`'s own `stateMutable()` accessor is used here exactly
/// as `EventDispatcher.cpp` uses it (the same "mutate through validated domain/state APIs,
/// from the one place that owns this decision" discipline extended to a Phase 11 command).
void applyRescoreCommand(orca::SimulationEngine& engine, const orca::io::Command& command) {
    orca::SimulationState& state = engine.stateMutable();
    const std::uint32_t missionId = parseIdSuffix(*command.missionId);
    const auto historyIt = state.missionParetoHistory.find(missionId);
    if (historyIt == state.missionParetoHistory.end() || historyIt->second.empty()) {
        throw std::invalid_argument("rescoreParetoFront: no captured front for mission '" +
                                    *command.missionId + "'");
    }
    // Re-scores whichever planning pass is CURRENT for this mission (the most recently
    // captured entry) -- a mission that has since been replanned should have live re-scoring
    // apply to its latest front, not a stale pass 1.
    const orca::ParetoFront& front = historyIt->second.back().front;
    const orca::OptimizationProfile profile = resolveNamedProfile(*command.profile);
    const std::optional<orca::RecommendedPlan> recommendation =
        orca::selectRecommendedPlan(front, profile);
    if (!recommendation) {
        throw std::invalid_argument("rescoreParetoFront: front has no feasible solution to recommend");
    }
    state.missionRescoring.insert_or_assign(
        missionId, orca::SimulationState::ParetoRescore{
                      .profileName = *command.profile,
                      .recommendedPlanId = front.solutions[recommendation->planIndex].id(),
                  });
}

/// Phase 12 brief §16: Algorithm Lab must actually RUN an algorithm on demand, not only
/// passively display telemetry captured at the last real dispatch (Phase 11's
/// `recordComparisonAlgorithmRuns()`, `EventDispatcher.cpp`). Reuses the exact same real
/// functions that decision path calls -- `findEarliestArrival`/`findEarliestArrivalAStar`/
/// `findParetoRoutes`/`evaluateGlobalReplan()` -- with no new algorithm code, only a new
/// entry point for a caller (the frontend, on a button click) to request one right now. The
/// run is appended to `state.algorithmRuns` (the same bounded ring buffer, Phase 11's
/// `kMaxAlgorithmRuns`), so it shows up in the very next snapshot exactly like a
/// dispatch-triggered one -- Algorithm Lab does not need to know which kind produced a run.
void applyRunAlgorithmCommand(orca::SimulationEngine& engine, const orca::io::Command& command) {
    orca::SimulationState& state = engine.stateMutable();
    const std::string& algo = *command.algorithm;
    const orca::Seconds now = engine.currentTimeSec();

    if (algo == "HUNGARIAN") {
        const std::vector<orca::VehicleId> availableIds = state.availableVehicleIds();
        const std::vector<orca::EmergencyId> pendingIds = state.pendingEmergencyIds();
        if (availableIds.empty() || pendingIds.empty()) {
            throw std::invalid_argument(
                "runAlgorithm: HUNGARIAN needs at least one available vehicle and one pending "
                "emergency to run against (none right now)");
        }
        std::vector<orca::RescueVehicle> available;
        available.reserve(availableIds.size());
        for (const orca::VehicleId id : availableIds) available.push_back(*state.findVehicle(id));
        std::vector<orca::Emergency> pending;
        pending.reserve(pendingIds.size());
        for (const orca::EmergencyId id : pendingIds) pending.push_back(*state.findEmergency(id));

        orca::PlanId scratchPlanId(1);  // never committed -- this run is read-only telemetry
        const orca::AssignmentResult result = orca::evaluateGlobalReplan(
            engine.graphForDispatch(), engine.modelForDispatch(), available, pending, now,
            orca::OptimizationProfile::balanced(), scratchPlanId);
        state.recordAlgorithmRun(orca::AlgorithmRunRecord{
            .algorithm = orca::AlgorithmKind::Hungarian,
            .timestampSec = now,
            .found = result.assignedVehicleCount > 0,
            .assignment = result.metrics,
        });
        return;
    }

    if (algo != "DIJKSTRA" && algo != "ASTAR" && algo != "PARETO") {
        throw std::invalid_argument("runAlgorithm: unknown algorithm '" + algo + "'");
    }

    const orca::RescueVehicle* vehicle = nullptr;
    if (command.vehicleId) {
        vehicle = state.findVehicle(orca::VehicleId(parseIdSuffix(*command.vehicleId)));
        if (vehicle == nullptr) {
            throw std::invalid_argument("runAlgorithm: unknown vehicleId '" + *command.vehicleId + "'");
        }
    } else {
        const std::vector<orca::VehicleId> availableIds = state.availableVehicleIds();
        if (!availableIds.empty()) {
            vehicle = state.findVehicle(availableIds.front());
        } else if (!state.vehicles.empty()) {
            vehicle = &state.vehicles.begin()->second;
        }
    }
    if (vehicle == nullptr) {
        throw std::invalid_argument("runAlgorithm: no vehicle available to run against");
    }

    const orca::Emergency* emergency = nullptr;
    if (command.emergencyId) {
        emergency = state.findEmergency(orca::EmergencyId(parseIdSuffix(*command.emergencyId)));
        if (emergency == nullptr) {
            throw std::invalid_argument("runAlgorithm: unknown emergencyId '" + *command.emergencyId + "'");
        }
    } else {
        const std::vector<orca::EmergencyId> pendingIds = state.pendingEmergencyIds();
        if (!pendingIds.empty()) emergency = state.findEmergency(pendingIds.front());
    }

    // With no real emergency (nothing has failed yet), still demonstrate the algorithm
    // against a real target -- the first satellite in the scenario -- rather than refusing
    // to run at all. Honestly reflected in the returned record: `found`/`arrivalTimeSec` are
    // whatever the real search actually returns for that pair, never fabricated.
    std::optional<orca::SatelliteId> targetSatId;
    if (emergency != nullptr) {
        targetSatId = emergency->subject();
    } else if (!state.satellites.empty()) {
        targetSatId = state.satellites.begin()->second.id();
    }
    if (!targetSatId) {
        throw std::invalid_argument("runAlgorithm: no satellite available to target");
    }

    const orca::EntityNodeIndex index = orca::buildEntityNodeIndex(engine.graphForDispatch());
    const auto vehicleNodeIt = index.vehicleNodes.find(vehicle->id().value());
    const auto targetNodeIt = index.satelliteNodes.find(targetSatId->value());
    if (vehicleNodeIt == index.vehicleNodes.end() || targetNodeIt == index.satelliteNodes.end()) {
        throw std::invalid_argument("runAlgorithm: no graph node for the selected vehicle/target");
    }
    const orca::NodeId source = vehicleNodeIt->second;
    const orca::NodeId target = targetNodeIt->second;

    orca::SearchConstraints constraints;
    constraints.vehicleDeltaVBudgetKms = vehicle->deltaVRemainingKms();
    constraints.maxWaitSec = 1.0e13;
    constraints.latestArrivalSec = emergency != nullptr ? emergency->deadlineSec() : 1.0e13;

    if (algo == "DIJKSTRA") {
        const orca::PathResult result = orca::findEarliestArrival(
            engine.graphForDispatch(), engine.modelForDispatch(), source, target, now, constraints);
        state.recordAlgorithmRun(orca::AlgorithmRunRecord{
            .algorithm = orca::AlgorithmKind::Dijkstra,
            .timestampSec = now,
            .found = result.isFound(),
            .arrivalTimeSec = result.isFound() ? std::optional(result.arrivalTimeSec()) : std::nullopt,
            .search = result.metrics(),
        });
    } else if (algo == "ASTAR") {
        const orca::PathResult result = orca::findEarliestArrivalAStar(
            engine.graphForDispatch(), engine.modelForDispatch(), source, target, now, constraints);
        state.recordAlgorithmRun(orca::AlgorithmRunRecord{
            .algorithm = orca::AlgorithmKind::AStar,
            .timestampSec = now,
            .found = result.isFound(),
            .arrivalTimeSec = result.isFound() ? std::optional(result.arrivalTimeSec()) : std::nullopt,
            .search = result.metrics(),
        });
    } else {  // PARETO
        const orca::ParetoSearchResult result = orca::findParetoRoutes(
            engine.graphForDispatch(), engine.modelForDispatch(), source, target, now, constraints);
        state.recordAlgorithmRun(orca::AlgorithmRunRecord{
            .algorithm = orca::AlgorithmKind::Pareto,
            .timestampSec = now,
            .found = !result.isEmpty(),
            .arrivalTimeSec =
                !result.isEmpty() ? std::optional(result.solutions.front().arrivalTimeSec) : std::nullopt,
            .pareto = result.metrics,
        });
    }
}

/// The command loop itself -- shared by `serve()` (the canonical demo scenario) and
/// `serveGenerated()` (Phase 11's `--serve <family>:<size>:<seed>`, brief §32-35's
/// visualization-scaling need: a real, large snapshot to measure marker/orbit-ring
/// rendering against, not a fabricated one). Identical protocol handling either way --
/// only how `engine` was constructed differs between the two callers.
/// Re-applies a scenario's failure schedule. `SimulationEngine::reset()` replaces the event
/// queue wholesale, which is correct -- a fresh run must not inherit the previous run's pending
/// events -- but it also discards the failures scheduled once at boot. Without re-arming, a
/// reset scenario is permanently inert: the simulation advances and nothing ever fails again.
///
/// This affected BOTH serve paths (the demo's scripted failures and a generated scenario's
/// seeded ones) and was found by pressing Reset and watching six simulated hours produce zero
/// emergencies. Re-arming from the same schedule is also what makes reset *reproducible*, which
/// is the property the benchmark and replay paths depend on.
using RearmSchedule = std::function<void(orca::SimulationEngine&)>;

void runServeLoop(orca::SimulationEngine& engine, const RearmSchedule& rearm) {
    emitLine(orca::io::serializeReadyMessage(
        kVersion, kSchemaVersion,
        {"start", "pause", "resume", "reset", "step", "advance", "setSpeed", "getSnapshot",
         "rescoreParetoFront", "runAlgorithm"}));

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) {
            continue;  // API_CONTRACT.md §1's framing tolerates a stray blank line
        }

        orca::io::JsonValue parsed;
        try {
            parsed = orca::io::parseJson(line);
        } catch (const orca::io::JsonError& e) {
            emitLine(orca::io::serializeErrorMessage("MALFORMED_JSON", e.what()));
            continue;
        }

        orca::io::Command command{.name = orca::io::CommandName::Start};
        try {
            command = orca::io::parseCommand(parsed);
        } catch (const orca::io::CommandError& e) {
            emitLine(orca::io::serializeErrorMessage(e.code(), e.what()));
            continue;
        }

        if (command.name == orca::io::CommandName::Shutdown) {
            break;
        }

        try {
            switch (command.name) {
                case orca::io::CommandName::Start: engine.start(); break;
                case orca::io::CommandName::Pause: engine.pause(); break;
                case orca::io::CommandName::Resume: engine.resume(); break;
                case orca::io::CommandName::Reset:
                    engine.reset();
                    rearm(engine);
                    break;
                case orca::io::CommandName::Step: engine.step(); break;
                case orca::io::CommandName::Advance: engine.advance(*command.seconds); break;
                case orca::io::CommandName::SetSpeed: engine.setSpeed(*command.multiplier); break;
                case orca::io::CommandName::GetSnapshot: break;  // no mutation; fall through to snapshot
                case orca::io::CommandName::Shutdown: break;     // unreachable, handled above
                case orca::io::CommandName::RescoreParetoFront: applyRescoreCommand(engine, command); break;
                case orca::io::CommandName::RunAlgorithm: applyRunAlgorithmCommand(engine, command); break;
            }
        } catch (const std::exception& e) {
            emitLine(orca::io::serializeErrorMessage("INTERNAL_ERROR", e.what(), command.reqId));
            continue;
        }

        const orca::io::SimulationSnapshot snapshot = orca::io::buildSnapshot(engine);
        emitLine(orca::io::serializeSnapshotMessage(snapshot, command.reqId));
    }
}

void serve() {
    orca::SimulationEngine engine(orca::buildDemoScenario());

    // The canonical demo scenario's own scripted timeline (`DemoScenario.hpp`) -- pre-loaded
    // before `ready` is announced, so the frontend controlling this process via `start`/
    // `advance` sees the exact Phase 8 end-to-end narrative without needing a scenario-file
    // loader, which does not exist (`DATA_MODEL.md` §8's correction).
    const RearmSchedule rearm = [](orca::SimulationEngine& e) {
        orca::scheduleScriptedFailures(e.eventQueueMutable(), orca::demoScenarioScriptedFailures());
        e.scheduleEvent(orca::demoScenarioDebrisAlertEvent());
    };
    rearm(engine);

    runServeLoop(engine, rearm);
}

/// Parses `<family>:<size>[:<seed>]` (the shape `--serve <spec>`, `--visualScenario <spec>`
/// and `--bench` all share the same family names for) and builds the real generated
/// `Scenario` -- one parsing path, reused by every CLI mode that accepts this spec shape
/// rather than duplicated per mode.
orca::Scenario parseGeneratedScenarioSpec(const std::string& spec) {
    const std::size_t firstColon = spec.find(':');
    const std::size_t secondColon = spec.find(':', firstColon == std::string::npos ? 0 : firstColon + 1);
    if (firstColon == std::string::npos) {
        throw std::invalid_argument("expected <family>:<size>[:<seed>], e.g. dense:500");
    }
    const std::string familyName = spec.substr(0, firstColon);
    const std::string sizeText = secondColon == std::string::npos ? spec.substr(firstColon + 1)
                                                                   : spec.substr(firstColon + 1, secondColon - firstColon - 1);
    const orca::Seed seed = secondColon == std::string::npos
                                 ? orca::Seed{1}
                                 : static_cast<orca::Seed>(std::stoull(spec.substr(secondColon + 1)));
    const int size = std::stoi(sizeText);

    const std::vector<std::pair<std::string, orca::ScenarioGeneratorConfig (*)(int)>> families = {
        {"sparse", orca::scenario_family::sparse},           {"normal", orca::scenario_family::normal},
        {"dense", orca::scenario_family::dense},              {"multiEmergency", orca::scenario_family::multiEmergency},
        {"fuelConstrained", orca::scenario_family::fuelConstrained},
        {"highDebris", orca::scenario_family::highDebris},   {"tradeoffRich", orca::scenario_family::tradeoffRich},
        {"stress", orca::scenario_family::stress},
        {"visualShell", orca::scenario_family::visualShell},
    };
    const auto it = std::find_if(families.begin(), families.end(),
                                 [&](const auto& p) { return p.first == familyName; });
    if (it == families.end()) {
        throw std::invalid_argument("unknown scenario family '" + familyName + "'");
    }

    const orca::ScenarioGeneratorConfig config = it->second(size);
    return orca::generateScenario(familyName + "-" + sizeText, seed, config);
}

/// How many satellites fail, as a fraction of the population, when a generated scenario is
/// served without an explicit failure count. One in twenty gives a mission-control view enough
/// simultaneous emergencies to show fleet contention and prioritisation without saturating the
/// rescue fleet on the first tick.
constexpr int kDefaultFailureDivisor = 20;
constexpr int kMinimumGeneratedFailures = 3;

/// Over how long the generated failures are spread, in seconds. Six hours: long enough that
/// emergencies arrive progressively rather than all at t=0, short enough that a session sees
/// several without fast-forwarding for hours.
constexpr double kGeneratedFailureWindowSec = 21600.0;

/// Survival window given to each generated emergency. A modelling simplification, stated as one
/// in `EmergencyGenerator.hpp` -- not a claim that real survival windows are uniform.
constexpr double kGeneratedSurvivalWindowSec = 5400.0;

/// `--serve <family>:<size>[:<seed>[:<failures>]]` -- boots a real, deterministically generated
/// scenario (`ScenarioGenerator.hpp`'s own families) instead of the small fixed demo one.
///
/// ## Why this now schedules failures
///
/// Earlier this deliberately scheduled none, on the correct grounds that the DEMO scenario's
/// scripted failures name demo-only entity IDs that do not exist in a generated scenario. But
/// the conclusion drawn from that was too strong: it left every generated scenario with a
/// population and no rescue narrative at all -- hundreds of satellites, all NOMINAL, forever.
/// A mission-control interface booted that way can only ever show an orbital environment, never
/// the rescue loop that is the product's entire purpose.
///
/// `scheduleRandomFailures()` has no such coupling: it DRAWS from the state it is given, so it
/// works against any scenario. It is seeded from the scenario's own seed via the sanctioned
/// `RandomStream::Failures` stream, so the same spec always produces the same failures at the
/// same times -- the determinism the benchmark and replay paths both depend on.
void serveGenerated(const std::string& spec, int explicitFailureCount) {
    const orca::Scenario scenario = parseGeneratedScenarioSpec(spec);
    orca::SimulationEngine engine(scenario);

    const int population = static_cast<int>(engine.state().satellites.size());
    int failureCount = explicitFailureCount >= 0
                           ? explicitFailureCount
                           : std::max(kMinimumGeneratedFailures, population / kDefaultFailureDivisor);
    // Never ask for more failures than there are satellites to fail: `scheduleRandomFailures`
    // draws without replacement and throws rather than repeating an object.
    failureCount = std::min(failureCount, population);

    // Captured by value so a reset re-draws from the SAME seeded stream and reproduces the
    // identical failure schedule, rather than continuing a generator that has moved on.
    const RearmSchedule rearm = [failureCount](orca::SimulationEngine& e) {
        if (failureCount <= 0) return;
        orca::RandomFailureConfig config;
        config.failureCount = failureCount;
        config.durationSec = kGeneratedFailureWindowSec;
        config.survivalWindowSec = kGeneratedSurvivalWindowSec;
        orca::RandomGenerator rng = e.randomStream(orca::RandomStream::Failures);
        orca::scheduleRandomFailures(e.eventQueueMutable(), e.state(), config, rng);
    };
    rearm(engine);

    runServeLoop(engine, rearm);
}

/// `--visualScenario <family>:<size>[:<seed>[:<timeSec>]]` -- Phase 12 brief §5/§19: a
/// large-scale VISUAL constellation (thousands of objects), deliberately NOT run through
/// `SimulationEngine`/`GraphBuilder` (see `cpp/io/VisualConstellation.hpp`'s own file
/// comment for the real O(n^2) graph-cost reason). One-shot: generate, project at the
/// requested time, print one JSON object, exit -- no persistent protocol session, since a
/// large constellation's "movement" only needs a fresh position query, not a running
/// simulation clock.
void runVisualScenario(const std::string& spec, orca::Seconds timeSec) {
    const auto genStart = std::chrono::steady_clock::now();
    const orca::Scenario scenario = parseGeneratedScenarioSpec(spec);
    const auto genEnd = std::chrono::steady_clock::now();
    const double generationMs = std::chrono::duration<double, std::milli>(genEnd - genStart).count();

    emitLine(orca::io::serializeVisualConstellation(scenario, timeSec, generationMs));
}

/// The default `--bench` suite -- Phase 11 brief §22-31, a real, bounded, documented
/// subset of the brief's full requested matrix, sized to finish in a few minutes rather
/// than the much larger grid the brief describes. **Found empirically, not assumed**: an
/// early, larger version of this suite (all 8 sizes up to 500, matrix sizes up to 50) was
/// actually run once to completion/near-completion during development, and assignment
/// candidate generation at `matrixSize=50` was the genuine bottleneck -- `O(vehicles x
/// emergencies)` Pareto searches inside `generateAllCandidateFronts()`, exactly the
/// candidate-generation-dominates finding Phase 6 already reported, now confirmed at
/// larger scale (`docs/FINAL_BENCHMARK_REPORT.md` states this explicitly as a real,
/// measured limitation, not swept under the default suite's smaller scope).
void runBenchmarkSuite() {
    using namespace orca::analytics;

    const std::vector<std::pair<std::string, orca::ScenarioGeneratorConfig (*)(int)>> families = {
        {"sparse", orca::scenario_family::sparse},
        {"normal", orca::scenario_family::normal},
        {"dense", orca::scenario_family::dense},
        {"fuelConstrained", orca::scenario_family::fuelConstrained},
        {"highDebris", orca::scenario_family::highDebris},
        {"tradeoffRich", orca::scenario_family::tradeoffRich},
    };
    const std::vector<int> sizes = {25, 50, 100};
    const std::vector<orca::Seed> seeds = {1, 2, 3};

    const std::string searchPath = "benchmarks/results/phase11_search_comparison.csv";
    const std::string assignmentPath = "benchmarks/results/phase11_assignment_comparison.csv";

    long searchRows = 0;
    long assignmentRows = 0;

    for (const auto& [familyName, familyFn] : families) {
        for (const int size : sizes) {
            for (const orca::Seed seed : seeds) {
                const SearchBenchmarkConfig config{
                    .familyName = familyName,
                    .scenarioConfig = familyFn(size),
                    .seed = seed,
                    .warmupRuns = 1,
                    .repetitions = 5,
                };
                const std::vector<BenchmarkRow> rows = runSearchComparisonBenchmark(config);
                appendBenchmarkCsv(searchPath, rows);
                searchRows += static_cast<long>(rows.size());
                std::fprintf(stderr, "[bench] search  family=%-16s size=%-4d seed=%llu (%zu rows)\n",
                            familyName.c_str(), size, static_cast<unsigned long long>(seed), rows.size());
            }
        }
    }

    const std::vector<int> matrixSizes = {2, 5, 10, 20};
    for (const int n : matrixSizes) {
        for (const orca::Seed seed : seeds) {
            const AssignmentBenchmarkConfig config{
                .familyName = "normal", .matrixSize = n, .seed = seed, .warmupRuns = 1, .repetitions = 5,
            };
            const std::vector<BenchmarkRow> rows = runAssignmentBenchmark(config);
            appendBenchmarkCsv(assignmentPath, rows);
            assignmentRows += static_cast<long>(rows.size());
            std::fprintf(stderr, "[bench] assign  matrixSize=%-4d seed=%llu (%zu rows)\n", n,
                        static_cast<unsigned long long>(seed), rows.size());
        }
    }

    std::printf("Benchmark suite complete.\n  %s (%ld rows)\n  %s (%ld rows)\n", searchPath.c_str(),
               searchRows, assignmentPath.c_str(), assignmentRows);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc > 1 && (std::strcmp(argv[1], "--version") == 0 || std::strcmp(argv[1], "-v") == 0)) {
        printVersion();
        return 0;
    }

    if (argc > 1 && std::strcmp(argv[1], "--serve") == 0) {
        if (argc > 2) {
            // The spec grammar gained an optional fourth field: <family>:<size>[:<seed>[:<failures>]].
            // Splitting it off here rather than inside `parseGeneratedScenarioSpec` keeps that
            // function's contract identical for `--visualScenario` and `--bench`, which share it
            // and have no notion of scheduled failures.
            std::string spec(argv[2]);
            int failureCount = -1;  // -1 means "derive from population"
            const std::size_t first = spec.find(':');
            const std::size_t second = first == std::string::npos ? std::string::npos
                                                                  : spec.find(':', first + 1);
            const std::size_t third = second == std::string::npos ? std::string::npos
                                                                  : spec.find(':', second + 1);
            if (third != std::string::npos) {
                failureCount = std::stoi(spec.substr(third + 1));
                spec = spec.substr(0, third);
            }
            serveGenerated(spec, failureCount);
        } else {
            serve();
        }
        return 0;
    }

    if (argc > 1 && std::strcmp(argv[1], "--bench") == 0) {
        runBenchmarkSuite();
        return 0;
    }

    if (argc > 1 && std::strcmp(argv[1], "--visualScenario") == 0) {
        if (argc <= 2) {
            std::fprintf(stderr, "orca_engine: --visualScenario <family>:<size>[:<seed>] --time <sec>\n");
            return 2;
        }
        double timeSec = 0.0;
        if (argc > 4 && std::strcmp(argv[3], "--time") == 0) {
            timeSec = std::stod(argv[4]);
        }
        runVisualScenario(argv[2], timeSec);
        return 0;
    }

    if (argc > 1) {
        std::fprintf(stderr,
                     "orca_engine: unknown argument '%s'\n"
                     "Supported: --version, --serve, --bench, --visualScenario. See\n"
                     "docs/INTEGRATION_ARCHITECTURE.md, docs/FINAL_BENCHMARK_REPORT.md.\n",
                     argv[1]);
        return 2;
    }

    printStatus();
    return 0;
}
