#pragma once

/// @file Validation.hpp
/// Construction-time invariant checking for domain entities.
///
/// ORCA distinguishes two kinds of "impossible state", and handles them differently:
///
///   * **Bad input crossing a boundary** — a scenario file with a negative altitude, an
///     API request with a survival window of zero. These are expected to happen, they are
///     the user's fault rather than the program's, and the caller needs a message naming
///     the offending field. These throw `std::invalid_argument` via `requireThat`.
///
///   * **Broken internal logic** — an adjacency list whose size no longer matches the
///     node count. These are the program's fault, cannot be handled meaningfully at
///     runtime, and should abort loudly in debug. These use `assert`.
///
/// The distinction matters because it keeps ORCA from becoming exception-heavy. Exceptions
/// appear at construction and at the io/ boundary; the simulation loop and the algorithms
/// contain none, because by the time data reaches them it has already been validated.

#include <stdexcept>
#include <string>

namespace orca {

/// Throw `std::invalid_argument` with `message` when `condition` is false.
///
/// Messages should name the field and the offending value, because they surface directly
/// in the UI (`"Scenario invalid: satellites[3].altitude missing"`, ARCHITECTURE.md §11).
inline void requireThat(bool condition, const std::string& message) {
    if (!condition) {
        throw std::invalid_argument(message);
    }
}

}  // namespace orca
