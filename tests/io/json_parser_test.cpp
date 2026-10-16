/// `parseJson()` tests — Phase 9 brief §50: fragmented/malformed input handling starts at
/// this layer (the Node-side stream parser, `backend/`, is a separate concern layered on
/// top of whatever one complete line contains; this file only tests that one complete line
/// parses correctly, or fails loudly rather than silently).

#include "../../cpp/io/JsonParser.hpp"
#include "../TestFramework.hpp"

using namespace orca::io;

ORCA_TEST(JsonParser, ParsesPrimitives) {
    CHECK(parseJson("null").isNull());
    CHECK(parseJson("true").asBool());
    CHECK(!parseJson("false").asBool());
    CHECK_NEAR(parseJson("42").asNumber(), 42.0, 1e-12);
    CHECK_NEAR(parseJson("-3.5").asNumber(), -3.5, 1e-12);
    CHECK_NEAR(parseJson("1.5e2").asNumber(), 150.0, 1e-9);
    CHECK_EQ(parseJson("\"hello\"").asString(), std::string("hello"));
}

ORCA_TEST(JsonParser, ParsesEscapedStrings) {
    CHECK_EQ(parseJson(R"("a\"b")").asString(), std::string("a\"b"));
    CHECK_EQ(parseJson(R"("a\\b")").asString(), std::string("a\\b"));
    CHECK_EQ(parseJson(R"("a\nb")").asString(), std::string("a\nb"));
}

ORCA_TEST(JsonParser, ParsesEmptyArrayAndObject) {
    CHECK(parseJson("[]").asArray().empty());
    CHECK(parseJson("{}").asObject().empty());
}

ORCA_TEST(JsonParser, ParsesNestedStructures) {
    const JsonValue v = parseJson(R"({"cmd":"advance","seconds":10.5,"tags":["a","b"]})");
    CHECK_EQ(v.find("cmd")->asString(), std::string("advance"));
    CHECK_NEAR(v.find("seconds")->asNumber(), 10.5, 1e-12);
    CHECK_EQ(v.find("tags")->asArray().size(), std::size_t{2});
}

ORCA_TEST(JsonParser, WhitespaceAroundTheValueIsTolerated) {
    CHECK_NEAR(parseJson("  \n 42 \t ").asNumber(), 42.0, 1e-12);
}

ORCA_TEST(JsonParser, EmptyInputThrows) { CHECK_THROWS(parseJson("")); }

ORCA_TEST(JsonParser, TruncatedObjectThrows) { CHECK_THROWS(parseJson(R"({"cmd":"start")")); }

ORCA_TEST(JsonParser, TrailingGarbageAfterAValidValueThrows) {
    CHECK_THROWS(parseJson(R"({"cmd":"start"} garbage)"));
}

ORCA_TEST(JsonParser, UnquotedKeyThrows) { CHECK_THROWS(parseJson("{cmd:1}")); }

ORCA_TEST(JsonParser, InvalidLiteralThrows) { CHECK_THROWS(parseJson("nul")); }

ORCA_TEST(JsonParser, InvalidNumberThrows) { CHECK_THROWS(parseJson("-")); }

ORCA_TEST(JsonParser, RoundTripsWhatJsonValueDumps) {
    const JsonValue original = JsonValue::object()
                                   .set("id", "SAT-001")
                                   .set("count", 5)
                                   .set("ok", true)
                                   .set("nothing", nullptr)
                                   .set("list", JsonValue::array().push(1).push(2).push(3));
    const std::string dumped = original.dump();
    const JsonValue reparsed = parseJson(dumped);
    CHECK_EQ(reparsed.find("id")->asString(), std::string("SAT-001"));
    CHECK_NEAR(reparsed.find("count")->asNumber(), 5.0, 1e-12);
    CHECK(reparsed.find("ok")->asBool());
    CHECK(reparsed.find("nothing")->isNull());
    CHECK_EQ(reparsed.find("list")->asArray().size(), std::size_t{3});
}
