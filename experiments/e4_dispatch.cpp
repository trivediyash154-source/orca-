// E4: end-to-end simulation. A burst of satellite failures, a fleet that flies one sortie
// per vehicle, and a stream of in-flight disturbances (fuel alarms, communication losses,
// debris alerts) that make the replanner reconsider active missions.
//
// Usage: e4_dispatch <mode> <seeds> <out.csv> <satellites> <disturbances_per_hour>
//   mode "dispatch": solver {Hungarian, Greedy} x cycle {0, 1 h, 3 h} x window {12, 24, 48 h}
//   mode "hysteresis": switch margin sweep, Hungarian, immediate dispatch, 48 h window
#include <cstdlib>
#include <limits>
#include <map>
#include <set>

#include "common.hpp"
#include "planning/MissionValidity.hpp"
#include "simulation/EmergencyGenerator.hpp"
#include "simulation/MissionExecutor.hpp"
#include "simulation/SimulationEngine.hpp"

using namespace orca;
using namespace exp_common;

namespace {

int gSatellites = 50;                // normal family: satellites/10 vehicles
double gDisturbancesPerHour = 1.0;
constexpr double kBurstSec = 6.0 * 3600.0;
constexpr double kStepSec = 10.0;

struct RunConfig {
    DispatchSolver solver = DispatchSolver::Hungarian;
    double cycleSec = 0.0;
    double windowSec = 48.0 * 3600.0;
    double margin = 0.05;
};

struct RunResult {
    int declared = 0, served = 0, servedOnTime = 0;
    double severityServed = 0.0, severityDeclared = 0.0;
    double deltaV = 0.0, meanResponseH = 0.0;
    int switchesTotal = 0, switchesRouteChanged = 0;
    int switchesMidLeg = 0;          // switch adopted while a leg was in progress
    double unchargedDeltaV = 0.0;    // delta-v of in-progress legs dropped by such switches
    double skippedHours = 0.0;       // remaining duration of those legs, skipped by the switch
    ReplanMetrics rm;
    long aborts = 0;
    double wallMs = 0.0;
};

double severityWeight(Severity s) {
    switch (s) {
        case Severity::Low: return 1.0;
        case Severity::Moderate: return 2.0;
        case Severity::High: return 3.0;
        case Severity::Critical: return 4.0;
    }
    return 1.0;
}

RunResult runOnce(int seed, const RunConfig& rc) {
    const auto t0 = std::chrono::steady_clock::now();
    ScenarioGeneratorConfig cfg = scenario_family::normal(gSatellites);
    cfg.timeStepSec = kStepSec;
    const double horizon = kBurstSec + rc.windowSec + 3600.0;
    cfg.durationSec = horizon;
    const Scenario sc = generateScenario("normal", static_cast<Seed>(seed), cfg);

    SimulationEngineConfig ec;
    ec.dispatchSolver = rc.solver;
    ec.dispatchCycleSec = rc.cycleSec;
    ec.singleSortie = true;
    ec.replanPolicy.switchMargin = rc.margin;
    SimulationEngine engine(sc, ec);

    // Failures: identical across configurations for a given seed.
    RandomGenerator failRng = RandomGenerator(static_cast<Seed>(seed)).forStream(RandomStream::Failures);
    RandomFailureConfig fc;
    fc.failureCount = 2 * static_cast<int>(sc.vehicles().size());  // twice the fleet
    fc.durationSec = kBurstSec;
    fc.survivalWindowSec = rc.windowSec;
    scheduleRandomFailures(engine.eventQueueMutable(), engine.state(), fc, failRng);

    // Disturbances: Poisson-like stream drawn on a fixed grid so it is identical across
    // configurations (uniform draws only; no transcendental functions).
    RandomGenerator dRng = RandomGenerator(static_cast<Seed>(seed)).forStream(RandomStream::Debris);
    const int nDist = static_cast<int>(gDisturbancesPerHour * horizon / 3600.0);
    for (int i = 0; i < nDist; ++i) {
        const double at = dRng.nextDoubleInRange(0.0, horizon);
        const double u = dRng.nextDoubleInRange(0.0, 1.0);
        Event ev{.id = EventId(0), .timestampSec = at};
        if (u < 0.4) {
            ev.kind = EventKind::FuelCritical;
            ev.vehicle = sc.vehicles()[static_cast<std::size_t>(
                dRng.nextIntInRange(0, static_cast<std::int64_t>(sc.vehicles().size()) - 1))].id();
            ev.detail = "injected fuel alarm";
        } else if (u < 0.8) {
            ev.kind = EventKind::CommunicationLoss;
            ev.vehicle = sc.vehicles()[static_cast<std::size_t>(
                dRng.nextIntInRange(0, static_cast<std::int64_t>(sc.vehicles().size()) - 1))].id();
            ev.detail = "injected communication loss";
        } else {
            ev.kind = EventKind::DebrisAlert;
            ev.debris = sc.debris()[static_cast<std::size_t>(
                dRng.nextIntInRange(0, static_cast<std::int64_t>(sc.debris().size()) - 1))].id();
            ev.detail = "injected debris alert";
        }
        engine.scheduleEvent(ev);
    }

    RunResult res;
    // Track plan changes per mission to separate route changes from re-adoptions.
    struct Seen {
        std::uint32_t plan;
        std::vector<EdgeId> route;
        std::size_t leg;
        double progress;
        double planDeparture;
    };
    std::map<std::uint32_t, Seen> seen;

    engine.start();
    while (engine.currentTimeSec() < horizon) {
        engine.step();
        for (const auto& [mid, mission] : engine.state().missions) {
            const std::uint32_t plan = mission.sourcePlan().value();
            auto it = seen.find(mid);
            if (it != seen.end() && it->second.plan != plan) {
                ++res.switchesTotal;
                const std::size_t start = remainingLegStartIndex(it->second.leg, it->second.progress);
                std::vector<EdgeId> suffix;
                for (std::size_t k = start; k < it->second.route.size(); ++k) suffix.push_back(it->second.route[k]);
                // After the commitment fix a mid-leg switch keeps the in-progress leg as its
                // first leg; compare only what follows it with the old remaining route.
                std::vector<EdgeId> fresh = mission.currentRoute();
                if (it->second.progress > 0.0 && it->second.leg < it->second.route.size() &&
                    !fresh.empty() && fresh.front() == it->second.route[it->second.leg]) {
                    fresh.erase(fresh.begin());
                }
                if (fresh != suffix) ++res.switchesRouteChanged;
                if (it->second.progress > 0.0 && it->second.leg < it->second.route.size()) {
                    ++res.switchesMidLeg;
                    const auto timings = computeLegTimings(engine.graph(), engine.modelForDispatch(),
                                                           it->second.route, it->second.planDeparture);
                    const LegTiming& lt = timings[it->second.leg];
                    res.unchargedDeltaV += lt.deltaVKms;
                    res.skippedHours += std::max(0.0, lt.arrivalSec - engine.currentTimeSec()) / 3600.0;
                }
            }
            const auto planIt = engine.state().missionPlans.find(mid);
            const double dep = planIt != engine.state().missionPlans.end() ? planIt->second.departureTimeSec() : 0.0;
            seen[mid] = Seen{plan, mission.currentRoute(), mission.currentLeg(), mission.progressOnLeg(), dep};
        }
    }

    double responseSum = 0.0;
    for (const auto& [eid, em] : engine.state().emergencies) {
        ++res.declared;
        res.severityDeclared += severityWeight(em.severity());
    }
    for (const auto& [mid, mission] : engine.state().missions) {
        res.deltaV += mission.deltaVSpentKms();
        if (mission.state() == MissionState::Success && mission.completedAtSec().has_value()) {
            ++res.served;
            const Emergency& em = engine.state().emergencies.at(mission.emergency().value());
            if (*mission.completedAtSec() <= em.deadlineSec() + kStepSec) {
                ++res.servedOnTime;
                res.severityServed += severityWeight(em.severity());
            }
            responseSum += (*mission.completedAtSec() - em.detectedAtSec()) / 3600.0;
        }
    }
    res.meanResponseH = res.served ? responseSum / res.served : 0.0;
    res.rm = engine.replanner().metrics();
    res.aborts = engine.state().metrics.missionAborts;
    res.wallMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return res;
}

void header(FILE* f) {
    std::fprintf(f, "satellites,rate_per_h,seed,solver,cycle_s,window_h,margin,declared,served,served_on_time,"
                    "severity_served,severity_declared,delta_v_kms,mean_response_h,"
                    "switches_total,switches_route_changed,switches_mid_leg,uncharged_dv_kms,skipped_h,"
                    "replan_requests,mandatory,optional,plan_switches,retentions,completions,aborts,wall_ms\n");
}

void row(FILE* f, int seed, const RunConfig& rc, const RunResult& r) {
    std::fprintf(f, "%d,%.1f,%d,%s,%.0f,%.0f,%.4g,%d,%d,%d,%.1f,%.1f,%.6f,%.4f,%d,%d,%d,%.6f,%.4f,%d,%d,%d,%d,%d,%d,%ld,%.1f\n",
                 gSatellites, gDisturbancesPerHour, seed, rc.solver == DispatchSolver::Greedy ? "greedy" : "hungarian", rc.cycleSec,
                 rc.windowSec / 3600.0, rc.margin, r.declared, r.served, r.servedOnTime,
                 r.severityServed, r.severityDeclared, r.deltaV, r.meanResponseH, r.switchesTotal,
                 r.switchesRouteChanged, r.switchesMidLeg, r.unchargedDeltaV, r.skippedHours,
                 r.rm.replanRequests, r.rm.mandatoryReplans, r.rm.optionalReplans, r.rm.planSwitches,
                 r.rm.planRetentions, r.rm.completions, r.aborts, r.wallMs);
    std::fflush(f);
}

}  // namespace

int main(int argc, char** argv) {
    const std::string mode = argv[1];
    const int seeds = std::atoi(argv[2]);
    FILE* f = std::fopen(argv[3], "w");
    gSatellites = std::atoi(argv[4]);
    gDisturbancesPerHour = std::atof(argv[5]);
    header(f);
    for (int seed = 1; seed <= seeds; ++seed) {
        if (mode == "dispatch") {
            for (double window : {12.0 * 3600.0, 24.0 * 3600.0, 48.0 * 3600.0}) {
                for (double cycle : {0.0, 3600.0, 10800.0}) {
                    for (DispatchSolver s : {DispatchSolver::Hungarian, DispatchSolver::Greedy}) {
                        RunConfig rc{.solver = s, .cycleSec = cycle, .windowSec = window};
                        row(f, seed, rc, runOnce(seed, rc));
                    }
                }
            }
        } else {
            for (double margin : {0.0, 0.01, 0.02, 0.05, 0.10, 0.20, 1.0e9}) {
                RunConfig rc{.margin = margin};
                row(f, seed, rc, runOnce(seed, rc));
            }
        }
        std::fprintf(stderr, "[e4 %s] seed %d\n", mode.c_str(), seed);
    }
}
