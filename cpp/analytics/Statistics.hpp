#pragma once

/// @file Statistics.hpp
/// Summary statistics over repeated trials — Phase 11 brief §29. Deliberately minimal:
/// mean, median, min, max, sample standard deviation. No dependency on the benchmark or
/// scenario types, so it is independently testable against hand-computed reference values.

#include <cstddef>
#include <vector>

namespace orca::analytics {

struct Statistics {
    double mean = 0.0;
    double median = 0.0;
    double minimum = 0.0;
    double maximum = 0.0;
    double stddev = 0.0;  ///< sample standard deviation (n-1 denominator); 0 if n < 2
    std::size_t count = 0;
};

/// @throws std::invalid_argument if `values` is empty -- there is no meaningful "statistics
/// of nothing," and returning a zeroed struct would be indistinguishable from "n
/// measurements that all happened to be zero."
[[nodiscard]] Statistics computeStatistics(std::vector<double> values);

}  // namespace orca::analytics
