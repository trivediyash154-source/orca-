/// \file
/// NORAD two-line element set parsing.
///
/// ## Why this is in C++ and not in the bridge
///
/// `docs/ARCHITECTURE.md` §6.1 makes the engine the computational source of truth, and a TLE is
/// the *input* to the only orbital computation that matters for a real catalogue. Parsing it in
/// TypeScript and shipping floats to C++ would put the authoritative numbers through a
/// re-serialisation nobody audits, and would leave the engine unable to propagate a catalogue on
/// its own — which it must be able to do, since `orca_engine` runs standalone with no Node and no
/// browser.
///
/// ## Fixed columns, not whitespace
///
/// The TLE format is a fixed-column punched-card layout. Fields are allowed to run together — a
/// three-digit RAAN leaves no space before the eccentricity — so splitting on whitespace produces
/// silently wrong values for exactly the objects whose elements are most extreme. Every field
/// below is taken at the column positions the format defines (celestrak.org/columns/v1d0/), with
/// the 1-indexed specification columns named in comments beside the 0-indexed offsets.

#ifndef ORCA_ORBITAL_TLE_HPP
#define ORCA_ORBITAL_TLE_HPP

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace orca::orbital {

/// One parsed element set. Angles are degrees and mean motion is revolutions per day — the
/// units the TLE itself carries. `Sgp4` converts to radians and radians-per-minute internally,
/// so no caller has to remember which convention it is holding.
struct TleElements {
    int noradId = 0;
    std::string name;            ///< From the preceding name line; empty for a bare 2LE.
    std::string intlDesignator;  ///< COSPAR form, e.g. "1998-067A".
    int epochYear = 0;           ///< Four-digit, already resolved from the two-digit field.
    double epochDayOfYear = 0.0; ///< Fractional day, 1-based (day 1.0 is Jan 1 00:00 UTC).
    double inclinationDeg = 0.0;
    double raanDeg = 0.0;
    double eccentricity = 0.0;
    double argPerigeeDeg = 0.0;
    double meanAnomalyDeg = 0.0;
    double meanMotionRevPerDay = 0.0;
    double bstar = 0.0;                ///< Drag term, earth-radii^-1.
    double meanMotionDot = 0.0;        ///< rev/day^2 / 2, as published.
    double meanMotionDdot = 0.0;       ///< rev/day^3 / 6, as published.
    int revolutionAtEpoch = 0;

    /// Julian date of the element-set epoch. SGP4 works in minutes from epoch, so this is what
    /// turns a wall-clock instant into that offset.
    [[nodiscard]] double epochJulianDate() const noexcept;
};

/// Why a line was rejected. Returned rather than thrown: a 16,000-object feed with one corrupt
/// record should lose that record, not the feed, and the caller needs to be able to report how
/// many were lost instead of silently returning fewer objects than it claimed.
struct TleParseError {
    std::string reason;
    std::string line;
};

/// Sum modulo 10 of every digit in columns 1-68, counting each minus sign as 1. The only
/// integrity check the format offers against a corrupted transfer.
[[nodiscard]] int tleChecksum(std::string_view line) noexcept;

/// Decodes the implied-decimal exponent notation, e.g. " 10435-3" -> 0.10435e-3.
[[nodiscard]] double parseTleExponential(std::string_view field) noexcept;

/// Parses one element set. `verifyChecksum` defaults on; a mismatch is a rejection, because a
/// garbled element set propagates to a confident, wrong position.
[[nodiscard]] std::optional<TleElements> parseTle(std::string_view nameLine,
                                                  std::string_view line1,
                                                  std::string_view line2,
                                                  std::string* error = nullptr,
                                                  bool verifyChecksum = true);

/// Parses a whole three-line-per-object feed. Malformed sets are collected into `errors` rather
/// than aborting the parse.
[[nodiscard]] std::vector<TleElements> parseTleCatalog(std::string_view text,
                                                       std::vector<TleParseError>* errors = nullptr,
                                                       bool verifyChecksum = true);

}  // namespace orca::orbital

#endif  // ORCA_ORBITAL_TLE_HPP
