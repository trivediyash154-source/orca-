# ORCA tests.
#
# NOT VERIFIED: CMake is not installed on this machine (docs/REPOSITORY_AUDIT.md §3).
# The verified path is scripts/run_tests.sh, which builds and runs the same test suite
# (588 tests as of Phase 8) with the system compiler.
#
# ---------------------------------------------------------------------------
# On GoogleTest
# ---------------------------------------------------------------------------
#
# Phase 0 specified GoogleTest via FetchContent. That remains the target, and the block
# below is how it will be wired. It is commented out rather than active because
# FetchContent requires network at configure time, and enabling it now would mean the
# first person to run CMake gets a download rather than a build.
#
# Phase 1's tests use the minimal harness in tests/TestFramework.hpp, which needs no
# dependency at all. Migration is mechanical (see that file's header) and the test bodies
# do not change -- only the assertion macro names.
#
#   include(FetchContent)
#   FetchContent_Declare(googletest
#       GIT_REPOSITORY https://github.com/google/googletest.git
#       GIT_TAG        v1.15.2          # pinned; never a moving branch
#   )
#   set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
#   FetchContent_MakeAvailable(googletest)
#   target_link_libraries(orca_tests PRIVATE GTest::gtest_main)

add_executable(orca_tests
    test_main.cpp

    analytics/assignment_benchmark_test.cpp
    analytics/benchmark_runner_test.cpp
    analytics/csv_writer_test.cpp
    analytics/statistics_test.cpp

    core/math_test.cpp
    core/random_test.cpp
    core/types_test.cpp
    core/vector_test.cpp

    domain/emergency_test.cpp
    domain/mission_test.cpp
    domain/orbit_test.cpp
    domain/satellite_test.cpp
    domain/vehicle_test.cpp

    transfer/feasibility_test.cpp
    transfer/fifo_test.cpp
    transfer/hohmann_test.cpp
    transfer/phasing_test.cpp
    transfer/planechange_test.cpp
    transfer/transfer_model_test.cpp

    graph/graph_builder_test.cpp
    graph/graph_structure_test.cpp

    algorithms/astar_heuristic_test.cpp
    algorithms/astar_oracle_test.cpp
    algorithms/astar_test.cpp
    algorithms/astar_time_dependence_test.cpp
    algorithms/dijkstra_oracle_test.cpp
    algorithms/dijkstra_test.cpp
    algorithms/dijkstra_time_dependence_test.cpp
    algorithms/dominance_test.cpp
    algorithms/greedy_assignment_test.cpp
    algorithms/heuristic_admissibility_test.cpp
    algorithms/heuristic_consistency_test.cpp
    algorithms/hungarian_oracle_test.cpp
    algorithms/hungarian_test.cpp
    algorithms/pareto_search_oracle_test.cpp
    algorithms/pareto_search_test.cpp
    algorithms/pareto_search_time_dependence_test.cpp

    optimization/assignment_cost_test.cpp
    optimization/assignment_matrix_test.cpp
    optimization/emergency_priority_test.cpp
    optimization/mission_scorer_test.cpp
    optimization/objective_normalizer_test.cpp
    optimization/optimization_profile_test.cpp
    optimization/pareto_front_test.cpp

    integration/assignment_scenarios_test.cpp
    integration/pareto_assignment_integration_test.cpp

    io/command_protocol_test.cpp
    io/json_parser_test.cpp
    io/json_value_test.cpp
    io/view_projection_test.cpp

    planning/dynamic_replanner_test.cpp
    planning/global_replanner_test.cpp
    planning/mission_validity_test.cpp
    planning/preemption_test.cpp
    planning/replan_commitment_test.cpp

    simulation/emergency_generator_test.cpp
    simulation/end_to_end_demo_test.cpp
    simulation/event_queue_test.cpp
    simulation/mission_executor_test.cpp
    simulation/scenario_generator_test.cpp
    simulation/simulation_clock_test.cpp
    simulation/simulation_engine_test.cpp
    simulation/simulation_state_test.cpp
)

target_link_libraries(orca_tests PRIVATE orca_core orca_options)
target_include_directories(orca_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Assertions must stay live even in a Release test build: the domain uses assert() for
# internal invariants (Validation.hpp), and running tests with NDEBUG defined would skip
# exactly the checks the tests exist to exercise. -UNDEBUG undefines it after the global
# Release flags have added it.
target_compile_options(orca_tests PRIVATE $<$<CONFIG:Release>:-UNDEBUG>)

add_test(NAME orca_tests COMMAND orca_tests)

# Phase 4 added: algorithms/astar_*.cpp, algorithms/heuristic_*.cpp (above).
# Phase 5 added: algorithms/dominance_test.cpp, algorithms/pareto_search_*.cpp, and the
# optimization/ test files (above).
# Phase 6 added: algorithms/{hungarian,greedy_assignment}*.cpp,
# optimization/{assignment_cost,assignment_matrix,emergency_priority}_test.cpp, and the
# integration/ suite (multi-emergency scenarios A-G, Pareto-integration checks, and the
# weight-change regression test).
#
# Phase 7 added: planning/{mission_validity,dynamic_replanner,global_replanner,
# preemption}_test.cpp -- mission validity, replan scenarios A-J, hysteresis/idempotence/
# determinism, mission-progress/deadline/fuel/target/vehicle/comm-loss coverage, the
# fleet-wide global replanner, and preemption policy.
#
# Phase 8 added: simulation/{simulation_clock,event_queue,simulation_state,
# emergency_generator,mission_executor,simulation_engine,end_to_end_demo}_test.cpp --
# clock start/pause/resume/step/reset, deterministic same-timestamp event ordering,
# world-state single-source-of-truth, scripted/seeded emergency generation, per-leg
# resource consumption, engine-level determinism/replay/large-dt/event-burst/long-run/
# replan-storm, and one hand-verified end-to-end scenario (failure -> global replan ->
# debris reroute -> rescue complete).
# Test inventory and exit criteria: docs/TEST_PLAN.md
