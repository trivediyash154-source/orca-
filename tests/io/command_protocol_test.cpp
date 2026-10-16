/// `parseCommand()` tests — Phase 9 brief §20: only commands the real `SimulationEngine`
/// supports are recognized; everything else is a named, structured error, never a crash.

#include "../../cpp/io/CommandProtocol.hpp"
#include "../../cpp/io/JsonParser.hpp"
#include "../TestFramework.hpp"

using namespace orca::io;

namespace {
Command parse(const std::string& text) { return parseCommand(parseJson(text)); }
}  // namespace

ORCA_TEST(CommandProtocol, ParsesEveryZeroArgCommand) {
    CHECK(parse(R"({"cmd":"start"})").name == CommandName::Start);
    CHECK(parse(R"({"cmd":"pause"})").name == CommandName::Pause);
    CHECK(parse(R"({"cmd":"resume"})").name == CommandName::Resume);
    CHECK(parse(R"({"cmd":"reset"})").name == CommandName::Reset);
    CHECK(parse(R"({"cmd":"step"})").name == CommandName::Step);
    CHECK(parse(R"({"cmd":"getSnapshot"})").name == CommandName::GetSnapshot);
    CHECK(parse(R"({"cmd":"shutdown"})").name == CommandName::Shutdown);
}

ORCA_TEST(CommandProtocol, ParsesAdvanceWithSeconds) {
    const Command c = parse(R"({"cmd":"advance","seconds":42.5})");
    CHECK(c.name == CommandName::Advance);
    CHECK(c.seconds.has_value());
    CHECK_NEAR(*c.seconds, 42.5, 1e-12);
}

ORCA_TEST(CommandProtocol, ParsesSetSpeedWithMultiplier) {
    const Command c = parse(R"({"cmd":"setSpeed","multiplier":4.0})");
    CHECK(c.name == CommandName::SetSpeed);
    CHECK(c.multiplier.has_value());
    CHECK_NEAR(*c.multiplier, 4.0, 1e-12);
}

ORCA_TEST(CommandProtocol, ParsesReqId) {
    const Command c = parse(R"({"cmd":"start","reqId":7})");
    CHECK(c.reqId.has_value());
    CHECK_EQ(*c.reqId, std::uint32_t{7});
}

ORCA_TEST(CommandProtocol, ReqIdIsOptional) {
    const Command c = parse(R"({"cmd":"start"})");
    CHECK(!c.reqId.has_value());
}

ORCA_TEST(CommandProtocol, AdvanceWithoutSecondsThrows) {
    CHECK_THROWS(parse(R"({"cmd":"advance"})"));
}

ORCA_TEST(CommandProtocol, SetSpeedWithoutMultiplierThrows) {
    CHECK_THROWS(parse(R"({"cmd":"setSpeed"})"));
}

ORCA_TEST(CommandProtocol, UnknownCommandThrows) { CHECK_THROWS(parse(R"({"cmd":"plan"})")); }

ORCA_TEST(CommandProtocol, MissingCmdFieldThrows) { CHECK_THROWS(parse(R"({"reqId":1})")); }

ORCA_TEST(CommandProtocol, NonObjectTopLevelThrows) { CHECK_THROWS(parse("42")); }

ORCA_TEST(CommandProtocol, WrongTypeParamThrows) {
    CHECK_THROWS(parse(R"({"cmd":"advance","seconds":"soon"})"));
}

ORCA_TEST(CommandProtocol, PlanReselectAssignGetGraphTraceAreAllUnrecognized) {
    // Explicit regression for brief §20: this project's SimulationEngine exposes no
    // planning-request surface, so these must all be rejected, never silently accepted.
    // "reselect" itself is still unrecognized -- Phase 11's real command is named
    // "rescoreParetoFront" specifically so it is never confused with this list.
    for (const char* cmd : {"plan", "reselect", "assign", "getGraph", "traceAlgorithm", "exportResults"}) {
        JsonValue obj = JsonValue::object().set("cmd", cmd);
        CHECK_THROWS(parseCommand(obj));
    }
}

ORCA_TEST(CommandProtocol, ParsesRescoreParetoFrontWithMissionIdAndProfile) {
    const Command c = parse(R"({"cmd":"rescoreParetoFront","missionId":"M-000","profile":"FASTEST"})");
    CHECK(c.name == CommandName::RescoreParetoFront);
    CHECK(c.missionId.has_value());
    CHECK_EQ(*c.missionId, std::string("M-000"));
    CHECK(c.profile.has_value());
    CHECK_EQ(*c.profile, std::string("FASTEST"));
}

ORCA_TEST(CommandProtocol, RescoreParetoFrontWithoutMissionIdThrows) {
    CHECK_THROWS(parse(R"({"cmd":"rescoreParetoFront","profile":"FASTEST"})"));
}

ORCA_TEST(CommandProtocol, RescoreParetoFrontWithoutProfileThrows) {
    CHECK_THROWS(parse(R"({"cmd":"rescoreParetoFront","missionId":"M-000"})"));
}

ORCA_TEST(CommandProtocol, ParsesRunAlgorithmWithJustAlgorithm) {
    const Command c = parse(R"({"cmd":"runAlgorithm","algorithm":"DIJKSTRA"})");
    CHECK(c.name == CommandName::RunAlgorithm);
    CHECK(c.algorithm.has_value());
    CHECK_EQ(*c.algorithm, std::string("DIJKSTRA"));
    CHECK(!c.vehicleId.has_value());
    CHECK(!c.emergencyId.has_value());
}

ORCA_TEST(CommandProtocol, ParsesRunAlgorithmWithVehicleAndEmergencyIds) {
    const Command c =
        parse(R"({"cmd":"runAlgorithm","algorithm":"PARETO","vehicleId":"RV-01","emergencyId":"E-000"})");
    CHECK(c.name == CommandName::RunAlgorithm);
    CHECK_EQ(*c.algorithm, std::string("PARETO"));
    CHECK_EQ(*c.vehicleId, std::string("RV-01"));
    CHECK_EQ(*c.emergencyId, std::string("E-000"));
}

ORCA_TEST(CommandProtocol, RunAlgorithmWithoutAlgorithmThrows) {
    CHECK_THROWS(parse(R"({"cmd":"runAlgorithm"})"));
}
