#pragma once

/// @file PlaneChange.hpp
/// Inclination-change delta-v.
///
/// ## Why this is the most important cost term in ORCA
///
/// Measured (REPOSITORY_AUDIT.md §4.1): a **one degree** plane change at 500 km costs
/// 0.1329 km/s, while raising an orbit by **300 km** costs 0.1608 km/s. One degree of
/// inclination is 83% of a 300 km altitude raise, and the break-even is about 1.21°.
///
/// Two consequences run through the whole system:
///
///   1. In LEO, any realistic inclination spread swamps any realistic altitude spread. A
///      rescue vehicle in a different plane is effectively unreachable however close it
///      looks on screen.
///   2. Euclidean distance is therefore a *physically wrong* proxy for cost. That is why
///      ORCA carries `distance` as a separate objective from `fuel` — the two genuinely
///      disagree, and the disagreement is exactly this term.
///
/// The functions here operate on **speeds**, not orbits. They have no knowledge of
/// radii, transfer ellipses or ORCA's domain types, which makes them trivially testable
/// against closed-form values.

#include "../core/Units.hpp"

namespace orca::plane_change {

/// Delta-v for a pure plane change at constant speed.
///
///     dv = 2 * v * sin(di / 2)
///
/// Valid when the manoeuvre changes only the orbital plane, leaving speed unchanged —
/// i.e. a single burn at a node of two circular orbits of equal radius.
///
/// `deltaInclinationRad` is used as an unsigned magnitude: rotating the plane by +di and
/// by -di cost the same.
[[nodiscard]] KmPerSec pureDeltaVKms(KmPerSec speedKms, Radians deltaInclinationRad) noexcept;

/// Delta-v for a burn that changes both speed and plane at once.
///
///     dv = sqrt(v1^2 + v2^2 - 2*v1*v2*cos(di))
///
/// This is the law of cosines on the velocity triangle: the burn is the vector difference
/// between the incoming velocity `v1` and the desired outgoing velocity `v2`, separated by
/// angle `di`.
///
/// It is never worse than doing the two separately, and usually much better — vector
/// addition beats scalar addition. At `di = 0` it degenerates to `|v2 - v1|`; at
/// `v1 == v2` it degenerates exactly to `pureDeltaVKms`. Both degeneracies are tested,
/// because they are what make this formula trustworthy at the boundaries where ORCA's
/// scenarios actually sit.
[[nodiscard]] KmPerSec combinedDeltaVKms(KmPerSec speedBeforeKms,
                                         KmPerSec speedAfterKms,
                                         Radians deltaInclinationRad) noexcept;

/// Unsigned inclination difference between two orbits, in [0, pi].
///
/// Inclination is conventionally bounded to [0, pi] (Orbit enforces this), so the
/// difference is a plain absolute value — no angular wrapping is involved. Wrapping would
/// be wrong here: inclinations of 10 deg and 170 deg differ by 160 deg, not by 20 deg,
/// because they are genuinely near-opposite orbital planes.
[[nodiscard]] Radians inclinationDifferenceRad(Radians a, Radians b) noexcept;

}  // namespace orca::plane_change
