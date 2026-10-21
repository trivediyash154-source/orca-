// E3: Hungarian versus greedy fleet assignment, one run per distinct cost matrix.
// Mirrors AssignmentBenchmark.cpp (normal family, square fleet, one emergency per
// satellite) but varies the seed and the survival window, and dumps every matrix cell so
// coverage can be compared with the maximum possible matching.
// Usage: e3_assignment <seeds> <instances.csv> <cells.csv> <size> [size ...]
#include <cstdlib>

#include "common.hpp"
#include "optimization/AssignmentCost.hpp"
#include "optimization/AssignmentMatrix.hpp"
#include "optimization/AssignmentResult.hpp"
#include "optimization/OptimizationProfile.hpp"

using namespace orca;
using namespace exp_common;

int main(int argc, char** argv) {
    const int seeds = std::atoi(argv[1]);
    FILE* inst = std::fopen(argv[2], "w");
    FILE* cells = std::fopen(argv[3], "w");
    std::vector<int> sizes;
    for (int i = 4; i < argc; ++i) sizes.push_back(std::atoi(argv[i]));
    const std::vector<std::pair<std::string, double>> windows = {
        {"none", 5.0e7}, {"48h", 48.0 * 3600.0}, {"24h", 24.0 * 3600.0}};

    std::fprintf(inst, "iid,size,seed,window,feasible_pairs,greedy_assigned,greedy_cost,"
                       "hung_assigned,hung_cost,greedy_ms,hung_ms\n");
    std::fprintf(cells, "iid,row,col,feasible,cost,greedy,hung\n");
    int iid = 0;
    for (int size : sizes) {
        for (int seed = 1; seed <= seeds; ++seed) {
            ScenarioGeneratorConfig cfg = scenario_family::normal(size);
            cfg.vehicleCount = size;
            const Scenario sc = generateScenario("normal", static_cast<Seed>(seed), cfg);
            const TransferModel model;
            const MissionGraph graph = GraphBuilder::build(sc, model).graph;
            const OptimizationProfile profile = OptimizationProfile::balanced();
            std::vector<VehicleId> vehicleIds;
            for (const RescueVehicle& v : sc.vehicles()) vehicleIds.push_back(v.id());
            for (const auto& [wname, window] : windows) {
                std::vector<Emergency> emergencies;
                for (std::size_t i = 0; i < sc.satellites().size(); ++i) {
                    emergencies.emplace_back(EmergencyId(static_cast<std::uint32_t>(i + 1)),
                                             sc.satellites()[i].id(),
                                             EmergencyKind::PropulsionFailure, Severity::High,
                                             0.0, window);
                }
                std::vector<EmergencyId> emergencyIds;
                for (const Emergency& e : emergencies) emergencyIds.push_back(e.id());
                PlanId nextPlanId(1);
                const auto fronts = generateAllCandidateFronts(graph, model, sc.vehicles(),
                                                               emergencies, 0.0, nextPlanId);
                const auto scored = scoreCandidates(fronts, profile);
                const auto costs = buildAssignmentCosts(fronts, scored, emergencies, 0.0, profile,
                                                        defaultPriorityWeights());
                const AssignmentMatrix matrix = buildAssignmentMatrix(costs, vehicleIds, emergencyIds);
                const AssignmentResult g = solveAssignmentGreedy(matrix, fronts, scored);
                const AssignmentResult h = solveAssignmentHungarian(matrix, fronts, scored);

                int feasiblePairs = 0;
                for (std::size_t r = 0; r < matrix.vehicleCount; ++r) {
                    for (std::size_t c = 0; c < matrix.emergencyCount; ++c) {
                        const bool feas = matrix.feasibleReal[r][c];
                        feasiblePairs += feas;
                        int inG = 0, inH = 0;
                        for (const auto& a : g.assignments)
                            inG |= (a.vehicle == matrix.vehicleIds[r] && a.emergency == matrix.emergencyIds[c]);
                        for (const auto& a : h.assignments)
                            inH |= (a.vehicle == matrix.vehicleIds[r] && a.emergency == matrix.emergencyIds[c]);
                        std::fprintf(cells, "%d,%zu,%zu,%d,%.9f,%d,%d\n", iid, r, c, feas ? 1 : 0,
                                     feas ? matrix.costMatrix[r][c] : -1.0, inG, inH);
                    }
                }
                std::fprintf(inst, "%d,%d,%d,%s,%d,%d,%.9f,%d,%.9f,%.6f,%.6f\n", iid, size, seed,
                             wname.c_str(), feasiblePairs, g.metrics.assignedPairs,
                             g.metrics.totalAssignmentCost, h.metrics.assignedPairs,
                             h.metrics.totalAssignmentCost, g.metrics.executionTimeMs,
                             h.metrics.executionTimeMs);
                ++iid;
            }
            std::fflush(inst);
            std::fflush(cells);
            std::fprintf(stderr, "[e3] size=%d seed=%d\n", size, seed);
        }
    }
}
