#pragma once

/// @file SimulationState.hpp
/// The single authoritative world state a simulation run mutates — Phase 8 brief §18-19.
///
/// ## Single source of truth, precisely
///
/// Every satellite, vehicle, debris object, emergency and mission that exists during a run
/// lives here, exactly once, as an owned value. Nothing else in `simulation/` keeps a
/// competing copy: `EmergencyGenerator`, `MissionExecutor` and `SimulationEngine` all take
/// a `SimulationState&` and mutate entities THROUGH their own validated domain APIs
/// (`Satellite::applyDamage()`, `Mission::setState()`, ...) rather than writing fields
/// directly — this file owns storage, not behaviour (brief §26's "do not directly mutate
/// mission status bypassing the domain state machine" applies to every entity here, not
/// only missions).
///
/// ## Why `unordered_map<uint32_t, T>`, not `vector<T>` + index
///
/// `Mission`, `Emergency`, `Satellite`, `RescueVehicle` and `DebrisObject` all have deleted
/// default constructors (the project-wide "no sentinel values" convention,
/// `core/Types.hpp`), so a `std::vector<T>` growing via `push_back` risks a reallocation
/// invalidating every reference held elsewhere mid-tick -- a real hazard once emergencies
/// and missions are created dynamically over a run, not just read from a fixed `Scenario`.
/// `std::unordered_map`'s node-based storage keeps every VALUE's address stable across
/// insertions (only bucket bookkeeping moves), which is what lets `MissionExecutor` and
/// `SimulationEngine` hold a `Mission&`/`Satellite&` across a tick safely.
///
/// ## Scenario-assigned vs. run-assigned identity
///
/// Satellites, vehicles and debris keep the `Id` values `Scenario` gave them at load time.
/// Emergencies, missions and mission plans are created DURING a run and need freshly
/// minted IDs — `nextEmergencyId`/`nextMissionId`/`nextPlanId` are simple monotonic
/// counters this state owns, the same caller-managed-sequence convention
/// `CandidateGeneration.hpp`'s `PlanId& nextPlanId` already established in Phase 5/6.

#include <deque>
#include <optional>
#include <string>
#include <unordered_map>

#include "../core/Types.hpp"
#include "../core/Units.hpp"
#include "../domain/DebrisObject.hpp"
#include "../domain/Emergency.hpp"
#include "../domain/Mission.hpp"
#include "../domain/MissionPlan.hpp"
#include "../domain/RescueVehicle.hpp"
#include "../domain/Satellite.hpp"
#include "../analytics/AlgorithmRun.hpp"
#include "../domain/Scenario.hpp"
#include "../optimization/ParetoFront.hpp"
#include "Event.hpp"

namespace orca {

/// Phase 8 brief §71's diagnostic counters. Not analytics (`analytics/` is a later
/// phase) — a plain, honest tally of what actually happened this run, for tests and logs.
struct SimulationMetrics {
    long ticks = 0;
    long eventsProcessed = 0;
    long missionsUpdated = 0;
    long replansTriggered = 0;
    long replansCompleted = 0;   ///< replansTriggered where the outcome was Replan (a switch)
    long missionSuccesses = 0;
    long missionFailures = 0;
    long missionAborts = 0;
};

struct SimulationState {
    Seconds simulationTimeSec = 0.0;

    std::unordered_map<std::uint32_t, Satellite> satellites;
    std::unordered_map<std::uint32_t, RescueVehicle> vehicles;
    std::unordered_map<std::uint32_t, DebrisObject> debris;
    std::unordered_map<std::uint32_t, Emergency> emergencies;
    std::unordered_map<std::uint32_t, Mission> missions;

    /// The `MissionPlan` currently governing each mission -- Phase 7's own established
    /// need (`DYNAMIC_REPLANNING.md`'s note: `DynamicReplanner` takes the current plan
    /// explicitly rather than re-deriving it from `Mission` alone, since `Mission` stores
    /// only a route of `EdgeId`s and a `PlanId`, not the full `MissionPlan` with its cost).
    /// Keyed by `MissionId`, not `PlanId` -- exactly one plan governs a mission at a time.
    std::unordered_map<std::uint32_t, MissionPlan> missionPlans;

    /// One retained planning pass -- Phase 11 brief §52-54's Pareto history: a later
    /// replan appends a NEW entry rather than overwriting the original (Phase 10's own
    /// stated limitation, now addressed). Pass 1 (global dispatch) carries the REAL,
    /// multi-point front `generateCandidateFront()` produced. **A later pass (2+, from a
    /// local replan) is honestly narrower**: it carries only the single plan that was
    /// actually adopted (`ReplanDecision::newPlan`), not a full re-derived multi-point
    /// front -- `DynamicReplanner`'s internal remaining-route search is not currently
    /// re-run a second time purely for display (unlike pass 1's dispatch-time front, which
    /// IS safely re-derivable via the same simple vehicle-to-target search the real
    /// assignment used). Stated explicitly in `VISUAL_DESIGN.md`, not silently narrower
    /// than the UI implies.
    struct ParetoFrontSnapshot {
        int planningPass = 1;
        Seconds timestampSec = 0.0;
        ParetoFront front;
    };
    std::unordered_map<std::uint32_t, std::vector<ParetoFrontSnapshot>> missionParetoHistory;

    /// The most recent live profile re-scoring requested for each mission's stored front
    /// (Phase 11 brief §50-51) -- `MissionScorer::selectRecommendedPlan()` applied directly
    /// to the ALREADY-COMPUTED `missionParetoHistory` entry, never a new search. Absent for
    /// a mission that has never had a re-scoring request, in which case the view layer
    /// falls back to Phase 10's original behaviour (mark the point matching the mission's
    /// actual adopted route). Keyed by mission id; value is `{profileName, recommendedPlanId}`.
    struct ParetoRescore {
        std::string profileName;
        PlanId recommendedPlanId;
    };
    std::unordered_map<std::uint32_t, ParetoRescore> missionRescoring;

    /// The deterministic, table-driven explanation (`planning/ReplanDecision.hpp::explain()`)
    /// of the most recent REAL decision (`Replan` or `Abort`) made about each mission --
    /// Phase 10's "why ORCA chose it" panel. Absent for a mission that has never been
    /// replanned (there is nothing to explain beyond the initial assignment, which this
    /// map deliberately does not fabricate an explanation for).
    std::unordered_map<std::uint32_t, std::string> missionExplanations;

    /// The most recent completed algorithm runs, oldest first, capped at
    /// `kMaxAlgorithmRuns` -- Phase 11's Algorithm Lab telemetry (brief §18: "bounded ring
    /// buffer... do not allow unlimited growth during long simulations"). Populated by
    /// `EventDispatcher.cpp` alongside the real global-dispatch decision, from algorithms
    /// run for comparison purposes on the same vehicle/emergency pair the real decision
    /// used -- never fabricated, never affecting the decision itself.
    static constexpr std::size_t kMaxAlgorithmRuns = 20;
    std::deque<AlgorithmRunRecord> algorithmRuns;

    void recordAlgorithmRun(AlgorithmRunRecord record);

    SimulationMetrics metrics;

    /// The most recently *fired* (already-dispatched) events, oldest first, capped at
    /// `kMaxRecentEvents` -- a structured event log for a future snapshot/timeline
    /// consumer (Phase 9's own motivation), distinct from `EventQueue`, which only ever
    /// holds events still *pending*. Bounded rather than unbounded so a very long run
    /// cannot grow this without limit; a consumer that needs the full history has the
    /// same access to `metrics.eventsProcessed` as a running total regardless of how much
    /// of the detailed log has aged out.
    static constexpr std::size_t kMaxRecentEvents = 100;
    std::deque<Event> recentEvents;

    /// Appends `event` to `recentEvents`, trimming from the front once `kMaxRecentEvents`
    /// is exceeded. The single place anything pushes to the log, so the cap is enforced in
    /// exactly one function rather than at every call site.
    void recordEvent(Event event);

    std::uint32_t nextEmergencyId = 0;
    std::uint32_t nextMissionId = 0;
    PlanId nextPlanId{1};

    // --- Lookup, mirroring Scenario::findX's "nullptr means not found, not an error"
    // convention (Scenario.hpp) -------------------------------------------------------

    [[nodiscard]] Satellite* findSatellite(SatelliteId id) noexcept;
    [[nodiscard]] const Satellite* findSatellite(SatelliteId id) const noexcept;
    [[nodiscard]] RescueVehicle* findVehicle(VehicleId id) noexcept;
    [[nodiscard]] const RescueVehicle* findVehicle(VehicleId id) const noexcept;
    [[nodiscard]] DebrisObject* findDebris(DebrisId id) noexcept;
    [[nodiscard]] const DebrisObject* findDebris(DebrisId id) const noexcept;
    [[nodiscard]] Emergency* findEmergency(EmergencyId id) noexcept;
    [[nodiscard]] const Emergency* findEmergency(EmergencyId id) const noexcept;
    [[nodiscard]] Mission* findMission(MissionId id) noexcept;
    [[nodiscard]] const Mission* findMission(MissionId id) const noexcept;
    [[nodiscard]] const MissionPlan* findMissionPlan(MissionId id) const noexcept;

    /// Every vehicle currently idle and healthy enough to dispatch --
    /// `RescueVehicle::isAvailable()`, collected fresh each call (never cached, so it is
    /// always consistent with whatever mutations already happened this tick).
    [[nodiscard]] std::vector<VehicleId> availableVehicleIds() const;

    /// Every emergency still `Pending` (not yet assigned, resolved or expired).
    [[nodiscard]] std::vector<EmergencyId> pendingEmergencyIds() const;

    /// Every mission that is `isActive()` (`Mission.hpp`) -- launched, in transit,
    /// replanning, or at rendezvous.
    [[nodiscard]] std::vector<MissionId> activeMissionIds() const;
};

/// Builds the initial world state from a loaded `Scenario` -- satellites, vehicles and
/// debris copied in (their Scenario-assigned IDs preserved), everything dynamic
/// (emergencies, missions) starting empty. `simulationTimeSec` starts at `0.0`.
[[nodiscard]] SimulationState makeInitialState(const Scenario& scenario);

}  // namespace orca
