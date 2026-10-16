#include "RandomGenerator.hpp"

#include <cassert>
#include <limits>

namespace orca {

RandomGenerator::RandomGenerator(Seed seed) noexcept : seed_(seed), engine_(seed) {}

std::uint64_t RandomGenerator::nextUInt64() noexcept {
    return engine_();
}

double RandomGenerator::nextDouble() noexcept {
    // Take the top 53 bits — the exact mantissa width of an IEEE-754 double — and scale
    // by 2^-53. Both steps are exact, so no rounding is introduced and the mapping from
    // engine output to double is identical on every IEEE-754 platform.
    const std::uint64_t bits = nextUInt64() >> 11;
    return static_cast<double>(bits) * 0x1.0p-53;
}

double RandomGenerator::nextDoubleInRange(double lo, double hi) noexcept {
    assert(lo <= hi && "nextDoubleInRange requires lo <= hi");
    return lo + (hi - lo) * nextDouble();
}

std::int64_t RandomGenerator::nextIntInRange(std::int64_t lo, std::int64_t hi) noexcept {
    assert(lo <= hi && "nextIntInRange requires lo <= hi");

    // Width of the inclusive interval. Computed in unsigned arithmetic so that the full
    // int64 span does not overflow: [INT64_MIN, INT64_MAX] has 2^64 outcomes, which wraps
    // to 0 and is handled as the special case below.
    const std::uint64_t range =
        static_cast<std::uint64_t>(hi) - static_cast<std::uint64_t>(lo) + 1ULL;

    if (range == 0ULL) {
        // The interval covers every int64 value, so every draw is already uniform.
        return static_cast<std::int64_t>(nextUInt64());
    }

    // 2^64 mod range, computed without needing 128-bit arithmetic: in unsigned wrapping
    // arithmetic, (0 - range) == 2^64 - range, so (0 - range) % range == 2^64 % range.
    //
    // Draws below this threshold fall in the partial final block and would bias the low
    // end of the range, so they are rejected. Expected rejections are under one per call
    // for any realistic range.
    const std::uint64_t threshold = (0ULL - range) % range;

    std::uint64_t x = nextUInt64();
    while (x < threshold) {
        x = nextUInt64();
    }

    return lo + static_cast<std::int64_t>(x % range);
}

bool RandomGenerator::bernoulli(double p) noexcept {
    if (p <= 0.0) {
        return false;
    }
    if (p >= 1.0) {
        return true;
    }
    return nextDouble() < p;
}

RandomGenerator RandomGenerator::forStream(RandomStream stream) const noexcept {
    return forStream(static_cast<std::uint64_t>(stream));
}

RandomGenerator RandomGenerator::forStream(std::uint64_t streamId) const noexcept {
    // Mix the stream id first so that adjacent ids (1, 2, 3) produce well-separated
    // seeds, then mix again with the master seed. Two rounds because a single XOR of a
    // small id into a seed leaves neighbouring streams correlated in their early draws.
    //
    // Note this reads seed_ rather than engine_ state, so it is pure: deriving a child
    // never advances the parent.
    return RandomGenerator{splitMix64(seed_ ^ splitMix64(streamId))};
}

}  // namespace orca
