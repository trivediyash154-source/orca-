/// \file
/// SGP4 orbital propagation — the near-Earth path of the NORAD analytic model.
///
/// ## What this is, and why it is here
///
/// A TLE is not a state vector. Its elements are *mean* elements defined only with respect to
/// the SGP4 model that generated them: propagating them with a Keplerian two-body formula, or
/// with the project's own `Orbit::positionAtKm()`, produces a position that looks reasonable and
/// is wrong by tens of kilometres within hours. SGP4 is therefore not an optimisation but the
/// only correct way to turn a catalogue record into a position.
///
/// It lives in C++ because `docs/ARCHITECTURE.md` §6.1 makes the engine the computational source
/// of truth, and because `orca_engine` must be able to propagate a catalogue standalone, with no
/// Node and no browser.
///
/// ## Scope: near-Earth only, stated rather than hidden
///
/// The full model has two branches. Objects with an orbital period under 225 minutes use the
/// near-Earth branch (SGP4); everything above it — most MEO, all GEO, and highly eccentric
/// orbits — additionally requires the deep-space branch (SDP4), with lunar-solar periodics and
/// resonance terms that are several times the size of this file.
///
/// Only the near-Earth branch is implemented. A deep-space object is REJECTED at initialisation
/// with `Sgp4Error::DeepSpaceUnsupported` rather than propagated by the near-Earth equations,
/// because doing the latter yields a confident, badly wrong answer — the exact failure this
/// project's architecture rules exist to prevent. Callers surface it as an honest "not
/// propagatable" state. That covers the entire LEO catalogue, which is where rescue planning
/// happens.
///
/// ## Units and frame
///
/// Internally SGP4 works in Earth radii and minutes, on the WGS-72 constants the model is
/// *defined* against — using WGS-84 values here would silently bias every result, since the
/// element sets were themselves fitted with WGS-72. Output is kilometres and km/s in TEME
/// (True Equator, Mean Equinox), which `Frames.hpp` converts onward.

#ifndef ORCA_ORBITAL_SGP4_HPP
#define ORCA_ORBITAL_SGP4_HPP

#include <optional>
#include <string_view>

#include "Tle.hpp"
#include "../core/Vector3.hpp"

namespace orca::orbital {

/// Why an element set could not be initialised or propagated. Every one is a real, reachable
/// condition for a live catalogue, not a defensive placeholder.
enum class Sgp4Error {
    None,
    /// Orbital period >= 225 minutes: the deep-space branch is required. See the file comment.
    DeepSpaceUnsupported,
    /// Eccentricity outside [0, 1) at epoch — a corrupt or non-orbital element set.
    InvalidEccentricity,
    /// Mean motion at or below zero.
    InvalidMeanMotion,
    /// Eccentricity left [0, 1) during propagation, or the semi-latus rectum went negative:
    /// the object has decayed or the elements are far past their validity.
    PropagationDiverged,
    /// The propagated radius fell below Earth's surface — the object has re-entered.
    Decayed,
};

[[nodiscard]] std::string_view describeSgp4Error(Sgp4Error error) noexcept;

/// Position and velocity in TEME, kilometres and km/s.
struct StateVector {
    Vector3 positionKm;
    Vector3 velocityKmPerSec;
};

/// An element set with SGP4's initialisation already applied.
///
/// Initialisation is the expensive half of the model — the secular rates, drag coefficients and
/// perigee-dependent terms below are all computed once per object, not once per propagation. A
/// catalogue of 10,000 objects therefore pays it 10,000 times at load and never again.
class Sgp4Satellite {
public:
    /// Initialises from a parsed element set. Returns nullopt for a set this implementation
    /// will not propagate; `error` then says which case applies.
    [[nodiscard]] static std::optional<Sgp4Satellite> initialise(const TleElements& elements,
                                                                 Sgp4Error* error = nullptr);

    /// Propagates to `minutesSinceEpoch`. Negative values propagate backwards, which is what
    /// makes historical replay possible without a separate code path.
    [[nodiscard]] std::optional<StateVector> propagate(double minutesSinceEpoch,
                                                       Sgp4Error* error = nullptr) const;

    [[nodiscard]] double epochJulianDate() const noexcept { return epochJd_; }
    [[nodiscard]] int noradId() const noexcept { return noradId_; }
    /// Minutes per revolution, from the un-Kozai'd mean motion actually used for propagation.
    [[nodiscard]] double periodMinutes() const noexcept;

private:
    Sgp4Satellite() = default;

    int noradId_ = 0;
    double epochJd_ = 0.0;

    // Epoch elements, in the model's own units: radians and radians per minute.
    double bstar_ = 0.0;
    double ecco_ = 0.0;
    double inclo_ = 0.0;
    double nodeo_ = 0.0;
    double argpo_ = 0.0;
    double mo_ = 0.0;
    double no_ = 0.0;  ///< Un-Kozai'd mean motion, rad/min.

    // Derived once at initialisation.
    bool simpleModel_ = false;
    double aycof_ = 0.0;
    double con41_ = 0.0;
    double cc1_ = 0.0;
    double cc4_ = 0.0;
    double cc5_ = 0.0;
    double d2_ = 0.0;
    double d3_ = 0.0;
    double d4_ = 0.0;
    double delmo_ = 0.0;
    double eta_ = 0.0;
    double argpdot_ = 0.0;
    double omgcof_ = 0.0;
    double sinmao_ = 0.0;
    double t2cof_ = 0.0;
    double t3cof_ = 0.0;
    double t4cof_ = 0.0;
    double t5cof_ = 0.0;
    double x1mth2_ = 0.0;
    double x7thm1_ = 0.0;
    double mdot_ = 0.0;
    double nodedot_ = 0.0;
    double xlcof_ = 0.0;
    double xmcof_ = 0.0;
    double nodecf_ = 0.0;
};

}  // namespace orca::orbital

#endif  // ORCA_ORBITAL_SGP4_HPP
