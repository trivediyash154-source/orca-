/// `writeBenchmarkCsv()`/`appendBenchmarkCsv()` tests — Phase 11 brief §27-28: valid CSV,
/// a header written exactly once, never overwriting prior results.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "../../cpp/analytics/CsvWriter.hpp"
#include "../TestFramework.hpp"

using namespace orca::analytics;

namespace {
BenchmarkRow makeRow(const std::string& algorithm, int repetition) {
    BenchmarkRow r;
    r.scenarioFamily = "normal";
    r.objectCount = 25;
    r.seed = 1;
    r.algorithm = algorithm;
    r.repetition = repetition;
    r.found = true;
    r.executionTimeMs = 1.5;
    return r;
}
}  // namespace

ORCA_TEST(CsvWriter, WritesAHeaderFollowedByOneLinePerRow) {
    std::ostringstream out;
    writeBenchmarkCsv(out, {makeRow("DIJKSTRA", 0), makeRow("ASTAR", 0)}, /*writeHeader=*/true);
    const std::string text = out.str();
    std::size_t newlineCount = 0;
    for (char c : text) {
        if (c == '\n') ++newlineCount;
    }
    CHECK_EQ(newlineCount, std::size_t{3});  // header + 2 rows
    CHECK(text.find("scenarioFamily") == 0);  // header is the first line
}

ORCA_TEST(CsvWriter, OmittingTheHeaderProducesOnlyDataLines) {
    std::ostringstream out;
    writeBenchmarkCsv(out, {makeRow("DIJKSTRA", 0)}, /*writeHeader=*/false);
    CHECK(out.str().find("scenarioFamily") == std::string::npos);
}

ORCA_TEST(CsvWriter, NAFieldsAreEmptyNotZero) {
    std::ostringstream out;
    BenchmarkRow r = makeRow("HUNGARIAN", 0);
    // No `search`/`pareto` set -- a Hungarian row's nodesExpanded column must be empty,
    // not "0" (which would misrepresent "not applicable" as "zero nodes expanded").
    writeBenchmarkCsv(out, {r}, /*writeHeader=*/true);
    const std::string text = out.str();
    // The nodesExpanded column (9th) should be empty: "...,true,1.5,,,...".
    CHECK(text.find(",true,1.5,,") != std::string::npos);
}

ORCA_TEST(CsvWriter, AppendBenchmarkCsvWritesTheHeaderOnlyOnce) {
    const std::string path = (std::filesystem::temp_directory_path() / "orca_csv_writer_test.csv").string();
    std::filesystem::remove(path);  // start clean, regardless of a prior test run

    appendBenchmarkCsv(path, {makeRow("DIJKSTRA", 0)});
    appendBenchmarkCsv(path, {makeRow("ASTAR", 0)});  // must NOT repeat the header

    std::ifstream in(path);
    std::string line;
    int headerCount = 0, lineCount = 0;
    while (std::getline(in, line)) {
        ++lineCount;
        if (line.rfind("scenarioFamily", 0) == 0) ++headerCount;
    }
    CHECK_EQ(headerCount, 1);
    CHECK_EQ(lineCount, 3);  // one header + two data rows, never overwritten

    std::filesystem::remove(path);
}
