#pragma once

/// @file Mission.hpp
/// A committed rescue mission — a plan that is being flown.
///
/// ## Mission is state, not behaviour
///
/// This class tracks *what is true* about a mission: which leg it is on, how far along,
/// how much delta-v it has spent, how many times it has been replanned. It does not
/// decide when to advance, when to replan, or whether to abort. Those are
/// `MissionExecutor` and `DynamicReplanner` in Phase 12, which call the mutators here.
///
/// The split matters because it keeps the invariants in one place. Any code that advances
/// a mission goes through `advanceLeg()` and cannot leave `currentLeg` past the end of the
/// route, however the execution policy changes later.
///
/// ## Rejected alternatives are kept deliberately
///
/// `rejectedAlternatives()` records the plans ORCA chose *against*. This costs memory and
/// buys the ability to answer "why not that one?" with a specific plan rather than a
/// shrug — which is what the Mission Intelligence panel and the explanation generator
/// need (OPTIMIZATION_MODEL.md §11).

#include <cstddef>
#include <optional>
#include <vector>

#include "../core/Types.hpp"
#include "../core/Units.hpp"
#include "MissionPlan.hpp"

namespace orca {

class Mission {
public:
    /// Commit a feasible plan.
    ///
    /// @throws std::invalid_argument if `plan` is infeasible. Committing an infeasible
    ///         plan is the failure mode the whole hard-constraint design exists to
    ///         prevent, so it is rejected here as well as at plan construction.
    Mission(MissionId id, const MissionPlan& plan, Seconds startedAtSec);

    // --- Identity and state -------------------------------------------------

    [[nodiscard]] MissionId id() const noexcept { return id_; }
    [[nodiscard]] PlanId sourcePlan() const noexcept { return sourcePlan_; }
    [[nodiscard]] VehicleId vehicle() const noexcept { return vehicle_; }
    [[nodiscard]] EmergencyId emergency() const noexcept { return emergency_; }
    [[nodiscard]] MissionState state() const noexcept { return state_; }
    [[nodiscard]] const std::vector<EdgeId>& currentRoute() const noexcept {
        return currentRoute_;
    }
    [[nodiscard]] std::size_t currentLeg() const noexcept { return currentLeg_; }
    [[nodiscard]] double progressOnLeg() const noexcept { return progressOnLeg_; }
    [[nodiscard]] Seconds startedAtSec() const noexcept { return startedAtSec_; }
    /// Departure time of the plan currently governing this mission. Leg timings are
    /// recomputed from it (MissionExecutor.cpp), so the replanner needs it to know when an
    /// in-progress leg will actually end.
    [[nodiscard]] Seconds planDepartureSec() const noexcept { return planDepartureSec_; }
    [[nodiscard]] const std::optional<Seconds>& completedAtSec() const noexcept {
        return completedAtSec_;
    }
    [[nodiscard]] KmPerSec deltaVSpentKms() const noexcept { return deltaVSpentKms_; }
    [[nodiscard]] int replanCount() const noexcept { return replanCount_; }
    [[nodiscard]] const std::vector<PlanId>& rejectedAlternatives() const noexcept {
        return rejectedAlternatives_;
    }

    // --- Derived ------------------------------------------------------------

    /// True when the mission has reached a terminal state.
    [[nodiscard]] bool isTerminal() const noexcept;

    /// True when the mission is under way and consuming resources.
    [[nodiscard]] bool isActive() const noexcept;

    /// True when every leg has been flown.
    [[nodiscard]] bool isOnFinalLeg() const noexcept;

    // --- Mutation -----------------------------------------------------------

    /// @throws std::invalid_argument if the transition is not permitted.
    void setState(MissionState next);

    [[nodiscard]] bool canTransitionTo(MissionState next) const noexcept;

    /// @throws std::invalid_argument if `progress` is outside [0,1].
    void setProgressOnLeg(double progress);

    /// Move to the next leg and reset progress.
    ///
    /// @throws std::invalid_argument if already on the final leg. Callers should check
    ///         `isOnFinalLeg()` and transition to `Rendezvous` instead.
    void advanceLeg();

    /// Record delta-v spent. Accumulates; never decreases.
    ///
    /// @throws std::invalid_argument if `amountKms` is negative.
    void recordDeltaVSpent(KmPerSec amountKms);

    /// Adopt a new route mid-mission, recording the plan being abandoned.
    ///
    /// Resets leg progress because the new route starts from the vehicle's *current*
    /// position — replanning from the origin would produce routes the vehicle cannot fly
    /// (ALGORITHMS.md §11.3).
    ///
    /// @throws std::invalid_argument if `replacement` is infeasible or has no route.
    void adoptRoute(const MissionPlan& replacement);

    void recordCompletion(Seconds atSec);

private:
    MissionId id_;
    PlanId sourcePlan_;
    VehicleId vehicle_;
    EmergencyId emergency_;
    MissionState state_ = MissionState::Planned;
    std::vector<EdgeId> currentRoute_;
    std::size_t currentLeg_ = 0;
    double progressOnLeg_ = 0.0;
    Seconds startedAtSec_;
    Seconds planDepartureSec_ = 0.0;
    std::optional<Seconds> completedAtSec_;
    KmPerSec deltaVSpentKms_ = 0.0;
    int replanCount_ = 0;
    std::vector<PlanId> rejectedAlternatives_;
};

/// Mission state transition table (DATA_MODEL.md §5).
///
///     Planned    -> Launched | Aborted
///     Launched   -> InTransit | Aborted | Failed
///     InTransit  -> Replanning | Rendezvous | Failed | Aborted
///     Replanning -> InTransit | Aborted
///     Rendezvous -> Success | Failed
///     Success / Failed / Aborted -> terminal
[[nodiscard]] bool isValidTransition(MissionState from, MissionState next) noexcept;

}  // namespace orca
