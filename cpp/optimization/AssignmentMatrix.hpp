#pragma once

/// @file AssignmentMatrix.hpp
/// Pads the rectangular (vehicles x emergencies) cost matrix into the square matrix
/// `Hungarian.hpp`/`GreedyAssignment.hpp` require, with padding semantics that are
/// explicit and justified (Phase 6 brief §12-13) rather than an arbitrary zero-fill.
///
/// # WHY `(N+M) x (N+M)`, NOT `max(N,M) x max(N,M)`
///
/// A naive `max(N,M)`-square padding (e.g. pad only the *smaller* side with dummy rows or
/// columns) can leave the equal-count case (`N == M`) with **no padding at all** — and if
/// some vehicle has zero feasible emergencies in that case, Hungarian has no legal escape
/// and is mathematically forced to select an infeasible real pairing anyway, silently,
/// because a perfect matching on a square matrix must match every row to *some* column.
/// That is exactly the "fabricated solution" Phase 6 brief §13 forbids.
///
/// The fix, the standard technique for optional (not-necessarily-total) bipartite
/// matching via a solver that requires a *perfect* matching: build an `(N+M) x (N+M)`
/// block matrix so every vehicle and every emergency always has its own dedicated
/// escape-hatch capacity, regardless of `N` vs `M` or how much of the real block is
/// infeasible:
///
/// ```
///                    M real emergency cols     N dummy-emergency cols
///   N real vehicle    [ real costs, or       [ 0 -- vehicle idle,
///   rows                kInfeasibleAssign-     any of the N slots,
///                       mentCost if that       cost is the same       ]
///                       pair is infeasible ]
///
///   M dummy-vehicle    [ unassignedPenalty(   [ 0 -- never a real outcome,
///   rows                 priority(col)) --      just balances the block
///                        this emergency         matrix so it is square ]
///                        goes unserved,
///                        same for all M
///                        dummy rows          ]
/// ```
///
/// Every real vehicle row has `N` zero-cost idle columns available; every real emergency
/// column has `M` dummy-vehicle rows offering it at the (bounded, small) unassigned
/// penalty. Since `kInfeasibleAssignmentCost` is astronomically larger than either escape
/// hatch (`AssignmentCost.hpp`), an optimal (or even a greedy-but-not-perverse) solver
/// never voluntarily selects an infeasible real cell while any escape hatch remains —
/// checked directly, not just argued, by `AssignmentMatrix.hpp`'s post-solve validation
/// (`AssignmentResult.hpp`) rejecting any real assignment whose feasibility flag is false.
///
/// # DIMENSION AND COMPLEXITY
///
/// `(N+M) x (N+M)`, so Hungarian's `O(n^3)` becomes `O((N+M)^3)` here — larger than the
/// `O(max(N,M)^3)` a smaller padding scheme would give, in exchange for the correctness
/// guarantee above. `ASSIGNMENT_MODEL.md` §16 states this trade-off explicitly rather than
/// only quoting the smaller bound.

#include <vector>

#include "../core/Types.hpp"
#include "AssignmentCost.hpp"

namespace orca {

/// What a padded matrix's row `i`, column `j` actually represents.
enum class AssignmentCellKind { RealPair, VehicleIdle, EmergencyUnassigned, Unused };

struct AssignmentMatrix {
    std::size_t vehicleCount = 0;
    std::size_t emergencyCount = 0;
    std::size_t dimension = 0;  ///< vehicleCount + emergencyCount

    std::vector<std::vector<double>> costMatrix;  ///< dimension x dimension

    /// Row `i < vehicleCount` -> `vehicleIds[i]`. Rows `>= vehicleCount` are dummy
    /// (no real vehicle); the caller never needs an ID for those.
    std::vector<VehicleId> vehicleIds;

    /// Column `j < emergencyCount` -> `emergencyIds[j]`. Columns `>= emergencyCount` are
    /// dummy idle-slots.
    std::vector<EmergencyId> emergencyIds;

    /// `feasibleReal[i][j]` for `i < vehicleCount, j < emergencyCount` only -- whether
    /// that specific real pair was a feasible candidate. Used for post-solve validation
    /// (`AssignmentResult.hpp`), independent of what cost value is in the matrix.
    std::vector<std::vector<bool>> feasibleReal;

    [[nodiscard]] AssignmentCellKind classify(std::size_t row, std::size_t col) const noexcept;
};

/// Builds the padded matrix from `entries` (`vehicles`-major order, as
/// `generateAllCandidateFronts`/`buildAssignmentCosts` produce -- each entry already
/// carries the emergency's priority, used directly for the unassigned-penalty column, with
/// no separate lookup needed), plus the vehicle and emergency ID lists needed to label rows
/// and columns.
///
/// @throws std::invalid_argument if `entries.size() != vehicles.size() * emergencies.size()`
///         or either input list is empty.
[[nodiscard]] AssignmentMatrix buildAssignmentMatrix(
    const std::vector<AssignmentCostEntry>& entries, const std::vector<VehicleId>& vehicles,
    const std::vector<EmergencyId>& emergencies);

}  // namespace orca
