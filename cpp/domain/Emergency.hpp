#pragma once

/// @file Emergency.hpp
/// A declared emergency aboard a satellite.
///
/// ## What this class does not do
///
/// It does not compute priority, and it does not schedule. Priority is a weighted
/// combination of severity, urgency and feasibility (OPTIMIZATION_MODEL.md §7) whose
/// weights are configuration and whose feasibility term requires a planner — so it
/// belongs to the optimization layer, not to the entity. Queue ordering belongs to
/// `PriorityScheduler` in Phase 2+.
///
/// What lives here is what an emergency intrinsically *is*, plus the two time-derived
/// quantities the rest of the system asks it for: how much of the survival window remains,
/// and how urgent that makes it.
///
/// ## The deadline is derived
///
/// `deadlineSec()` is `detectedAtSec + survivalWindowSec`, computed rather than stored.
/// DATA_MODEL.md §3.3 requires this: storing it separately creates two fields that must
/// agree, and a survival window extended without updating the deadline would produce a
/// casualty that expires at the wrong time for reasons no log would explain.

#include "../core/Types.hpp"
#include "../core/Units.hpp"

namespace orca {

class Emergency {
public:
    /// @throws std::invalid_argument if `survivalWindowSec` is not positive or
    ///         `detectedAtSec` is negative.
    Emergency(EmergencyId id,
              SatelliteId subject,
              EmergencyKind kind,
              Severity severity,
              Seconds detectedAtSec,
              Seconds survivalWindowSec);

    // --- Identity and state -------------------------------------------------

    [[nodiscard]] EmergencyId id() const noexcept { return id_; }
    [[nodiscard]] SatelliteId subject() const noexcept { return subject_; }
    [[nodiscard]] EmergencyKind kind() const noexcept { return kind_; }
    [[nodiscard]] Severity severity() const noexcept { return severity_; }
    [[nodiscard]] Seconds detectedAtSec() const noexcept { return detectedAtSec_; }
    [[nodiscard]] Seconds survivalWindowSec() const noexcept { return survivalWindowSec_; }
    [[nodiscard]] EmergencyState state() const noexcept { return state_; }

    // --- Derived ------------------------------------------------------------

    /// Absolute time at which the survival window closes.
    [[nodiscard]] Seconds deadlineSec() const noexcept;

    /// Seconds of survival window left at `nowSec`, clamped at zero.
    ///
    /// The engine ships this value in snapshots rather than letting the frontend subtract
    /// `deadline - simTime` — see ARCHITECTURE_REVIEW.md §3. The subtraction is trivially
    /// correct today and would diverge silently the moment deadlines became mutable.
    [[nodiscard]] Seconds remainingWindowSec(Seconds nowSec) const noexcept;

    /// Urgency in [0,1]: 0 at detection, 1 when the window has closed.
    ///
    ///     urgency = 1 - remainingWindow / originalWindow
    ///
    /// Rises as the deadline approaches, which is what lets a moderate emergency near
    /// expiry overtake a fresh critical one in the priority queue. That is intended
    /// triage behaviour, not a bug (OPTIMIZATION_MODEL.md §7.1).
    [[nodiscard]] double urgencyAt(Seconds nowSec) const noexcept;

    /// True when the survival window has closed at `nowSec`.
    ///
    /// Note this is a question about *time*, not about state: it can be true before
    /// anything has transitioned the emergency to `Expired`. The simulation loop uses it
    /// to decide when to make that transition.
    [[nodiscard]] bool isPastDeadline(Seconds nowSec) const noexcept;

    /// True when no further state transitions are possible.
    [[nodiscard]] bool isTerminal() const noexcept;

    // --- Mutation -----------------------------------------------------------

    /// @throws std::invalid_argument if the transition is not permitted.
    void setState(EmergencyState next);

    [[nodiscard]] bool canTransitionTo(EmergencyState next) const noexcept;

private:
    EmergencyId id_;
    SatelliteId subject_;
    EmergencyKind kind_;
    Severity severity_;
    Seconds detectedAtSec_;
    Seconds survivalWindowSec_;
    EmergencyState state_ = EmergencyState::Pending;
};

/// Emergency state transition table.
///
///     Pending     -> Assigned | Unreachable | Expired
///     Assigned    -> InProgress | Pending | Expired     (reassignment returns to Pending)
///     InProgress  -> Resolved | Pending | Expired       (mission abort returns to Pending)
///     Unreachable -> Pending | Expired                  (environment may change)
///     Resolved    -> terminal
///     Expired     -> terminal
///
/// `Unreachable` deliberately returns to `Pending` rather than being terminal: a casualty
/// no vehicle can currently reach may become reachable when a vehicle finishes another
/// mission. Treating it as terminal would make that situation permanently invisible
/// (OPTIMIZATION_MODEL.md §7.1).
[[nodiscard]] bool isValidTransition(EmergencyState from, EmergencyState next) noexcept;

}  // namespace orca
