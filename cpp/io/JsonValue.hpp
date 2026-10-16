#pragma once

/// @file JsonValue.hpp
/// A minimal JSON value tree — Phase 9 brief §16: "select a lightweight C++ JSON solution
/// appropriate to the project." No external JSON library is vendored (the project's
/// established pattern is to build small, verifiable pieces from scratch rather than adopt
/// an unverified dependency — `RandomGenerator` avoiding `std::*_distribution`,
/// `AssignmentSolver` implementing Hungarian directly, are the same choice made earlier).
/// This is deliberately narrow: enough to represent every shape `ViewProjection.hpp`
/// produces and every command `CommandProtocol.hpp` parses, not a general-purpose library.
///
/// ## Object field order is preserved
///
/// `Object` is `std::vector<std::pair<std::string, JsonValue>>`, not a `std::map` — so a
/// snapshot's fields serialize in the order they were built, matching what a human reading
/// captured NDJSON expects, and matching `API_CONTRACT.md`'s worked examples field-for-field.

#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace orca::io {

class JsonValue;

using JsonArray = std::vector<JsonValue>;
using JsonObject = std::vector<std::pair<std::string, JsonValue>>;

/// Thrown by `dump()` on a non-finite number, and by the parser (`JsonParser.hpp`) on
/// malformed input. One exception type for both directions of this narrow library --
/// `std::invalid_argument`, matching the project-wide convention every `requireThat()`
/// call already established (`core/Validation.hpp`), not a new hierarchy of its own.
class JsonError : public std::invalid_argument {
public:
    explicit JsonError(const std::string& message) : std::invalid_argument(message) {}
};

class JsonValue {
public:
    JsonValue() noexcept : data_(nullptr) {}
    JsonValue(std::nullptr_t) noexcept : data_(nullptr) {}
    JsonValue(bool b) noexcept : data_(b) {}
    JsonValue(double n) noexcept : data_(n) {}
    JsonValue(int n) noexcept : data_(static_cast<double>(n)) {}
    JsonValue(long n) noexcept : data_(static_cast<double>(n)) {}
    JsonValue(std::uint32_t n) noexcept : data_(static_cast<double>(n)) {}
    JsonValue(std::string s) noexcept : data_(std::move(s)) {}
    JsonValue(const char* s) : data_(std::string(s)) {}
    JsonValue(JsonArray a) noexcept : data_(std::move(a)) {}
    JsonValue(JsonObject o) noexcept : data_(std::move(o)) {}

    [[nodiscard]] static JsonValue object() noexcept { return JsonValue(JsonObject{}); }
    [[nodiscard]] static JsonValue array() noexcept { return JsonValue(JsonArray{}); }

    /// Appends `{key, value}` to an object value. @throws JsonError if this value is not
    /// an object (constructed via `object()`).
    JsonValue& set(std::string key, JsonValue value);

    /// Appends `value` to an array value. @throws JsonError if this value is not an array.
    JsonValue& push(JsonValue value);

    [[nodiscard]] bool isNull() const noexcept { return std::holds_alternative<std::nullptr_t>(data_); }
    [[nodiscard]] bool isBool() const noexcept { return std::holds_alternative<bool>(data_); }
    [[nodiscard]] bool isNumber() const noexcept { return std::holds_alternative<double>(data_); }
    [[nodiscard]] bool isString() const noexcept { return std::holds_alternative<std::string>(data_); }
    [[nodiscard]] bool isArray() const noexcept { return std::holds_alternative<JsonArray>(data_); }
    [[nodiscard]] bool isObject() const noexcept { return std::holds_alternative<JsonObject>(data_); }

    /// @throws JsonError if this value is not the requested type.
    [[nodiscard]] bool asBool() const;
    [[nodiscard]] double asNumber() const;
    [[nodiscard]] const std::string& asString() const;
    [[nodiscard]] const JsonArray& asArray() const;
    [[nodiscard]] const JsonObject& asObject() const;

    /// Looks up `key` in an object value. Returns `nullptr` if absent or this value is not
    /// an object -- the same "absence is a normal question, not an error" convention
    /// `Scenario::findX` already established.
    [[nodiscard]] const JsonValue* find(const std::string& key) const noexcept;

    /// Serializes to compact, single-line JSON (no embedded newlines, no pretty-printing —
    /// the NDJSON framing rule, `API_CONTRACT.md` §1.5).
    ///
    /// @throws JsonError if any number in the tree is non-finite (NaN/Infinity) —
    /// `API_CONTRACT.md` §1.7's "floats are finite" rule, enforced here rather than trusted
    /// of every caller.
    [[nodiscard]] std::string dump() const;

private:
    void dumpTo(std::string& out) const;

    std::variant<std::nullptr_t, bool, double, std::string, JsonArray, JsonObject> data_;
};

}  // namespace orca::io
