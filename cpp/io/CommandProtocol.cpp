#include "CommandProtocol.hpp"

namespace orca::io {

std::string toString(CommandName name) noexcept {
    switch (name) {
        case CommandName::Start: return "start";
        case CommandName::Pause: return "pause";
        case CommandName::Resume: return "resume";
        case CommandName::Reset: return "reset";
        case CommandName::Step: return "step";
        case CommandName::Advance: return "advance";
        case CommandName::SetSpeed: return "setSpeed";
        case CommandName::GetSnapshot: return "getSnapshot";
        case CommandName::Shutdown: return "shutdown";
        case CommandName::RescoreParetoFront: return "rescoreParetoFront";
        case CommandName::RunAlgorithm: return "runAlgorithm";
    }
    return "unknown";
}

namespace {

std::optional<std::uint32_t> readReqId(const JsonValue& json) {
    const JsonValue* v = json.find("reqId");
    if (v == nullptr) return std::nullopt;
    if (!v->isNumber()) throw CommandError("INVALID_PARAMS", "reqId must be a number");
    return static_cast<std::uint32_t>(v->asNumber());
}

double readRequiredNumber(const JsonValue& json, const std::string& field) {
    const JsonValue* v = json.find(field);
    if (v == nullptr) {
        throw CommandError("INVALID_PARAMS", "missing required field '" + field + "'");
    }
    if (!v->isNumber()) {
        throw CommandError("INVALID_PARAMS", "field '" + field + "' must be a number");
    }
    return v->asNumber();
}

std::string readRequiredString(const JsonValue& json, const std::string& field) {
    const JsonValue* v = json.find(field);
    if (v == nullptr) {
        throw CommandError("INVALID_PARAMS", "missing required field '" + field + "'");
    }
    if (!v->isString()) {
        throw CommandError("INVALID_PARAMS", "field '" + field + "' must be a string");
    }
    return v->asString();
}

std::optional<std::string> readOptionalString(const JsonValue& json, const std::string& field) {
    const JsonValue* v = json.find(field);
    if (v == nullptr) return std::nullopt;
    if (!v->isString()) {
        throw CommandError("INVALID_PARAMS", "field '" + field + "' must be a string");
    }
    return v->asString();
}

}  // namespace

Command parseCommand(const JsonValue& json) {
    if (!json.isObject()) {
        throw CommandError("MALFORMED_JSON", "command must be a JSON object");
    }
    const JsonValue* cmdField = json.find("cmd");
    if (cmdField == nullptr || !cmdField->isString()) {
        throw CommandError("UNKNOWN_COMMAND", "missing or non-string 'cmd' field");
    }
    const std::string& cmd = cmdField->asString();
    const std::optional<std::uint32_t> reqId = readReqId(json);

    if (cmd == "start") return Command{.name = CommandName::Start, .reqId = reqId};
    if (cmd == "pause") return Command{.name = CommandName::Pause, .reqId = reqId};
    if (cmd == "resume") return Command{.name = CommandName::Resume, .reqId = reqId};
    if (cmd == "reset") return Command{.name = CommandName::Reset, .reqId = reqId};
    if (cmd == "step") return Command{.name = CommandName::Step, .reqId = reqId};
    if (cmd == "getSnapshot") return Command{.name = CommandName::GetSnapshot, .reqId = reqId};
    if (cmd == "shutdown") return Command{.name = CommandName::Shutdown, .reqId = reqId};
    if (cmd == "advance") {
        return Command{
            .name = CommandName::Advance, .reqId = reqId, .seconds = readRequiredNumber(json, "seconds")};
    }
    if (cmd == "setSpeed") {
        return Command{.name = CommandName::SetSpeed,
                       .reqId = reqId,
                       .multiplier = readRequiredNumber(json, "multiplier")};
    }
    if (cmd == "rescoreParetoFront") {
        return Command{.name = CommandName::RescoreParetoFront,
                       .reqId = reqId,
                       .missionId = readRequiredString(json, "missionId"),
                       .profile = readRequiredString(json, "profile")};
    }
    if (cmd == "runAlgorithm") {
        return Command{.name = CommandName::RunAlgorithm,
                       .reqId = reqId,
                       .algorithm = readRequiredString(json, "algorithm"),
                       .vehicleId = readOptionalString(json, "vehicleId"),
                       .emergencyId = readOptionalString(json, "emergencyId")};
    }
    throw CommandError("UNKNOWN_COMMAND", "unrecognized command '" + cmd + "'");
}

}  // namespace orca::io
