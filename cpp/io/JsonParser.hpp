#pragma once

/// @file JsonParser.hpp
/// A minimal recursive-descent JSON parser — the read-side counterpart to
/// `JsonValue::dump()`. Parses the standard grammar (object, array, string, number, bool,
/// null); it exists to read commands arriving on stdin (`CommandProtocol.hpp`) and, in
/// tests, to round-trip what `JsonValue::dump()` produces. Not a validating, standards-
/// exhaustive parser (no `\uXXXX` surrogate-pair handling, for instance) — narrow enough
/// for this project's own wire format, same scope discipline as `JsonValue.hpp`.

#include <string>

#include "JsonValue.hpp"

namespace orca::io {

/// Parses exactly one JSON value from `text`. Leading/trailing whitespace is permitted;
/// any other trailing content after the value is an error (each NDJSON line is exactly one
/// value, `API_CONTRACT.md` §1.5).
///
/// @throws JsonError on malformed input, including empty input, truncated structures, or
///         trailing garbage.
[[nodiscard]] JsonValue parseJson(const std::string& text);

}  // namespace orca::io
