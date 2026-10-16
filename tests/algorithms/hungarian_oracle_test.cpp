/// Independent brute-force verification of `solveSquareAssignment()` — shares nothing with
/// `Hungarian.cpp`'s potentials-based implementation: this oracle enumerates every
/// permutation directly and takes the minimum by brute force. Mirrors the discipline
/// `pareto_search_oracle_test.cpp` established for the multi-objective search (Phase 6
/// brief §18's "oracle randomized" group, in the same spirit as Phase 5 brief §44-47).
///
/// Bounded to n <= 7 (5040 permutations, negligible runtime) since brute force is
/// factorial; that is what "independent oracle, not a scalability test" means here --
/// `assignment_benchmark.cpp` covers scaling behaviour separately.

#include <algorithm>
#include <cstdint>
#include <limits>
#include <numeric>
#include <vector>

#include "../../cpp/algorithms/Hungarian.hpp"
#include "../TestFramework.hpp"

using namespace orca;

namespace {

/// Deterministic, dependency-free PRNG (xorshift32) -- this file must not reuse anything
/// from production or from another test's random source, and does not need cryptographic
/// quality, only reproducibility across runs.
struct Xorshift32 {
    std::uint32_t state;
    explicit Xorshift32(std::uint32_t seed) : state(seed == 0 ? 1 : seed) {}
    std::uint32_t next() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }
    double nextDouble(double lo, double hi) {
        const double unit = static_cast<double>(next()) / 4294967296.0;
        return lo + unit * (hi - lo);
    }
};

/// Brute-force minimum-cost perfect matching by enumerating every permutation of
/// `[0, n)`. Returns the minimum total cost found.
double bruteForceMinimumAssignment(const std::vector<std::vector<double>>& cost) {
    const std::size_t n = cost.size();
    std::vector<std::size_t> perm(n);
    std::iota(perm.begin(), perm.end(), std::size_t{0});

    double best = std::numeric_limits<double>::max();
    do {
        double total = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            total += cost[i][perm[i]];
        }
        if (total < best) {
            best = total;
        }
    } while (std::next_permutation(perm.begin(), perm.end()));
    return best;
}

std::vector<std::vector<double>> randomMatrix(std::size_t n, Xorshift32& rng, double lo,
                                              double hi) {
    std::vector<std::vector<double>> m(n, std::vector<double>(n, 0.0));
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = 0; j < n; ++j) {
            m[i][j] = rng.nextDouble(lo, hi);
        }
    }
    return m;
}

}  // namespace

ORCA_TEST(HungarianOracle, AgreesOnHandPickedFourByFour) {
    const std::vector<std::vector<double>> cost{
        {4.0, 1.0, 3.0, 8.0}, {2.0, 5.0, 9.0, 1.0}, {7.0, 3.0, 2.0, 6.0}, {5.0, 6.0, 4.0, 2.0}};
    const HungarianResult solved = solveSquareAssignment(cost);
    const double oracle = bruteForceMinimumAssignment(cost);
    CHECK_NEAR(solved.totalCost, oracle, 1e-9);
}

ORCA_TEST(HungarianOracle, AgreesOnRandomisedMatricesAcrossSizesTwoToSeven) {
    Xorshift32 rng(0xC0FFEEu);
    int casesChecked = 0;
    for (std::size_t n = 2; n <= 7; ++n) {
        for (int trial = 0; trial < 12; ++trial) {
            const std::vector<std::vector<double>> cost = randomMatrix(n, rng, 0.0, 100.0);
            const HungarianResult solved = solveSquareAssignment(cost);
            const double oracle = bruteForceMinimumAssignment(cost);
            CHECK_NEAR(solved.totalCost, oracle, 1e-6);
            ++casesChecked;
        }
    }
    CHECK_EQ(casesChecked, 72);
}

ORCA_TEST(HungarianOracle, AgreesWithSentinelScaleInfeasibleCells) {
    // Mixes ordinary-scale real costs with kInfeasibleAssignmentCost-scale sentinels (the
    // same magnitude AssignmentCost.hpp uses) -- the oracle and the production solver must
    // still agree even when some cells are enormously more expensive than others.
    constexpr double kSentinel = 1.0e9;
    Xorshift32 rng(0xABCDu);
    int casesChecked = 0;
    for (std::size_t n = 3; n <= 6; ++n) {
        for (int trial = 0; trial < 6; ++trial) {
            std::vector<std::vector<double>> cost = randomMatrix(n, rng, 0.0, 10.0);
            // Force roughly a third of cells infeasible, but never an entire row or column
            // (that would make the instance itself pathological/unsolvable, which is a
            // domain-layer concern, not this raw-solver oracle's).
            for (std::size_t i = 0; i < n; ++i) {
                const std::size_t forcedInfeasibleCol = (i + 1) % n;
                cost[i][forcedInfeasibleCol] = kSentinel;
            }
            const HungarianResult solved = solveSquareAssignment(cost);
            const double oracle = bruteForceMinimumAssignment(cost);
            CHECK_NEAR(solved.totalCost, oracle, 1e-3);
            ++casesChecked;
        }
    }
    CHECK_EQ(casesChecked, 24);
}

ORCA_TEST(HungarianOracle, AgreesOnRepeatedTiedCosts) {
    // Many cells sharing the exact same value (ties are where a buggy tie-break could
    // silently produce a suboptimal-but-plausible-looking total).
    Xorshift32 rng(0x1234u);
    int casesChecked = 0;
    for (std::size_t n = 3; n <= 6; ++n) {
        for (int trial = 0; trial < 6; ++trial) {
            std::vector<std::vector<double>> cost(n, std::vector<double>(n, 5.0));
            // Perturb only a few cells so most of the matrix stays tied at 5.0.
            for (std::size_t k = 0; k < n; ++k) {
                cost[k][(k + static_cast<std::size_t>(trial)) % n] = rng.nextDouble(0.0, 5.0);
            }
            const HungarianResult solved = solveSquareAssignment(cost);
            const double oracle = bruteForceMinimumAssignment(cost);
            CHECK_NEAR(solved.totalCost, oracle, 1e-9);
            ++casesChecked;
        }
    }
    CHECK_EQ(casesChecked, 24);
}
