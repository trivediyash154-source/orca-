#pragma once

/// @file TestFramework.hpp
/// A minimal test harness.
///
/// ## Why this exists rather than GoogleTest
///
/// Phase 0 specified GoogleTest via CMake `FetchContent`. CMake is not installed on this
/// machine (REPOSITORY_AUDIT.md §3), so that path cannot run today. The choice was
/// between writing GoogleTest files that cannot be executed, or writing tests that
/// actually run.
///
/// Tests that run are worth more than tests that are merely written, so this harness
/// exists to make Phase 1 verifiable now, with nothing but the system compiler. It is
/// ~90 lines and deliberately not a framework: no fixtures, no mocks, no parameterised
/// suites, no death tests.
///
/// ## Migration
///
/// When CMake and GoogleTest are available, migration is mechanical:
///
///     ORCA_TEST(Suite, Name)      -> TEST(Suite, Name)
///     CHECK(x)                    -> EXPECT_TRUE(x)
///     CHECK_EQ(a, b)              -> EXPECT_EQ(a, b)
///     CHECK_NEAR(a, b, tol)       -> EXPECT_NEAR(a, b, tol)
///     CHECK_THROWS(expr)          -> EXPECT_THROW(expr, std::invalid_argument)
///
/// The test *bodies* — which is where the actual verification lives — do not change.
/// This harness is scaffolding around them, not a dependency of them.

#include <cmath>
#include <cstdio>
#include <exception>
#include <string>
#include <string_view>
#include <vector>

namespace orca::test {

struct TestCase {
    std::string suite;
    std::string name;
    void (*fn)();
};

/// Registry of all tests. Function-local static so registration order across translation
/// units is well-defined regardless of static initialisation order.
inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

/// Failures recorded by the currently-running test.
inline std::vector<std::string>& currentFailures() {
    static std::vector<std::string> failures;
    return failures;
}

struct Registrar {
    Registrar(std::string_view suite, std::string_view name, void (*fn)()) {
        registry().push_back(TestCase{std::string{suite}, std::string{name}, fn});
    }
};

inline void recordFailure(const char* file, int line, const std::string& message) {
    currentFailures().push_back(std::string{file} + ":" + std::to_string(line) + "  " +
                                message);
}

/// Run every registered test. Returns the process exit code.
inline int runAll() {
    int passed = 0;
    int failed = 0;
    std::string lastSuite;

    for (const auto& test : registry()) {
        if (test.suite != lastSuite) {
            std::printf("\n[%s]\n", test.suite.c_str());
            lastSuite = test.suite;
        }

        currentFailures().clear();
        try {
            test.fn();
        } catch (const std::exception& e) {
            currentFailures().push_back(std::string{"unexpected exception: "} + e.what());
        } catch (...) {
            currentFailures().emplace_back("unexpected non-standard exception");
        }

        if (currentFailures().empty()) {
            std::printf("  PASS  %s\n", test.name.c_str());
            ++passed;
        } else {
            std::printf("  FAIL  %s\n", test.name.c_str());
            for (const auto& failure : currentFailures()) {
                std::printf("          %s\n", failure.c_str());
            }
            ++failed;
        }
    }

    std::printf("\n%d passed, %d failed, %d total\n", passed, failed, passed + failed);
    return failed == 0 ? 0 : 1;
}

}  // namespace orca::test

// ---------------------------------------------------------------------------
// Macros
// ---------------------------------------------------------------------------

#define ORCA_TEST(suite, name)                                                          \
    static void orca_test_##suite##_##name();                                           \
    static const ::orca::test::Registrar orca_registrar_##suite##_##name{               \
        #suite, #name, &orca_test_##suite##_##name};                                    \
    static void orca_test_##suite##_##name()

/// Assertions record and continue rather than aborting, so one test reports every
/// failing expectation instead of only the first.
///
/// The single-expression macros are variadic because the preprocessor does not treat
/// braces as grouping: `CHECK(v == Vector3{1.0, 2.0, 3.0})` would otherwise be parsed as
/// three macro arguments. Variadic capture puts the whole expression back together.
#define CHECK(...)                                                                      \
    do {                                                                                \
        if (!(__VA_ARGS__)) {                                                           \
            ::orca::test::recordFailure(__FILE__, __LINE__,                             \
                                        "CHECK failed: " #__VA_ARGS__);                 \
        }                                                                               \
    } while (false)

#define CHECK_EQ(a, b)                                                                  \
    do {                                                                                \
        const auto orca_a = (a);                                                        \
        const auto orca_b = (b);                                                        \
        if (!(orca_a == orca_b)) {                                                      \
            ::orca::test::recordFailure(__FILE__, __LINE__,                             \
                                        "CHECK_EQ failed: " #a " == " #b);              \
        }                                                                               \
    } while (false)

#define CHECK_NEAR(a, b, tol)                                                           \
    do {                                                                                \
        const double orca_a = static_cast<double>(a);                                   \
        const double orca_b = static_cast<double>(b);                                   \
        const double orca_tol = static_cast<double>(tol);                               \
        if (!(std::fabs(orca_a - orca_b) <= orca_tol)) {                                \
            char orca_buf[256];                                                         \
            std::snprintf(orca_buf, sizeof(orca_buf),                                   \
                          "CHECK_NEAR failed: %s (%.10g) vs %s (%.10g), |diff|=%.3g > %.3g", \
                          #a, orca_a, #b, orca_b, std::fabs(orca_a - orca_b), orca_tol); \
            ::orca::test::recordFailure(__FILE__, __LINE__, orca_buf);                  \
        }                                                                               \
    } while (false)

/// Assert that `expr` throws std::invalid_argument — ORCA's construction-time
/// invariant-violation signal (Validation.hpp).
#define CHECK_THROWS(...)                                                               \
    do {                                                                                \
        bool orca_threw = false;                                                        \
        try {                                                                           \
            (void)(__VA_ARGS__);                                                        \
        } catch (const std::invalid_argument&) {                                        \
            orca_threw = true;                                                          \
        } catch (...) {                                                                 \
            ::orca::test::recordFailure(                                                \
                __FILE__, __LINE__,                                                     \
                "CHECK_THROWS: wrong exception type: " #__VA_ARGS__);                   \
            orca_threw = true;                                                          \
        }                                                                               \
        if (!orca_threw) {                                                              \
            ::orca::test::recordFailure(__FILE__, __LINE__,                             \
                                        "CHECK_THROWS: no throw from: " #__VA_ARGS__);  \
        }                                                                               \
    } while (false)

#define CHECK_NO_THROW(...)                                                             \
    do {                                                                                \
        try {                                                                           \
            (void)(__VA_ARGS__);                                                        \
        } catch (const std::exception& orca_e) {                                        \
            ::orca::test::recordFailure(__FILE__, __LINE__,                             \
                                        std::string{"CHECK_NO_THROW threw: "} +         \
                                            orca_e.what());                             \
        }                                                                               \
    } while (false)
