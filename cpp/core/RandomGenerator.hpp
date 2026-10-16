#pragma once

/// @file RandomGenerator.hpp
/// Deterministic pseudo-random generation.
///
/// ## The problem this solves
///
/// ORCA requires that seed + config produce an identical run (FR-N5). The obvious
/// implementation — seed `std::mt19937_64` and use `std::uniform_real_distribution` —
/// **does not achieve this across platforms.**
///
/// `std::mt19937_64` is fully specified by the standard: the same seed produces the same
/// 64-bit sequence on every conforming implementation. The *distribution* classes are
/// not. `[rand.dist]` constrains only their statistical properties, not their algorithm,
/// so libstdc++ and libc++ are both correct while producing entirely different values
/// from the same engine state.
///
/// Using them would give ORCA determinism on one machine and silent divergence on
/// another, which is the worst kind of reproducibility bug: invisible until someone tries
/// to replicate a benchmark.
///
/// This class therefore uses `std::mt19937_64` for the bit stream and implements every
/// transform itself, from documented integer operations.
///
/// ## Portability guarantee, stated precisely
///
/// | Operation                | Bit-identical across platforms? |
/// |--------------------------|---------------------------------|
/// | `nextUInt64`             | Yes — engine is standard-specified |
/// | `nextDouble`             | Yes — integer shift and exact power-of-two scaling |
/// | `nextDoubleInRange`      | Yes — IEEE-754 arithmetic is deterministic |
/// | `nextIntInRange`         | Yes — integer operations only |
/// | `bernoulli`              | Yes — comparison of the above |
/// | `forStream`              | Yes — SplitMix64, integer operations only |
///
/// This holds for any IEEE-754 platform with a conforming `<random>`. It would NOT hold
/// for a Gaussian or exponential transform, because those route through `std::log`,
/// `std::cos` or `std::sqrt`, and only `sqrt` is required to be correctly rounded. Such
/// transforms are therefore **deliberately absent** until a subsystem needs one, at which
/// point the portability caveat must be documented alongside it rather than discovered
/// later.
///
/// ## Stream splitting
///
/// Each subsystem draws from its own derived generator (SIMULATION_MODEL.md §7.2). This
/// is what makes the design robust rather than merely deterministic: adding a debris
/// object does not shift the failure schedule, because they draw from independent
/// streams. Without splitting, any change to one subsystem's draw *count* would perturb
/// every later draw in the run and destroy cross-run comparability — so a scenario tweak
/// would invalidate every previously recorded benchmark.
///
/// `std::random_device` is never used, anywhere in ORCA.

#include <cstdint>
#include <random>

#include "Types.hpp"

namespace orca {

/// Named random streams. Values are fixed by SIMULATION_MODEL.md §7.2 and must not be
/// renumbered — changing one would change every scenario generated from a given seed,
/// silently invalidating recorded fixtures and benchmark results.
enum class RandomStream : std::uint64_t {
    Orbits = 0x01,
    Failures = 0x02,
    Debris = 0x03,
    Severity = 0x04,
    Scenario = 0x05,
};

class RandomGenerator {
public:
    /// Construct from an explicit seed. There is no default constructor: every generator
    /// in ORCA must be able to name where its seed came from.
    explicit RandomGenerator(Seed seed) noexcept;

    /// The seed this generator was constructed with. Used for logging and for export
    /// metadata, so a run can always report how to reproduce itself.
    [[nodiscard]] Seed seed() const noexcept { return seed_; }

    /// Raw 64 bits from the underlying engine. Advances the state.
    [[nodiscard]] std::uint64_t nextUInt64() noexcept;

    /// Uniform double in `[0, 1)`.
    ///
    /// Built from the top 53 bits of a 64-bit draw scaled by 2^-53. Both operations are
    /// exact in IEEE-754 double, so the result is reproducible bit-for-bit.
    [[nodiscard]] double nextDouble() noexcept;

    /// Uniform double in `[lo, hi)`. Requires `lo <= hi`.
    ///
    /// Computed as `lo + (hi - lo) * nextDouble()`. Mathematically half-open; for
    /// extreme ranges floating-point rounding could in principle return `hi`. Callers
    /// requiring a strict bound should clamp.
    [[nodiscard]] double nextDoubleInRange(double lo, double hi) noexcept;

    /// Uniform integer in `[lo, hi]`, inclusive. Requires `lo <= hi`.
    ///
    /// Unbiased. A naive `value % range` over-represents the low end of the range
    /// whenever `range` does not divide 2^64; this rejects the biased tail instead,
    /// which costs an occasional extra draw and buys exact uniformity.
    [[nodiscard]] std::int64_t nextIntInRange(std::int64_t lo, std::int64_t hi) noexcept;

    /// True with probability `p`. `p <= 0` is always false, `p >= 1` always true.
    [[nodiscard]] bool bernoulli(double p) noexcept;

    /// Derive an independent generator for a named subsystem stream.
    ///
    /// `const` and pure: calling it does not advance this generator, so deriving a child
    /// stream never perturbs the parent's sequence. That property is what makes stream
    /// independence hold.
    [[nodiscard]] RandomGenerator forStream(RandomStream stream) const noexcept;

    /// Derive an independent generator from an arbitrary stream index. Prefer the
    /// `RandomStream` overload; this exists for per-entity sub-streams, where the index
    /// is an entity ordinal.
    [[nodiscard]] RandomGenerator forStream(std::uint64_t streamId) const noexcept;

private:
    Seed seed_;
    std::mt19937_64 engine_;
};

/// SplitMix64 — a fully-specified integer mixing function.
///
/// Used for seed derivation because every operation is exact integer arithmetic, so
/// derived seeds are identical on every platform. Exposed rather than hidden because the
/// determinism tests verify its output directly.
[[nodiscard]] constexpr std::uint64_t splitMix64(std::uint64_t x) noexcept {
    x += 0x9E3779B97F4A7C15ULL;
    std::uint64_t z = x;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

}  // namespace orca
