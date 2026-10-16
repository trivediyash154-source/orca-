#pragma once

/// @file TransferFeasibility.hpp
/// Constraint policy, separated from transfer physics.
///
/// This is a **pure function of (geometry, cost, constraints)**. It computes nothing about
/// orbits; it only judges. Keeping it here rather than inside `TransferModel` means:
///
///   * a later planner can reuse the physical approximation under different rules without
///     forking the maths;
///   * the check can be unit-tested against hand-built geometries, with no orbital
///     mechanics involved at all;
///   * the order in which constraints are evaluated is visible in one place, which
///     matters because the *first* violated constraint is the one reported to the user.
///
/// ## Ordering
///
/// Constraints are checked most-fundamental first, so the reported reason is the most
/// informative one. A transfer that is both unreachable and unaffordable reports the
/// phasing problem, because no amount of fuel would fix it.

#include "TransferTypes.hpp"

namespace orca {

/// Judge a computed transfer against the constraints.
///
/// Returns `TransferFailureReason::Feasible` when every constraint is satisfied.
///
/// @param geometry  what the transfer physically requires
/// @param fuelCost  delta-v as a fraction of the vehicle's budget
/// @param risk      estimated transfer risk in [0,1]
[[nodiscard]] TransferFailureReason checkFeasibility(
    const TransferGeometry& geometry,
    Fraction fuelCost,
    double risk,
    const TransferConstraints& constraints) noexcept;

}  // namespace orca
