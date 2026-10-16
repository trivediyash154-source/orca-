#pragma once

/// @file PhasingModel.hpp
/// When can we depart so that we actually arrive where the target will be?
///
/// # THIS FILE IS WHERE ORCA'S FIFO GUARANTEE COMES FROM
///
/// ## The problem
///
/// A Hohmann transfer arrives 180 degrees around from where it departed, `coastSec` later.
/// The target must be *at that point when we get there*. It usually is not. So a departure
/// is only useful at particular moments, and those moments recur with the **synodic
/// period** — 25.4 hours for a 500 km and an 800 km orbit (REPOSITORY_AUDIT.md §4.2),
/// against survival windows measured in tens of minutes.
///
/// ## Two ways to model that, and why only one of them is safe
///
/// **Rejected — a delta-v penalty for departing at a bad phase.** This is what Phase 0's
/// SIMULATION_MODEL.md §4.5 specified: depart immediately, pay extra for the awkward
/// geometry. It produces an attractive continuous trade-off. **It also breaks FIFO.**
///
/// If the time cost varies with phase error, then a departure one minute later can arrive
/// *earlier*, because the phase improved by more than the minute cost. Measured across
/// 4000 randomly sampled orbit pairs, that model violated FIFO in **827** of them,
/// concentrated in LEO-to-MEO/GEO transfers — precisely ORCA's multi-band scenarios. The
/// condition turns out to be `k < 2 / (coastTime * |dOmega|)`, which large altitude
/// differences comfortably breach.
///
/// Without FIFO, Dijkstra's greedy commitment is unsound: a node finalised early can later
/// be improved. Building the search layer on that would produce wrong routes that look
/// entirely plausible.
///
/// **Adopted — wait for the window.** The vehicle loiters in its source orbit until the
/// geometry is right, then flies the same optimal transfer it would always have flown.
///
/// ## The proof
///
/// Let `A(t) = min { s >= t : the geometry is correct at s }` and `coast` be constant.
///
///     arrival(t) = A(t) + coast
///
/// `A` is non-decreasing: for `t1 <= t2`, the set `{s >= t2}` is a subset of `{s >= t1}`,
/// so its minimum cannot be smaller. Adding a constant preserves that. Therefore
/// `arrival` is non-decreasing in `t`. **FIFO holds by construction, unconditionally.**
///
/// Concretely, `wait(t)` decreases at exactly rate 1 as `t` advances, so `arrival` is a
/// **staircase**: flat between windows, jumping up by one synodic period at each. Verified
/// numerically to 5.6e-16 relative — 2 to 3 ulp, which is rounding, not drift.
///
/// ## The consequence that matters for Phase 3
///
/// Because *all* phase dependence is absorbed into the wait, the delta-v of an edge does
/// not depend on when you depart. Only **time** is time-dependent; fuel, risk and distance
/// are geometry-only constants. That is a much stronger property than FIFO alone, and
/// Phase 3's multi-objective label-setting needs it — see TRANSFER_MODEL.md §13.
///
/// ## Simplifications
///
/// Phase is treated as an angular position in a shared reference, so the **line-of-nodes
/// constraint imposed by differing RAAN is not modelled**. Two orbits with the same
/// inclination but different RAAN can only rendezvous at their node crossing; ORCA ignores
/// that and treats phase alignment alone as sufficient. This under-constrains rendezvous
/// for non-coplanar transfers. It is exact for coplanar, equal-RAAN cases.
///
/// This is a planning abstraction, not a rendezvous solver.

#include "../core/Units.hpp"
#include "../domain/Orbit.hpp"

namespace orca::phasing {

/// The next moment at which departing yields a rendezvous.
struct RendezvousWindow {
    bool exists = false;

    /// Loiter time from the requested departure until the window opens. Zero when the
    /// geometry is already correct.
    Seconds waitSec = 0.0;

    /// Absolute time the window opens: `departureTimeSec + waitSec`.
    Seconds windowTimeSec = 0.0;

    /// How often the window recurs. Diverges as the two orbits' periods converge, which
    /// is why near-identical orbits are effectively unreachable from one another.
    Seconds synodicPeriodSec = 0.0;

    /// Relative phase the target must hold at departure for the rendezvous to close:
    /// `transferArc - omegaTarget * coast`.
    Radians requiredLeadRad = 0.0;

    /// Relative phase actually held at the requested departure time.
    Radians currentRelativePhaseRad = 0.0;
};

/// Find the next rendezvous window at or after `departureTimeSec`.
///
/// @param transferArcRad  Angle swept by the transfer, measured at the central body:
///                        `pi` for a Hohmann transfer, `0` for a pure plane change.
/// @param coastSec        Flight time of the transfer arc.
///
/// Returns `exists == false` when the two orbits have no relative drift and are not
/// already aligned — two spacecraft in the same orbit at different phases can never
/// rendezvous by waiting, which is physically correct and is why `PhasingUnavailable`
/// exists as a distinct failure reason.
///
/// The caller is responsible for rejecting windows that open too late; this function
/// reports the wait rather than judging it, keeping calculation separate from policy.
[[nodiscard]] RendezvousWindow nextWindow(const Orbit& source,
                                          const Orbit& target,
                                          Seconds departureTimeSec,
                                          Seconds coastSec,
                                          Radians transferArcRad) noexcept;

/// Synodic period of two orbits: how long between repetitions of their relative geometry.
///
///     T_syn = 2*pi / |omega2 - omega1|
///
/// Returns positive infinity when the orbits have no relative drift.
[[nodiscard]] Seconds synodicPeriodSec(const Orbit& a, const Orbit& b) noexcept;

/// Relative drift below which two orbits are treated as having no relative motion.
///
/// 1e-12 rad/s corresponds to a synodic period of roughly 200 000 years, which is
/// unambiguously "never" at any planning horizon ORCA considers.
inline constexpr double kMinRelativeDriftRadPerSec = 1e-12;

/// Angular tolerance for "already aligned" when there is no relative drift.
inline constexpr Radians kAlignmentToleranceRad = 1e-9;

}  // namespace orca::phasing
