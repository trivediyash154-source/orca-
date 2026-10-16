/// `JsonValue` writer tests — Phase 9 brief §16-17: valid JSON, numeric fidelity, string
/// escaping, empty collections, and the "floats are finite" rule.

#include <limits>

#include "../../cpp/io/JsonValue.hpp"
#include "../TestFramework.hpp"

using namespace orca::io;

ORCA_TEST(JsonValue, PrimitivesDumpCorrectly) {
    CHECK_EQ(JsonValue(nullptr).dump(), std::string("null"));
    CHECK_EQ(JsonValue(true).dump(), std::string("true"));
    CHECK_EQ(JsonValue(false).dump(), std::string("false"));
    CHECK_EQ(JsonValue("hi").dump(), std::string("\"hi\""));
}

ORCA_TEST(JsonValue, IntegralNumbersDumpWithoutATrailingFraction) {
    CHECK_EQ(JsonValue(5.0).dump(), std::string("5"));
    CHECK_EQ(JsonValue(-3.0).dump(), std::string("-3"));
    CHECK_EQ(JsonValue(0.0).dump(), std::string("0"));
}

ORCA_TEST(JsonValue, FractionalNumbersDumpWithTheirFraction) {
    const std::string dumped = JsonValue(3.5).dump();
    CHECK_EQ(dumped, std::string("3.5"));
}

ORCA_TEST(JsonValue, StringEscapingCoversTheStandardCases) {
    CHECK_EQ(JsonValue(std::string("a\"b")).dump(), std::string("\"a\\\"b\""));
    CHECK_EQ(JsonValue(std::string("a\\b")).dump(), std::string("\"a\\\\b\""));
    CHECK_EQ(JsonValue(std::string("a\nb")).dump(), std::string("\"a\\nb\""));
    CHECK_EQ(JsonValue(std::string("a\tb")).dump(), std::string("\"a\\tb\""));
}

ORCA_TEST(JsonValue, EmptyArrayAndObjectDumpCorrectly) {
    CHECK_EQ(JsonValue::array().dump(), std::string("[]"));
    CHECK_EQ(JsonValue::object().dump(), std::string("{}"));
}

ORCA_TEST(JsonValue, ObjectFieldOrderIsPreservationOfInsertionOrder) {
    JsonValue obj = JsonValue::object().set("z", 1).set("a", 2).set("m", 3);
    CHECK_EQ(obj.dump(), std::string("{\"z\":1,\"a\":2,\"m\":3}"));
}

ORCA_TEST(JsonValue, ArrayOfObjectsDumpsAsOneCompactLine) {
    JsonValue arr = JsonValue::array();
    arr.push(JsonValue::object().set("id", "SAT-001"));
    arr.push(JsonValue::object().set("id", "SAT-002"));
    const std::string dumped = arr.dump();
    CHECK_EQ(dumped, std::string("[{\"id\":\"SAT-001\"},{\"id\":\"SAT-002\"}]"));
    CHECK(dumped.find('\n') == std::string::npos);  // NDJSON: no embedded newlines
}

ORCA_TEST(JsonValue, NestedObjectsAndArraysDumpCorrectly) {
    JsonValue root = JsonValue::object();
    JsonValue pos = JsonValue::object().set("x", 1.5).set("y", -2.5).set("z", 0.0);
    root.set("position", std::move(pos));
    JsonValue tags = JsonValue::array().push("a").push("b");
    root.set("tags", std::move(tags));
    CHECK_EQ(root.dump(),
            std::string("{\"position\":{\"x\":1.5,\"y\":-2.5,\"z\":0},\"tags\":[\"a\",\"b\"]}"));
}

ORCA_TEST(JsonValue, DumpingNaNThrows) {
    const JsonValue v(std::numeric_limits<double>::quiet_NaN());
    CHECK_THROWS(v.dump());
}

ORCA_TEST(JsonValue, DumpingInfinityThrows) {
    const JsonValue v(std::numeric_limits<double>::infinity());
    CHECK_THROWS(v.dump());
}

ORCA_TEST(JsonValue, NonFiniteInsideANestedStructureStillThrows) {
    JsonValue root = JsonValue::object().set(
        "bad", JsonValue(std::numeric_limits<double>::quiet_NaN()));
    CHECK_THROWS(root.dump());
}

ORCA_TEST(JsonValue, TypedAccessorsRoundTrip) {
    const JsonValue obj = JsonValue::object().set("n", 42).set("s", "x").set("b", true);
    CHECK_NEAR(obj.find("n")->asNumber(), 42.0, 1e-12);
    CHECK_EQ(obj.find("s")->asString(), std::string("x"));
    CHECK(obj.find("b")->asBool());
    CHECK(obj.find("missing") == nullptr);
}

ORCA_TEST(JsonValue, WrongTypeAccessThrows) {
    const JsonValue v(5.0);
    CHECK_THROWS(v.asString());
    CHECK_THROWS(v.asBool());
    CHECK_THROWS(v.asArray());
    CHECK_THROWS(v.asObject());
}

ORCA_TEST(JsonValue, SetOnNonObjectThrows) {
    JsonValue v(5.0);
    CHECK_THROWS(v.set("x", 1));
}

ORCA_TEST(JsonValue, PushOnNonArrayThrows) {
    JsonValue v(5.0);
    CHECK_THROWS(v.push(1));
}
