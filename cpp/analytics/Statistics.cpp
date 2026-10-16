#include "Statistics.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

#include "../core/Validation.hpp"

namespace orca::analytics {

Statistics computeStatistics(std::vector<double> values) {
    requireThat(!values.empty(), "computeStatistics: values must not be empty");

    std::sort(values.begin(), values.end());
    const std::size_t n = values.size();

    Statistics stats;
    stats.count = n;
    stats.minimum = values.front();
    stats.maximum = values.back();
    stats.mean = std::accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(n);
    stats.median = (n % 2 == 1) ? values[n / 2] : (values[n / 2 - 1] + values[n / 2]) / 2.0;

    if (n >= 2) {
        double sumSquaredDeviation = 0.0;
        for (const double v : values) {
            const double deviation = v - stats.mean;
            sumSquaredDeviation += deviation * deviation;
        }
        stats.stddev = std::sqrt(sumSquaredDeviation / static_cast<double>(n - 1));
    }

    return stats;
}

}  // namespace orca::analytics
