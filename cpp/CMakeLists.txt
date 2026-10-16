# ORCA engine build.
#
# NOT VERIFIED: CMake is not installed on this machine (docs/REPOSITORY_AUDIT.md §3), so
# this file has never been configured. It is written to be correct but must be treated as
# unverified until `cmake -B build` succeeds for the first time.
#
# The build that IS verified is scripts/build_engine.sh, which compiles the same sources
# with the same warning flags using the system compiler directly.
#
# Target structure (ARCHITECTURE.md §8):
#
#   orca_core    STATIC   everything except the entry point
#   orca_engine  EXE      links orca_core, adds app/
#
# Splitting orca_core as a static library is what lets tests link the engine's real code
# without also linking main().

# ---------------------------------------------------------------------------
# orca_core
# ---------------------------------------------------------------------------
#
# Sources are listed explicitly rather than globbed: a glob does not re-run when a file is
# added, so a new source silently fails to build until someone reconfigures by hand.

add_library(orca_core STATIC
    core/RandomGenerator.cpp

    domain/DebrisObject.cpp
    domain/Emergency.cpp
    domain/Mission.cpp
    domain/MissionPlan.cpp
    domain/Orbit.cpp
    domain/RescueVehicle.cpp
    domain/Satellite.cpp
    domain/Scenario.cpp
    domain/SimplifiedOrbitalModel.cpp
    domain/Subsystems.cpp

    transfer/HohmannTransfer.cpp
    transfer/PhasingModel.cpp
    transfer/PlaneChange.cpp
    transfer/TransferFeasibility.cpp
    transfer/TransferModel.cpp
    transfer/TransferRiskModel.cpp

    graph/GraphBuilder.cpp
    graph/MissionGraph.cpp

    algorithms/AStar.cpp
    algorithms/AStarHeuristic.cpp
    algorithms/Dijkstra.cpp
    algorithms/Dominance.cpp
    algorithms/GreedyAssignment.cpp
    algorithms/Hungarian.cpp
    algorithms/ParetoSearch.cpp
    algorithms/PathResult.cpp

    optimization/AssignmentCost.cpp
    optimization/AssignmentMatrix.cpp
    optimization/AssignmentResult.cpp
    optimization/CandidateGeneration.cpp
    optimization/EmergencyPriority.cpp
    optimization/MissionScorer.cpp
    optimization/ObjectiveNormalizer.cpp
    optimization/OptimizationProfile.cpp
    optimization/ParetoFront.cpp

    planning/DynamicReplanner.cpp
    planning/GlobalReplanner.cpp
    planning/MissionValidity.cpp
    planning/ReplanDecision.cpp

    simulation/DemoScenario.cpp
    simulation/EmergencyGenerator.cpp
    simulation/EventDispatcher.cpp
    simulation/EventQueue.cpp
    simulation/MissionExecutor.cpp
    simulation/ScenarioGenerator.cpp
    simulation/SimulationClock.cpp
    simulation/SimulationEngine.cpp
    simulation/SimulationState.cpp

    io/CommandProtocol.cpp
    io/JsonParser.cpp
    io/JsonSerializer.cpp
    io/JsonValue.cpp
    io/ViewProjection.cpp

    analytics/AssignmentBenchmark.cpp
    analytics/BenchmarkRunner.cpp
    analytics/CsvWriter.cpp
    analytics/Statistics.cpp
)

# PUBLIC so that tests and the executable can include "core/..." and "domain/..." relative
# to cpp/.
target_include_directories(orca_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(orca_core PUBLIC orca_options)

# ---------------------------------------------------------------------------
# orca_engine
# ---------------------------------------------------------------------------

add_executable(orca_engine
    app/main.cpp
)

target_link_libraries(orca_engine PRIVATE orca_core orca_options)

# Phase 4 added: algorithms/AStar.cpp, algorithms/AStarHeuristic.cpp (above).
# Phase 5 added: algorithms/Dominance.cpp, algorithms/ParetoSearch.cpp, and the
# optimization/ library (above) -- Dominance lives in algorithms/, not optimization/, per
# ParetoSearch.hpp's file comment (the same dependency-direction reasoning that moved
# ObjectiveVector out of optimization/ in Phase 1).
# Phase 6 added: algorithms/Hungarian.cpp, algorithms/GreedyAssignment.cpp (domain-agnostic
# solvers, same directory-placement reasoning as Dominance -- Hungarian.hpp's file comment)
# and optimization/{EmergencyPriority,CandidateGeneration,AssignmentCost,AssignmentMatrix,
# AssignmentResult}.cpp (rescue assignment across multiple vehicles/emergencies).
#
# Phase 7 added: planning/{MissionValidity,DynamicReplanner,GlobalReplanner,
# ReplanDecision}.cpp -- dynamic replanning, orchestrating the existing Phase 5/6 search
# and assignment pipeline rather than duplicating it (DynamicReplanner.hpp's file comment).
# MissionChange.hpp, ReplanReason.hpp, ReplanPolicy.hpp are header-only (plain data/enums).
#
# Phase 8 added: simulation/{EventQueue,SimulationClock,SimulationState,EmergencyGenerator,
# MissionExecutor,SimulationEngine,EventDispatcher}.cpp -- the continuous tick loop, event
# scheduling and dispatch, and mission execution that make DynamicReplanner's first real
# caller (SIMULATION_ENGINE.md's file comment). Event.hpp is header-only.
#
# Phase 9 added: io/{JsonValue,JsonParser,JsonSerializer,CommandProtocol,ViewProjection}.cpp
# -- the snapshot/serialization boundary and NDJSON command protocol
# (SimulationView.hpp/CommandProtocol.hpp are header-only where pure data/enums). app/main.cpp
# gained `--serve`, the protocol loop. simulation/DemoScenario.cpp (the canonical demo
# scenario, extracted from Phase 8's end-to-end test so both share one definition) also
# landed under simulation/ this phase.
#
# Phase 11 added: analytics/{Statistics,BenchmarkRunner,AssignmentBenchmark,CsvWriter}.cpp
# -- benchmark statistics, Dijkstra/A*/Pareto comparison, Greedy/Hungarian assignment
# comparison, and CSV export (BenchmarkTypes.hpp is header-only). simulation/
# ScenarioGenerator.cpp -- deterministic N-object scenario generation, also this phase.
