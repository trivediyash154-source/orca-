#pragma once

/// @file CsvWriter.hpp
/// Writes `BenchmarkRow`s to CSV — Phase 11 brief §27-28: machine-readable, every number
/// from an actual execution, never hand-written. One wide header covers every algorithm's
/// fields (brief §27's "do not fabricate results" extends to never writing a column value
/// for a field that specific row's algorithm did not actually produce -- those cells are
/// empty, not zero).

#include <ostream>
#include <string>
#include <vector>

#include "BenchmarkTypes.hpp"

namespace orca::analytics {

/// Appends `rows` to `out` as CSV lines, writing the header first only if `writeHeader` is
/// true (so multiple calls can append to one open file without repeating it).
void writeBenchmarkCsv(std::ostream& out, const std::vector<BenchmarkRow>& rows, bool writeHeader);

/// Convenience wrapper: opens `path` (append mode) and writes `rows`, writing the header
/// only if the file did not already exist before this call -- brief §27's "do not
/// overwrite prior experiment results."
///
/// @throws std::runtime_error if `path` cannot be opened for writing.
void appendBenchmarkCsv(const std::string& path, const std::vector<BenchmarkRow>& rows);

}  // namespace orca::analytics
