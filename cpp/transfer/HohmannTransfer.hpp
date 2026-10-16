#pragma once

/// @file HohmannTransfer.hpp
/// The two-burn transfer ellipse between circular orbits, with an optional plane change.
///
/// # ASSUMPTIONS, STATED PLAINLY
///
/// The Hohmann transfer is the minimum-energy two-impulse transfer between **coplanar
/// circular** orbits under **two-body** motion with **impulsive** burns. Every one of
/// those words is an assumption ORCA makes:
///
///   * circular initial orbit
///   * circular final orbit
///   * coplanar for the base transfer (inclination handled by the plane-change term)
///   * two-body — no J2, no drag, no third-body
///   * impulsive burns — instantaneous, no finite-burn losses
///
/// This is **not** a general orbital transfer solution. A true optimal transfer between
/// two arbitrary states at an arbitrary time requires a Lambert solver, which ORCA
/// deliberately does not have (RESEARCH_EXTENSION.md §3). It is a defensible cost model
/// for a planning problem, and nothing more.
///
/// ## Combined manoeuvre — the choice this file makes
///
/// ORCA models the plane change as **combined with one of the two Hohmann burns**, not as
/// a third separate burn.
///
/// The combination happens at whichever end has the **larger radius**, because that is
/// where the spacecraft is slowest, and the cost of rotating a velocity vector scales with
/// its magnitude. For a raise (r2 > r1) that is the arrival burn; for a descent it is the
/// departure burn.
///
/// **Why combined rather than sequential:** it is what a real mission would fly, and the
/// saving is not marginal — for a 500 -> 800 km transfer with 5 degrees of inclination
/// change it is about 10%. A planner using sequential burns would systematically overprice
/// inclined transfers and would therefore make *different routing decisions*, not merely
/// pessimistic ones. Since the whole project is about routing decisions, that matters.
///
/// `sequentialDeltaVKms` is provided purely so tests can assert `combined <= sequential`.
/// It is not used by the model.

#include "../core/Units.hpp"

namespace orca::hohmann {

/// The transfer ellipse and the burns that enter and leave it.
struct TransferEllipse {
    Kilometres semiMajorAxisKm = 0.0;  ///< a_t = (r1 + r2) / 2

    KmPerSec speedCircularAtSourceKms = 0.0;  ///< circular speed at r1
    KmPerSec speedCircularAtTargetKms = 0.0;  ///< circular speed at r2
    KmPerSec speedOnTransferAtSourceKms = 0.0;  ///< speed on the ellipse at r1
    KmPerSec speedOnTransferAtTargetKms = 0.0;  ///< speed on the ellipse at r2

    KmPerSec departureDeltaVKms = 0.0;
    KmPerSec arrivalDeltaVKms = 0.0;
    KmPerSec totalDeltaVKms = 0.0;

    /// Portion of `totalDeltaVKms` attributable to the inclination change — that is,
    /// how much more the combined burn cost than the same burn would have cost coplanar.
    KmPerSec planeChangeDeltaVKms = 0.0;

    Seconds coastTimeSec = 0.0;   ///< half the ellipse period: departure burn to arrival burn
    Kilometres pathLengthKm = 0.0;  ///< arc length of the half-ellipse flown
};

/// Compute the transfer between two circular orbits, combining the plane change into the
/// burn at the larger radius.
///
/// Requires `sourceRadiusKm > 0` and `targetRadiusKm > 0`. `deltaInclinationRad` is used
/// as an unsigned magnitude.
///
/// For equal radii this degenerates correctly: the ellipse becomes the circle, both
/// transfer speeds equal the circular speed, the departure burn vanishes, and the arrival
/// burn reduces exactly to `2*v*sin(di/2)` — the pure plane-change formula. That
/// degeneracy is tested rather than assumed.
[[nodiscard]] TransferEllipse compute(Kilometres sourceRadiusKm,
                                      Kilometres targetRadiusKm,
                                      Radians deltaInclinationRad) noexcept;

/// Total delta-v if the plane change were flown as a separate third burn at the higher
/// circular orbit, instead of being combined.
///
/// Exists only as a test oracle for the claim that combining is never worse.
[[nodiscard]] KmPerSec sequentialDeltaVKms(Kilometres sourceRadiusKm,
                                           Kilometres targetRadiusKm,
                                           Radians deltaInclinationRad) noexcept;

/// Arc length of the half-ellipse flown between the two burns, in km.
///
/// The transfer ellipse has semi-major axis `a = (r1+r2)/2` and semi-minor axis
/// `b = sqrt(r1*r2)`. Its perimeter has no closed form, so this uses **Ramanujan's second
/// approximation**:
///
///     P ~= pi * [ 3(a+b) - sqrt((3a+b)(a+3b)) ]
///
/// accurate to better than 1e-5 relative for the eccentricities ORCA encounters, and
/// exact for a circle. Half of `P` is the arc actually flown.
///
/// See TRANSFER_MODEL.md §9 for what this quantity does and does not mean.
[[nodiscard]] Kilometres transferArcLengthKm(Kilometres sourceRadiusKm,
                                             Kilometres targetRadiusKm) noexcept;

}  // namespace orca::hohmann
