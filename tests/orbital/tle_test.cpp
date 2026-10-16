/// \file
/// TLE parsing, against real element sets.
///
/// The fixtures are records CelesTrak actually served. A hand-written TLE would not exercise the
/// column alignment that makes this format hazardous — fields are allowed to run together, so a
/// parser that splits on whitespace passes a tidy fixture and fails on a real catalogue.

#include "../TestFramework.hpp"

#include <string>
#include <vector>

#include "../../cpp/orbital/Tle.hpp"

namespace {

using orca::orbital::parseTle;
using orca::orbital::parseTleCatalog;
using orca::orbital::parseTleExponential;
using orca::orbital::TleElements;
using orca::orbital::TleParseError;
using orca::orbital::tleChecksum;

constexpr const char* kIssName = "ISS (ZARYA)             ";
constexpr const char* kIssLine1 =
    "1 25544U 98067A   26250.49846501  .00005306  00000+0  10435-3 0  9991";
constexpr const char* kIssLine2 =
    "2 25544  51.6306 252.7093 0004983 115.8922 244.2580 15.49018229584512";

}  // namespace

ORCA_TEST(Tle, ParsesEveryFieldOfARealElementSet) {
    const auto e = parseTle(kIssName, kIssLine1, kIssLine2);
    CHECK(e.has_value());
    if (!e.has_value()) return;

    CHECK_EQ(e->noradId, 25544);
    CHECK(e->name == "ISS (ZARYA)");
    CHECK(e->intlDesignator == "1998-067A");
    CHECK_EQ(e->epochYear, 2026);
    CHECK_NEAR(e->epochDayOfYear, 250.49846501, 1e-8);
    CHECK_NEAR(e->inclinationDeg, 51.6306, 1e-6);
    CHECK_NEAR(e->raanDeg, 252.7093, 1e-6);
    // Assumed leading decimal point: "0004983" is 0.0004983, not 4983.
    CHECK_NEAR(e->eccentricity, 0.0004983, 1e-10);
    CHECK_NEAR(e->argPerigeeDeg, 115.8922, 1e-6);
    CHECK_NEAR(e->meanAnomalyDeg, 244.2580, 1e-6);
    CHECK_NEAR(e->meanMotionRevPerDay, 15.49018229, 1e-9);
    // "10435-3" is 0.10435e-3.
    CHECK_NEAR(e->bstar, 0.00010435, 1e-12);
    CHECK_EQ(e->revolutionAtEpoch, 58451);
}

ORCA_TEST(Tle, EpochJulianDateMatchesAnIndependentlyComputedValue) {
    // The canonical SGP4 verification object: epoch 00179.78495062 is JD 2451723.28495062.
    const char* line1 = "1 00005U 58002B   00179.78495062  .00000023  00000-0  28098-4 0  4753";
    const char* line2 = "2 00005  34.2682 348.7242 1859667 331.7664  19.3264 10.82419157413667";
    const auto e = parseTle("", line1, line2);
    CHECK(e.has_value());
    if (!e.has_value()) return;
    CHECK_EQ(e->epochYear, 2000);
    CHECK_NEAR(e->epochJulianDate(), 2451723.28495062, 1e-8);
}

ORCA_TEST(Tle, ResolvesTheTwoDigitYearAtTheFormatsOwnPivot) {
    // 57-99 are 1957-1999; 00-56 are 2000-2056. Sputnik is the reason the pivot sits there.
    const auto e = parseTle("", kIssLine1, kIssLine2);
    CHECK(e.has_value());
    if (e.has_value()) CHECK_EQ(e->epochYear, 2026);
    // The international designator uses the same pivot: "98067A" is 1998, not 2098.
    if (e.has_value()) CHECK(e->intlDesignator == "1998-067A");
}

ORCA_TEST(Tle, ComputesThePublishedChecksumForBothRealLines) {
    CHECK_EQ(tleChecksum(kIssLine1), 1);  // final digit of line 1
    CHECK_EQ(tleChecksum(kIssLine2), 2);  // final digit of line 2
}

ORCA_TEST(Tle, RejectsACorruptedLineRatherThanPropagatingGarbage) {
    // Flip a digit in the inclination: the published checksum no longer describes the line.
    std::string corrupted(kIssLine2);
    corrupted[9] = '9';
    std::string error;
    const auto e = parseTle(kIssName, kIssLine1, corrupted, &error);
    CHECK(!e.has_value());
    CHECK(error.find("checksum") != std::string::npos);
}

ORCA_TEST(Tle, RejectsLinesThatDisagreeAboutTheCatalogNumber) {
    std::string mismatched(kIssLine2);
    mismatched.replace(2, 5, "99999");
    std::string error;
    // Checksum off, so the catalog-number check is what has to catch this.
    const auto e = parseTle(kIssName, kIssLine1, mismatched, &error, false);
    CHECK(!e.has_value());
    CHECK(error.find("catalog number") != std::string::npos);
}

ORCA_TEST(Tle, DecodesImpliedDecimalExponentsInBothSigns) {
    CHECK_NEAR(parseTleExponential(" 10435-3"), 0.00010435, 1e-12);
    CHECK_NEAR(parseTleExponential("-11606-4"), -0.000011606, 1e-14);
    CHECK_NEAR(parseTleExponential(" 00000+0"), 0.0, 1e-15);
    CHECK_NEAR(parseTleExponential("        "), 0.0, 1e-15);
}

ORCA_TEST(Tle, CollectsMalformedSetsInsteadOfDiscardingTheWholeFeed) {
    std::string feed;
    feed += kIssName;
    feed += "\n";
    feed += kIssLine1;
    feed += "\n";
    feed += kIssLine2;
    feed += "\nBROKEN OBJECT\n1 not a tle\n2 not a tle\n";

    std::vector<TleParseError> errors;
    const std::vector<TleElements> parsed = parseTleCatalog(feed, &errors);
    // One good record survives, and the bad one is REPORTED rather than silently dropped:
    // returning 1 of 2 while claiming success is the quiet data loss this guards against.
    CHECK_EQ(parsed.size(), std::size_t{1});
    CHECK_EQ(errors.size(), std::size_t{1});
    if (!parsed.empty()) CHECK_EQ(parsed[0].noradId, 25544);
}

ORCA_TEST(Tle, HandlesTheThreeLineFormatsLineNumberMarker) {
    // Some feeds emit "0 ISS (ZARYA)" as the name line.
    const auto e = parseTle("0 ISS (ZARYA)", kIssLine1, kIssLine2);
    CHECK(e.has_value());
    if (e.has_value()) CHECK(e->name == "ISS (ZARYA)");
}
