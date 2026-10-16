#include "Sgp4.hpp"

#include <cmath>

namespace orca::orbital {
namespace {

/// WGS-72, because that is the model SGP4 is DEFINED against. The element sets themselves were
/// fitted using these constants, so substituting the more modern WGS-84 values would introduce a
/// systematic error into every propagation while looking like an improvement.
constexpr double kMuWgs72 = 398600.8;             // km^3/s^2
constexpr double kEarthRadiusWgs72Km = 6378.135;  // km
constexpr double kJ2 = 0.001082616;
constexpr double kJ3 = -0.00000253881;
constexpr double kJ4 = -0.00000165597;

constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;
constexpr double kDegToRad = kPi / 180.0;

/// Earth-radii-and-minutes gravitational parameter: sqrt(GM) expressed in the model's units.
const double kXke = 60.0 / std::sqrt(kEarthRadiusWgs72Km * kEarthRadiusWgs72Km *
                                     kEarthRadiusWgs72Km / kMuWgs72);
const double kJ3oJ2 = kJ3 / kJ2;

constexpr double kTwoThirds = 2.0 / 3.0;

/// The period boundary that separates the near-Earth and deep-space branches: 225 minutes, or
/// equivalently a mean motion of 2*pi/225 rad/min.
constexpr double kDeepSpacePeriodMinutes = 225.0;

/// Normalises an angle into [0, 2pi).
[[nodiscard]] double wrapTwoPi(double angle) noexcept {
    double wrapped = std::fmod(angle, kTwoPi);
    if (wrapped < 0.0) wrapped += kTwoPi;
    return wrapped;
}

void setError(Sgp4Error* out, Sgp4Error value) noexcept {
    if (out != nullptr) *out = value;
}

}  // namespace

std::string_view describeSgp4Error(Sgp4Error error) noexcept {
    switch (error) {
        case Sgp4Error::None:
            return "no error";
        case Sgp4Error::DeepSpaceUnsupported:
            return "orbital period is 225 minutes or more; the deep-space branch (SDP4) is "
                   "required and is not implemented";
        case Sgp4Error::InvalidEccentricity:
            return "eccentricity is outside [0, 1)";
        case Sgp4Error::InvalidMeanMotion:
            return "mean motion is not positive";
        case Sgp4Error::PropagationDiverged:
            return "the elements diverged during propagation; the object has decayed or the "
                   "element set is far outside its validity";
        case Sgp4Error::Decayed:
            return "the propagated radius is below Earth's surface";
    }
    return "unknown error";
}

double Sgp4Satellite::periodMinutes() const noexcept {
    return (no_ > 0.0) ? kTwoPi / no_ : 0.0;
}

std::optional<Sgp4Satellite> Sgp4Satellite::initialise(const TleElements& elements,
                                                       Sgp4Error* error) {
    setError(error, Sgp4Error::None);

    if (!(elements.eccentricity >= 0.0) || elements.eccentricity >= 1.0) {
        setError(error, Sgp4Error::InvalidEccentricity);
        return std::nullopt;
    }
    if (!(elements.meanMotionRevPerDay > 0.0)) {
        setError(error, Sgp4Error::InvalidMeanMotion);
        return std::nullopt;
    }

    Sgp4Satellite s;
    s.noradId_ = elements.noradId;
    s.epochJd_ = elements.epochJulianDate();
    s.bstar_ = elements.bstar;
    s.ecco_ = elements.eccentricity;
    s.inclo_ = elements.inclinationDeg * kDegToRad;
    s.nodeo_ = elements.raanDeg * kDegToRad;
    s.argpo_ = elements.argPerigeeDeg * kDegToRad;
    s.mo_ = elements.meanAnomalyDeg * kDegToRad;

    // Revolutions per day -> radians per minute, the model's own unit.
    const double noKozai = elements.meanMotionRevPerDay * kTwoPi / 1440.0;

    // Reject deep-space objects on the PUBLISHED mean motion, before any of the near-Earth
    // initialisation runs on elements it does not describe.
    if (kTwoPi / noKozai >= kDeepSpacePeriodMinutes) {
        setError(error, Sgp4Error::DeepSpaceUnsupported);
        return std::nullopt;
    }

    const double cosio = std::cos(s.inclo_);
    const double sinio = std::sin(s.inclo_);
    const double cosio2 = cosio * cosio;
    const double eccsq = s.ecco_ * s.ecco_;
    const double omeosq = 1.0 - eccsq;
    const double rteosq = std::sqrt(omeosq);

    // "Un-Kozai" the mean motion: a TLE publishes the Kozai mean motion, and the model's
    // secular terms are defined against the Brouwer one. Skipping this shifts every orbit's
    // energy slightly and shows up as a growing along-track error.
    const double ak = std::pow(kXke / noKozai, kTwoThirds);
    const double d1 = 0.75 * kJ2 * (3.0 * cosio2 - 1.0) / (rteosq * omeosq);
    double del = d1 / (ak * ak);
    const double adel = ak * (1.0 - del * del - del * (1.0 / 3.0 + 134.0 * del * del / 81.0));
    del = d1 / (adel * adel);
    s.no_ = noKozai / (1.0 + del);

    const double ao = std::pow(kXke / s.no_, kTwoThirds);
    const double po = ao * omeosq;
    const double con42 = 1.0 - 5.0 * cosio2;
    s.con41_ = -con42 - cosio2 - cosio2;
    const double posq = po * po;
    const double rp = ao * (1.0 - s.ecco_);

    // Below ~220 km perigee the drag terms are truncated: the higher-order d2..d4 corrections
    // are not meaningful for an object that deep in the atmosphere.
    s.simpleModel_ = rp < (220.0 / kEarthRadiusWgs72Km + 1.0);

    // Atmospheric density parameters, adjusted for low perigee.
    double sfour = 78.0 / kEarthRadiusWgs72Km + 1.0;
    double qzms24 = std::pow((120.0 - 78.0) / kEarthRadiusWgs72Km, 4.0);
    const double perige = (rp - 1.0) * kEarthRadiusWgs72Km;
    if (perige < 156.0) {
        sfour = (perige < 98.0) ? 20.0 : perige - 78.0;
        qzms24 = std::pow((120.0 - sfour) / kEarthRadiusWgs72Km, 4.0);
        sfour = sfour / kEarthRadiusWgs72Km + 1.0;
    }

    const double pinvsq = 1.0 / posq;
    const double tsi = 1.0 / (ao - sfour);
    s.eta_ = ao * s.ecco_ * tsi;
    const double etasq = s.eta_ * s.eta_;
    const double eeta = s.ecco_ * s.eta_;
    const double psisq = std::fabs(1.0 - etasq);
    const double coef = qzms24 * std::pow(tsi, 4.0);
    const double coef1 = coef / std::pow(psisq, 3.5);

    const double cc2 = coef1 * s.no_ *
                       (ao * (1.0 + 1.5 * etasq + eeta * (4.0 + etasq)) +
                        0.375 * kJ2 * tsi / psisq * s.con41_ * (8.0 + 3.0 * etasq * (8.0 + etasq)));
    s.cc1_ = s.bstar_ * cc2;

    double cc3 = 0.0;
    if (s.ecco_ > 1.0e-4) {
        cc3 = -2.0 * coef * tsi * kJ3oJ2 * s.no_ * sinio / s.ecco_;
    }
    s.x1mth2_ = 1.0 - cosio2;
    s.cc4_ = 2.0 * s.no_ * coef1 * ao * omeosq *
             (s.eta_ * (2.0 + 0.5 * etasq) + s.ecco_ * (0.5 + 2.0 * etasq) -
              kJ2 * tsi / (ao * psisq) *
                  (-3.0 * s.con41_ * (1.0 - 2.0 * eeta + etasq * (1.5 - 0.5 * eeta)) +
                   0.75 * s.x1mth2_ * (2.0 * etasq - eeta * (1.0 + etasq)) *
                       std::cos(2.0 * s.argpo_)));
    s.cc5_ = 2.0 * coef1 * ao * omeosq * (1.0 + 2.75 * (etasq + eeta) + eeta * etasq);

    const double cosio4 = cosio2 * cosio2;
    const double temp1 = 1.5 * kJ2 * pinvsq * s.no_;
    const double temp2 = 0.5 * temp1 * kJ2 * pinvsq;
    const double temp3 = -0.46875 * kJ4 * pinvsq * pinvsq * s.no_;

    s.mdot_ = s.no_ + 0.5 * temp1 * rteosq * s.con41_ +
              0.0625 * temp2 * rteosq * (13.0 - 78.0 * cosio2 + 137.0 * cosio4);
    s.argpdot_ = -0.5 * temp1 * con42 + 0.0625 * temp2 * (7.0 - 114.0 * cosio2 + 395.0 * cosio4) +
                 temp3 * (3.0 - 36.0 * cosio2 + 49.0 * cosio4);
    const double xhdot1 = -temp1 * cosio;
    s.nodedot_ = xhdot1 + (0.5 * temp2 * (4.0 - 19.0 * cosio2) +
                           2.0 * temp3 * (3.0 - 7.0 * cosio2)) * cosio;

    s.omgcof_ = s.bstar_ * cc3 * std::cos(s.argpo_);
    s.xmcof_ = 0.0;
    if (s.ecco_ > 1.0e-4) {
        s.xmcof_ = -kTwoThirds * coef * s.bstar_ / eeta;
    }
    s.nodecf_ = 3.5 * omeosq * xhdot1 * s.cc1_;
    s.t2cof_ = 1.5 * s.cc1_;

    // Guard the near-polar singularity: as cos(i) -> -1 the denominator vanishes.
    const double denominator = (std::fabs(cosio + 1.0) > 1.5e-12) ? (1.0 + cosio) : 1.5e-12;
    s.xlcof_ = -0.25 * kJ3oJ2 * sinio * (3.0 + 5.0 * cosio) / denominator;
    s.aycof_ = -0.5 * kJ3oJ2 * sinio;

    const double delmoTemp = 1.0 + s.eta_ * std::cos(s.mo_);
    s.delmo_ = delmoTemp * delmoTemp * delmoTemp;
    s.sinmao_ = std::sin(s.mo_);
    s.x7thm1_ = 7.0 * cosio2 - 1.0;

    if (!s.simpleModel_) {
        const double cc1sq = s.cc1_ * s.cc1_;
        s.d2_ = 4.0 * ao * tsi * cc1sq;
        const double temp = s.d2_ * tsi * s.cc1_ / 3.0;
        s.d3_ = (17.0 * ao + sfour) * temp;
        s.d4_ = 0.5 * temp * ao * tsi * (221.0 * ao + 31.0 * sfour) * s.cc1_;
        s.t3cof_ = s.d2_ + 2.0 * cc1sq;
        s.t4cof_ = 0.25 * (3.0 * s.d3_ + s.cc1_ * (12.0 * s.d2_ + 10.0 * cc1sq));
        s.t5cof_ = 0.2 * (3.0 * s.d4_ + 12.0 * s.cc1_ * s.d3_ + 6.0 * s.d2_ * s.d2_ +
                          15.0 * cc1sq * (2.0 * s.d2_ + cc1sq));
    }

    return s;
}

std::optional<StateVector> Sgp4Satellite::propagate(double minutesSinceEpoch,
                                                    Sgp4Error* error) const {
    setError(error, Sgp4Error::None);
    const double t = minutesSinceEpoch;

    // --- Secular gravity and atmospheric drag -------------------------------------------
    const double xmdf = mo_ + mdot_ * t;
    const double argpdf = argpo_ + argpdot_ * t;
    const double nodedf = nodeo_ + nodedot_ * t;
    double argpm = argpdf;
    double mm = xmdf;
    const double t2 = t * t;
    double nodem = nodedf + nodecf_ * t2;
    double tempa = 1.0 - cc1_ * t;
    double tempe = bstar_ * cc4_ * t;
    double templ = t2cof_ * t2;

    if (!simpleModel_) {
        const double delomg = omgcof_ * t;
        const double delmTemp = 1.0 + eta_ * std::cos(xmdf);
        const double delm = xmcof_ * (delmTemp * delmTemp * delmTemp - delmo_);
        const double temp = delomg + delm;
        mm = xmdf + temp;
        argpm = argpdf - temp;
        const double t3 = t2 * t;
        const double t4 = t3 * t;
        tempa = tempa - d2_ * t2 - d3_ * t3 - d4_ * t4;
        tempe = tempe + bstar_ * cc5_ * (std::sin(mm) - sinmao_);
        templ = templ + t3cof_ * t3 + t4 * (t4cof_ + t * t5cof_);
    }

    const double am = std::pow(kXke / no_, kTwoThirds) * tempa * tempa;
    const double nm = kXke / std::pow(am, 1.5);
    const double em = ecco_ - tempe;

    // Eccentricity leaving [0,1) means the drag model has carried the orbit outside the
    // regime the elements describe. Reported, never clamped into a plausible-looking answer.
    if (em >= 1.0 || em < -0.001) {
        setError(error, Sgp4Error::PropagationDiverged);
        return std::nullopt;
    }
    const double emClamped = (em < 1.0e-6) ? 1.0e-6 : em;

    mm = mm + no_ * templ;
    double xlm = mm + argpm + nodem;
    nodem = wrapTwoPi(nodem);
    argpm = wrapTwoPi(argpm);
    xlm = wrapTwoPi(xlm);
    mm = wrapTwoPi(xlm - argpm - nodem);

    const double inclm = inclo_;
    const double sinim = std::sin(inclm);
    const double cosim = std::cos(inclm);

    // --- Long-period periodics ------------------------------------------------------------
    const double axnl = emClamped * std::cos(argpm);
    const double temp = 1.0 / (am * (1.0 - emClamped * emClamped));
    const double aynl = emClamped * std::sin(argpm) + temp * aycof_;
    const double xl = mm + argpm + nodem + temp * xlcof_ * axnl;

    // --- Kepler's equation for (E + argument of perigee) ----------------------------------
    // Newton-Raphson with a step limiter. The limiter is what keeps a high-eccentricity start
    // from overshooting into a different revolution and converging on the wrong root.
    const double u = wrapTwoPi(xl - nodem);
    double eo1 = u;
    double tem5 = 9999.9;
    double sineo1 = 0.0;
    double coseo1 = 0.0;
    for (int ktr = 1; ktr <= 10 && std::fabs(tem5) >= 1.0e-12; ++ktr) {
        sineo1 = std::sin(eo1);
        coseo1 = std::cos(eo1);
        tem5 = 1.0 - coseo1 * axnl - sineo1 * aynl;
        tem5 = (u - aynl * coseo1 + axnl * sineo1 - eo1) / tem5;
        if (std::fabs(tem5) >= 0.95) {
            tem5 = (tem5 > 0.0) ? 0.95 : -0.95;
        }
        eo1 = eo1 + tem5;
    }

    // --- Short-period periodics -----------------------------------------------------------
    const double ecose = axnl * coseo1 + aynl * sineo1;
    const double esine = axnl * sineo1 - aynl * coseo1;
    const double el2 = axnl * axnl + aynl * aynl;
    const double pl = am * (1.0 - el2);
    if (pl < 0.0) {
        setError(error, Sgp4Error::PropagationDiverged);
        return std::nullopt;
    }

    const double rl = am * (1.0 - ecose);
    const double rdotl = std::sqrt(am) * esine / rl;
    const double rvdotl = std::sqrt(pl) / rl;
    const double betal = std::sqrt(1.0 - el2);
    const double temp3 = esine / (1.0 + betal);
    const double sinu = am / rl * (sineo1 - aynl - axnl * temp3);
    const double cosu = am / rl * (coseo1 - axnl + aynl * temp3);
    double su = std::atan2(sinu, cosu);
    const double sin2u = (cosu + cosu) * sinu;
    const double cos2u = 1.0 - 2.0 * sinu * sinu;
    const double temp4 = 1.0 / pl;
    const double temp1 = 0.5 * kJ2 * temp4;
    const double temp2 = temp1 * temp4;

    const double mrt = rl * (1.0 - 1.5 * temp2 * betal * con41_) + 0.5 * temp1 * x1mth2_ * cos2u;
    su = su - 0.25 * temp2 * x7thm1_ * sin2u;
    const double xnode = nodem + 1.5 * temp2 * cosim * sin2u;
    const double xinc = inclm + 1.5 * temp2 * cosim * sinim * cos2u;
    const double mvt = rdotl - nm * temp1 * x1mth2_ * sin2u / kXke;
    const double rvdot = rvdotl + nm * temp1 * (x1mth2_ * cos2u + 1.5 * con41_) / kXke;

    if (mrt < 1.0) {
        // Below one Earth radius: the object has re-entered, and any position reported from
        // here would be inside the planet.
        setError(error, Sgp4Error::Decayed);
        return std::nullopt;
    }

    // --- Orientation vectors and unit conversion ------------------------------------------
    const double sinsu = std::sin(su);
    const double cossu = std::cos(su);
    const double snod = std::sin(xnode);
    const double cnod = std::cos(xnode);
    const double sini = std::sin(xinc);
    const double cosi = std::cos(xinc);
    const double xmx = -snod * cosi;
    const double xmy = cnod * cosi;

    const Vector3 uVec{xmx * sinsu + cnod * cossu, xmy * sinsu + snod * cossu, sini * sinsu};
    const Vector3 vVec{xmx * cossu - cnod * sinsu, xmy * cossu - snod * sinsu, sini * cossu};

    // Earth radii -> km, and Earth-radii-per-minute -> km/s.
    const double velocityScale = kEarthRadiusWgs72Km * kXke / 60.0;
    StateVector state;
    state.positionKm = uVec * (mrt * kEarthRadiusWgs72Km);
    state.velocityKmPerSec = (uVec * mvt + vVec * rvdot) * velocityScale;

    if (!isFinite(state.positionKm) || !isFinite(state.velocityKmPerSec)) {
        setError(error, Sgp4Error::PropagationDiverged);
        return std::nullopt;
    }
    return state;
}

}  // namespace orca::orbital
