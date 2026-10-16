#include "Tle.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace orca::orbital {
namespace {

/// Trims ASCII whitespace from both ends. TLE fields are space-padded by construction.
[[nodiscard]] std::string_view trim(std::string_view s) noexcept {
    std::size_t begin = 0;
    while (begin < s.size() && (std::isspace(static_cast<unsigned char>(s[begin])) != 0)) {
        ++begin;
    }
    std::size_t end = s.size();
    while (end > begin && (std::isspace(static_cast<unsigned char>(s[end - 1])) != 0)) {
        --end;
    }
    return s.substr(begin, end - begin);
}

/// Parses a fixed-column numeric field. Returns false rather than throwing, so one bad field
/// rejects one object instead of unwinding a whole catalogue parse.
[[nodiscard]] bool parseDouble(std::string_view field, double& out) noexcept {
    const std::string_view t = trim(field);
    if (t.empty()) return false;
    const std::string owned(t);
    char* end = nullptr;
    const double value = std::strtod(owned.c_str(), &end);
    if (end == owned.c_str() || *end != '\0') return false;
    if (!std::isfinite(value)) return false;
    out = value;
    return true;
}

[[nodiscard]] bool parseInt(std::string_view field, int& out) noexcept {
    double asDouble = 0.0;
    if (!parseDouble(field, asDouble)) return false;
    out = static_cast<int>(asDouble);
    return true;
}

/// Safe fixed-column slice: a short line yields an empty view rather than undefined behaviour.
[[nodiscard]] std::string_view column(std::string_view line, std::size_t from, std::size_t to) noexcept {
    if (from >= line.size()) return {};
    return line.substr(from, std::min(to, line.size()) - from);
}

/// The format's own two-digit-year rule: 57-99 are 1957-1999 (Sputnik onward), 00-56 are
/// 2000-2056. Applies to both the epoch and the international designator.
[[nodiscard]] int resolveTwoDigitYear(int yy) noexcept {
    return yy >= 57 ? 1900 + yy : 2000 + yy;
}

/// "98067A" -> "1998-067A".
[[nodiscard]] std::string formatIntlDesignator(std::string_view raw) {
    const std::string_view t = trim(raw);
    if (t.size() < 2) return std::string(t);
    int yy = 0;
    if (!parseInt(t.substr(0, 2), yy)) return std::string(t);
    return std::to_string(resolveTwoDigitYear(yy)) + "-" + std::string(t.substr(2));
}

void setError(std::string* error, std::string message) {
    if (error != nullptr) *error = std::move(message);
}

}  // namespace

double TleElements::epochJulianDate() const noexcept {
    // Julian date of January 0.0 of the epoch year, by the standard civil-calendar reduction.
    // Using year-1/month-13 folds January and February into the previous year, which is what
    // makes the Gregorian correction terms below uniform.
    const int y = epochYear - 1;
    const double a = std::floor(static_cast<double>(y) / 100.0);
    const double b = 2.0 - a + std::floor(a / 4.0);
    const double jan0 = std::floor(365.25 * static_cast<double>(y)) +
                        std::floor(30.6001 * 14.0) + 1720994.5 + b;
    return jan0 + epochDayOfYear;
}

int tleChecksum(std::string_view line) noexcept {
    int sum = 0;
    const std::size_t limit = std::min<std::size_t>(68, line.size());
    for (std::size_t i = 0; i < limit; ++i) {
        const char c = line[i];
        if (c >= '0' && c <= '9') {
            sum += c - '0';
        } else if (c == '-') {
            sum += 1;
        }
    }
    return sum % 10;
}

double parseTleExponential(std::string_view field) noexcept {
    const std::string_view t = trim(field);
    if (t.empty()) return 0.0;

    // Mantissa carries an assumed leading decimal point; the final two characters are a signed
    // base-10 exponent. " 10435-3" is 0.10435e-3, not 10435 - 3.
    std::size_t i = 0;
    double sign = 1.0;
    if (t[i] == '+' || t[i] == '-') {
        if (t[i] == '-') sign = -1.0;
        ++i;
    }
    std::string mantissa = "0.";
    while (i < t.size() && (std::isdigit(static_cast<unsigned char>(t[i])) != 0)) {
        mantissa.push_back(t[i]);
        ++i;
    }
    if (mantissa.size() == 2) return 0.0;  // no digits at all

    int exponent = 0;
    if (i < t.size() && (t[i] == '+' || t[i] == '-')) {
        const double expSign = (t[i] == '-') ? -1.0 : 1.0;
        ++i;
        int digits = 0;
        while (i < t.size() && (std::isdigit(static_cast<unsigned char>(t[i])) != 0)) {
            digits = digits * 10 + (t[i] - '0');
            ++i;
        }
        exponent = static_cast<int>(expSign * static_cast<double>(digits));
    }

    double value = 0.0;
    if (!parseDouble(mantissa, value)) return 0.0;
    return sign * value * std::pow(10.0, static_cast<double>(exponent));
}

std::optional<TleElements> parseTle(std::string_view nameLine,
                                    std::string_view line1,
                                    std::string_view line2,
                                    std::string* error,
                                    bool verifyChecksum) {
    if (line1.size() < 68 || line1.substr(0, 2) != "1 ") {
        setError(error, "line 1 is not a TLE first line");
        return std::nullopt;
    }
    if (line2.size() < 68 || line2.substr(0, 2) != "2 ") {
        setError(error, "line 2 is not a TLE second line");
        return std::nullopt;
    }

    if (verifyChecksum) {
        if (line1.size() > 68 && (std::isdigit(static_cast<unsigned char>(line1[68])) != 0) &&
            tleChecksum(line1) != line1[68] - '0') {
            setError(error, "line 1 checksum mismatch");
            return std::nullopt;
        }
        if (line2.size() > 68 && (std::isdigit(static_cast<unsigned char>(line2[68])) != 0) &&
            tleChecksum(line2) != line2[68] - '0') {
            setError(error, "line 2 checksum mismatch");
            return std::nullopt;
        }
    }

    TleElements e;
    int noradFromLine2 = 0;
    int epochYearTwoDigit = 0;

    const bool ok =
        parseInt(column(line1, 2, 7), e.noradId) &&              // cols 3-7
        parseInt(column(line2, 2, 7), noradFromLine2) &&         // cols 3-7
        parseInt(column(line1, 18, 20), epochYearTwoDigit) &&    // cols 19-20
        parseDouble(column(line1, 20, 32), e.epochDayOfYear) &&  // cols 21-32
        parseDouble(column(line2, 8, 16), e.inclinationDeg) &&   // cols 9-16
        parseDouble(column(line2, 17, 25), e.raanDeg) &&         // cols 18-25
        parseDouble(column(line2, 34, 42), e.argPerigeeDeg) &&   // cols 35-42
        parseDouble(column(line2, 43, 51), e.meanAnomalyDeg) &&  // cols 44-51
        parseDouble(column(line2, 52, 63), e.meanMotionRevPerDay);  // cols 53-63

    if (!ok) {
        setError(error, "a numeric field did not parse");
        return std::nullopt;
    }
    if (e.noradId != noradFromLine2) {
        setError(error, "the two lines disagree about the catalog number");
        return std::nullopt;
    }

    // Eccentricity has an assumed leading decimal point: "0004983" is 0.0004983.
    double eccentricity = 0.0;
    const std::string eccField = "0." + std::string(trim(column(line2, 26, 33)));  // cols 27-33
    if (!parseDouble(eccField, eccentricity)) {
        setError(error, "eccentricity did not parse");
        return std::nullopt;
    }
    e.eccentricity = eccentricity;

    e.epochYear = resolveTwoDigitYear(epochYearTwoDigit);
    e.intlDesignator = formatIntlDesignator(column(line1, 9, 17));  // cols 10-17
    e.bstar = parseTleExponential(column(line1, 53, 61));           // cols 54-61
    e.meanMotionDdot = parseTleExponential(column(line1, 44, 52));  // cols 45-52
    (void)parseDouble(column(line1, 33, 43), e.meanMotionDot);      // cols 34-43, plain decimal
    (void)parseInt(column(line2, 63, 68), e.revolutionAtEpoch);     // cols 64-68

    // A 3LE's name line may carry a "0 " line-number marker.
    std::string_view trimmedName = trim(nameLine);
    if (trimmedName.size() > 2 && trimmedName.substr(0, 2) == "0 ") {
        trimmedName = trim(trimmedName.substr(2));
    }
    e.name = std::string(trimmedName);

    return e;
}

std::vector<TleElements> parseTleCatalog(std::string_view text,
                                         std::vector<TleParseError>* errors,
                                         bool verifyChecksum) {
    std::vector<TleElements> out;
    std::vector<std::string_view> lines;
    lines.reserve(64);

    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t newline = text.find('\n', start);
        const std::size_t end = (newline == std::string_view::npos) ? text.size() : newline;
        std::string_view line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (!trim(line).empty()) lines.push_back(line);
        if (newline == std::string_view::npos) break;
        start = newline + 1;
    }

    for (std::size_t i = 0; i + 2 < lines.size() + 1; i += 3) {
        if (i + 2 >= lines.size()) break;
        std::string error;
        std::optional<TleElements> parsed =
            parseTle(lines[i], lines[i + 1], lines[i + 2], &error, verifyChecksum);
        if (parsed.has_value()) {
            out.push_back(std::move(*parsed));
        } else if (errors != nullptr) {
            errors->push_back(TleParseError{std::move(error), std::string(lines[i])});
        }
    }
    return out;
}

}  // namespace orca::orbital
