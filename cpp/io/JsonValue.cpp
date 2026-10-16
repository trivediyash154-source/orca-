#include "JsonValue.hpp"

#include <cmath>
#include <format>

namespace orca::io {

JsonValue& JsonValue::set(std::string key, JsonValue value) {
    if (!isObject()) {
        throw JsonError("JsonValue::set: value is not an object");
    }
    std::get<JsonObject>(data_).emplace_back(std::move(key), std::move(value));
    return *this;
}

JsonValue& JsonValue::push(JsonValue value) {
    if (!isArray()) {
        throw JsonError("JsonValue::push: value is not an array");
    }
    std::get<JsonArray>(data_).push_back(std::move(value));
    return *this;
}

bool JsonValue::asBool() const {
    if (!isBool()) throw JsonError("JsonValue::asBool: value is not a bool");
    return std::get<bool>(data_);
}

double JsonValue::asNumber() const {
    if (!isNumber()) throw JsonError("JsonValue::asNumber: value is not a number");
    return std::get<double>(data_);
}

const std::string& JsonValue::asString() const {
    if (!isString()) throw JsonError("JsonValue::asString: value is not a string");
    return std::get<std::string>(data_);
}

const JsonArray& JsonValue::asArray() const {
    if (!isArray()) throw JsonError("JsonValue::asArray: value is not an array");
    return std::get<JsonArray>(data_);
}

const JsonObject& JsonValue::asObject() const {
    if (!isObject()) throw JsonError("JsonValue::asObject: value is not an object");
    return std::get<JsonObject>(data_);
}

const JsonValue* JsonValue::find(const std::string& key) const noexcept {
    if (!isObject()) {
        return nullptr;
    }
    for (const auto& [k, v] : std::get<JsonObject>(data_)) {
        if (k == key) {
            return &v;
        }
    }
    return nullptr;
}

namespace {
void appendEscaped(std::string& out, const std::string& s) {
    out.push_back('"');
    for (const char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    out += std::format("\\u{:04x}", static_cast<unsigned>(c));
                } else {
                    out.push_back(c);
                }
        }
    }
    out.push_back('"');
}
}  // namespace

void JsonValue::dumpTo(std::string& out) const {
    if (std::holds_alternative<std::nullptr_t>(data_)) {
        out += "null";
    } else if (std::holds_alternative<bool>(data_)) {
        out += std::get<bool>(data_) ? "true" : "false";
    } else if (std::holds_alternative<double>(data_)) {
        const double n = std::get<double>(data_);
        if (!std::isfinite(n)) {
            throw JsonError(
                "JsonValue::dump: refusing to serialize a non-finite number "
                "(API_CONTRACT.md #1.7)");
        }
        // Integral values print without a trailing ".0" fraction's noise-free form
        // (e.g. "5" not "5.000000") while still round-tripping fractional values exactly
        // enough for this project's purposes -- IDs and counts are the common integral
        // case, and this keeps captured NDJSON readable.
        if (n == std::floor(n) && std::fabs(n) < 1.0e15) {
            out += std::format("{}", static_cast<long long>(n));
        } else {
            out += std::format("{}", n);
        }
    } else if (std::holds_alternative<std::string>(data_)) {
        appendEscaped(out, std::get<std::string>(data_));
    } else if (std::holds_alternative<JsonArray>(data_)) {
        out.push_back('[');
        const auto& arr = std::get<JsonArray>(data_);
        for (std::size_t i = 0; i < arr.size(); ++i) {
            if (i > 0) out.push_back(',');
            arr[i].dumpTo(out);
        }
        out.push_back(']');
    } else {
        out.push_back('{');
        const auto& obj = std::get<JsonObject>(data_);
        for (std::size_t i = 0; i < obj.size(); ++i) {
            if (i > 0) out.push_back(',');
            appendEscaped(out, obj[i].first);
            out.push_back(':');
            obj[i].second.dumpTo(out);
        }
        out.push_back('}');
    }
}

std::string JsonValue::dump() const {
    std::string out;
    dumpTo(out);
    return out;
}

}  // namespace orca::io
