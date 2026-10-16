#pragma once

/// @file DynamicReplanner.hpp
/// "Is my current plan still valid, and if not, what should replace it?" -- Phase 7's
/// central transition from Phase 4-6's "what is the best plan right now?"
///
/// ## Orchestration, not another optimizer
///
/// This file calls `evaluateMissionValidity()` (`MissionValidity.hpp`), then, when a new
/// candidate is actually needed, the exact same search/scoring functions Phase 5-6 already
/// built: `findParetoRoutes()`, `buildParetoFront()`, `selectRecommendedPlan()`. It
/// introduces no new transfer physics, no new dominance rule, no new assignment algorithm
/// -- Phase 7 brief §3's rule, restated in code rather than merely in this comment.
///
/// ## Local vs global
///
/// `DynamicReplanner` (this file) answers the LOCAL question: is one specific mission's
/// route still good. `GlobalReplanner.hpp` answers the GLOBAL question: should the fleet's
/// entire idle-vehicle-to-pending-emergency assignment be reconsidered — and it does so by
/// calling straight into Phase 6's `CandidateGeneration`/`AssignmentCost`/`AssignmentMatrix`
/// /`AssignmentResult` pipeline, unmodified. The two are genuinely different questions
/// (Phase 7 brief §22) and are kept in separate files for that reason.

#include <chrono>
#include <optional>
#include <vector>

#include "../core/Units.hpp"
#include "../domain/Emergency.hpp"
#include "../domain/Mission.hpp"
#include "../domain/RescueVehicle.hpp"
#include "../graph/MissionGraph.hpp"
#include "../optimization/OptimizationProfile.hpp"
#include "../transfer/TransferModel.hpp"
#include "MissionChange.hpp"
#include "ReplanDecision.hpp"
#include "ReplanPolicy.hpp"

namespace orca {

/// One entry in the replanner's history log -- Phase 7 brief §27. Deliberately small: the
/// full `ReplanDecision` (and its `explain()`) is available at decision time and is the
/// caller's to persist if it wants the complete record; this is the lightweight index.
struct ReplanHistoryEntry {
    Seconds timestampSec = 0.0;
    MissionId mission;
    ReplanReason reason;
    PlanId previousPlan;
    std::optional<PlanId> newPlan;
    ReplanDecisionType decision;
};

/// Phase 7 brief §28's counters, verbatim.
struct ReplanMetrics {
    int replanRequests = 0;
    int mandatoryReplans = 0;
    int optionalReplans = 0;
    int planSwitches = 0;
    int planRetentions = 0;
    int aborts = 0;
    int completions = 0;
    double totalReplanComputationMs = 0.0;
    int candidatePlansGenerated = 0;

    [[nodiscard]] double averageReplanComputationMs() const noexcept {
        return replanRequests > 0 ? totalReplanComputationMs / static_cast<double>(replanRequests)
                                 : 0.0;
    }
};

class DynamicReplanner {
public:
    explicit DynamicReplanner(ReplanPolicy policy = defaultReplanPolicy());

    /// The single entry point (Phase 7 brief §30). Deterministic: identical arguments
    /// (including `nextPlanId` and this instance's accumulated history/metrics state)
    /// produce an identical `ReplanDecision` every time (brief §42-43).
    ///
    /// @param nextPlanId Advanced by however many `MissionPlan`s this call constructs
    ///        (zero, if no replan was attempted or none was found) -- the same
    ///        caller-managed-sequence convention `generateCandidateFront()` uses.
    [[nodiscard]] ReplanDecision evaluate(const Mission& mission, const RescueVehicle& vehicle,
                                          const Emergency& emergency, const MissionGraph& graph,
                                          const TransferModel& model, Seconds nowSec,
                                          const MissionChange& trigger,
                                          const OptimizationProfile& profile,
                                          PlanId& nextPlanId);

    /// Should `mission` be preempted in favour of a higher-priority emergency -- Phase 7
    /// brief §23. A pure policy decision: never searches for a replacement plan (per this
    /// file's header comment, `Mission::emergency()` cannot change post-construction --
    /// `Mission.hpp`'s own API has no setter for it -- so "preemption" here means
    /// "abandon this mission, freeing the vehicle," never "retarget it." Whatever picks up
    /// the freed vehicle for the new emergency is a SEPARATE, subsequent global-replan
    /// pass (`GlobalReplanner.hpp`), not this call.
    ///
    /// @param missionProgressFraction In `[0,1]` -- how far along `mission` already is;
    ///        `(currentLeg + progressOnLeg) / max(1, routeLegCount)` is the natural way to
    ///        compute it from a real `Mission`, left to the caller since this function has
    ///        no need for the `Mission` object itself beyond its id and progress.
    /// @param currentEmergencyPriority `mission`'s own emergency's current priority
    ///        (`EmergencyPriority.hpp`'s `computePriority()`).
    /// @param candidateEmergencyPriority The new/escalated emergency's current priority.
    ///
    /// @throws std::invalid_argument if `missionProgressFraction` is outside `[0,1]`.
    [[nodiscard]] ReplanDecision evaluatePreemption(const Mission& mission,
                                                    double missionProgressFraction,
                                                    double currentEmergencyPriority,
                                                    double candidateEmergencyPriority,
                                                    Seconds nowSec);

    [[nodiscard]] const std::vector<ReplanHistoryEntry>& history() const noexcept {
        return history_;
    }
    [[nodiscard]] const ReplanMetrics& metrics() const noexcept { return metrics_; }
    [[nodiscard]] const ReplanPolicy& policy() const noexcept { return policy_; }

private:
    ReplanPolicy policy_;
    std::vector<ReplanHistoryEntry> history_;
    ReplanMetrics metrics_;

    /// Last replan timestamp per mission, for `ReplanPolicy::minimumReplanIntervalSec`
    /// throttling (brief §47) -- keyed by the mission's raw id so no hash specialisation
    /// is needed, the same convention `CandidateGeneration.cpp`'s `EntityNodeIndex` uses.
    std::vector<std::pair<std::uint32_t, Seconds>> lastReplanAtSecByMission_;

    [[nodiscard]] std::optional<Seconds> lastReplanAtSec(MissionId mission) const noexcept;
    void recordReplanTimestamp(MissionId mission, Seconds atSec);

    /// Shared tail: accumulates elapsed wall-clock time into `metrics_` and appends one
    /// `ReplanHistoryEntry` -- called from every exit path of `evaluate()` so timing/
    /// history bookkeeping cannot drift between branches.
    void finalize(const Mission& mission, const ReplanDecision& decision,
                 std::chrono::steady_clock::time_point startedAt, Seconds nowSec);
};

}  // namespace orca
