/// Tests for deterministic random generation.
///
/// These are the tests that underwrite FR-N5 (reproducibility). Several use expected
/// values produced by an *independent* implementation — the C++ standard's own
/// conformance value for mt19937_64, and a separate Python implementation of SplitMix64 —
/// rather than by the code under test.

#include <cstdint>
#include <random>
#include <set>
#include <vector>

#include "../../cpp/core/RandomGenerator.hpp"
#include "../TestFramework.hpp"

using namespace orca;

namespace {

constexpr Seed kDemoSeed = 928371;

std::vector<std::uint64_t> drawInts(RandomGenerator& rng, int count) {
    std::vector<std::uint64_t> out;
    out.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        out.push_back(rng.nextUInt64());
    }
    return out;
}

}  // namespace

// ---------------------------------------------------------------------------
// Engine conformance -- the foundation of the portability claim
// ---------------------------------------------------------------------------

ORCA_TEST(Random, EngineMatchesStandardConformanceValue) {
    // [rand.predef] requires that the 10000th consecutive invocation of a
    // default-constructed mt19937_64 (seed 5489) produces 9981545732273789042.
    //
    // This is an independent oracle in the strongest available sense: the expected value
    // comes from the C++ standard, not from ORCA. It verifies that the underlying bit
    // stream is the standard-specified one, which is precisely what the cross-platform
    // determinism claim in RandomGenerator.hpp rests on. If this fails, the library is
    // non-conforming and no ORCA reproducibility guarantee holds.
    std::mt19937_64 engine;  // default seed = 5489
    std::uint64_t value = 0;
    for (int i = 0; i < 10000; ++i) {
        value = engine();
    }
    CHECK_EQ(value, std::uint64_t{9981545732273789042ULL});
}

ORCA_TEST(Random, SplitMix64MatchesIndependentImplementation) {
    // Expected values computed by a separate Python implementation of SplitMix64.
    CHECK_EQ(splitMix64(0), std::uint64_t{16294208416658607535ULL});
    CHECK_EQ(splitMix64(1), std::uint64_t{10451216379200822465ULL});
    CHECK_EQ(splitMix64(2), std::uint64_t{10905525725756348110ULL});
    CHECK_EQ(splitMix64(0x9E3779B97F4A7C15ULL), std::uint64_t{7960286522194355700ULL});
    CHECK_EQ(splitMix64(kDemoSeed), std::uint64_t{11020203463416319129ULL});
}

// ---------------------------------------------------------------------------
// Reproducibility
// ---------------------------------------------------------------------------

ORCA_TEST(Random, SameSeedProducesSameSequence) {
    RandomGenerator a{kDemoSeed};
    RandomGenerator b{kDemoSeed};
    CHECK(drawInts(a, 500) == drawInts(b, 500));
}

ORCA_TEST(Random, SameSeedProducesSameDoubleSequence) {
    RandomGenerator a{kDemoSeed};
    RandomGenerator b{kDemoSeed};
    for (int i = 0; i < 500; ++i) {
        // Bit-identical, not merely close: these are the same computation.
        CHECK(a.nextDouble() == b.nextDouble());
    }
}

ORCA_TEST(Random, DifferentSeedsProduceDifferentSequences) {
    RandomGenerator a{kDemoSeed};
    RandomGenerator b{kDemoSeed + 1};
    CHECK(drawInts(a, 100) != drawInts(b, 100));
}

ORCA_TEST(Random, SeedIsReportedForReproduction) {
    // Every run must be able to say how to reproduce itself.
    const RandomGenerator rng{kDemoSeed};
    CHECK_EQ(rng.seed(), kDemoSeed);
}

ORCA_TEST(Random, ResetToSeedReproducesFromStart) {
    RandomGenerator a{kDemoSeed};
    const auto first = drawInts(a, 50);
    (void)drawInts(a, 1000);  // advance well past

    RandomGenerator fresh{kDemoSeed};
    CHECK(drawInts(fresh, 50) == first);
}

// ---------------------------------------------------------------------------
// Range guarantees
// ---------------------------------------------------------------------------

ORCA_TEST(Random, NextDoubleStaysInHalfOpenUnitInterval) {
    RandomGenerator rng{kDemoSeed};
    for (int i = 0; i < 200000; ++i) {
        const double v = rng.nextDouble();
        CHECK(v >= 0.0);
        CHECK(v < 1.0);
    }
}

ORCA_TEST(Random, NextDoubleInRangeRespectsBounds) {
    RandomGenerator rng{kDemoSeed};
    for (int i = 0; i < 100000; ++i) {
        const double v = rng.nextDoubleInRange(-12.5, 37.25);
        CHECK(v >= -12.5);
        CHECK(v <= 37.25);
    }
}

ORCA_TEST(Random, NextDoubleInRangeWithEmptyRangeIsConstant) {
    RandomGenerator rng{kDemoSeed};
    for (int i = 0; i < 100; ++i) {
        CHECK_NEAR(rng.nextDoubleInRange(4.0, 4.0), 4.0, 1e-15);
    }
}

ORCA_TEST(Random, NextIntInRangeRespectsInclusiveBounds) {
    RandomGenerator rng{kDemoSeed};
    for (int i = 0; i < 100000; ++i) {
        const std::int64_t v = rng.nextIntInRange(-5, 5);
        CHECK(v >= -5);
        CHECK(v <= 5);
    }
}

ORCA_TEST(Random, NextIntInRangeReachesBothEndpoints) {
    // An off-by-one in the rejection bound would show up as a never-produced endpoint,
    // which range-only checks would miss entirely.
    RandomGenerator rng{kDemoSeed};
    std::set<std::int64_t> seen;
    for (int i = 0; i < 20000; ++i) {
        seen.insert(rng.nextIntInRange(0, 9));
    }
    CHECK_EQ(seen.size(), std::size_t{10});
    CHECK(seen.count(0) == 1);
    CHECK(seen.count(9) == 1);
}

ORCA_TEST(Random, NextIntInRangeSingleValue) {
    RandomGenerator rng{kDemoSeed};
    for (int i = 0; i < 100; ++i) {
        CHECK_EQ(rng.nextIntInRange(7, 7), std::int64_t{7});
    }
}

ORCA_TEST(Random, NextIntInRangeIsApproximatelyUniform) {
    // Not a rigorous statistical test -- it is a coarse guard against a badly biased
    // rejection bound. With 120000 draws over 12 buckets, each bucket expects 10000;
    // allowing +/-15% is far outside plausible sampling noise but well inside what a
    // real modulo bias would produce.
    RandomGenerator rng{kDemoSeed};
    constexpr int kBuckets = 12;
    constexpr int kDraws = 120000;
    std::vector<int> counts(kBuckets, 0);
    for (int i = 0; i < kDraws; ++i) {
        counts[static_cast<std::size_t>(rng.nextIntInRange(0, kBuckets - 1))]++;
    }
    const double expected = static_cast<double>(kDraws) / kBuckets;
    for (const int count : counts) {
        CHECK(static_cast<double>(count) > expected * 0.85);
        CHECK(static_cast<double>(count) < expected * 1.15);
    }
}

// ---------------------------------------------------------------------------
// Bernoulli
// ---------------------------------------------------------------------------

ORCA_TEST(Random, BernoulliBoundaryProbabilities) {
    RandomGenerator rng{kDemoSeed};
    for (int i = 0; i < 1000; ++i) {
        CHECK(!rng.bernoulli(0.0));
        CHECK(!rng.bernoulli(-1.0));
        CHECK(rng.bernoulli(1.0));
        CHECK(rng.bernoulli(2.0));
    }
}

ORCA_TEST(Random, BernoulliDoesNotConsumeDrawsAtBoundaries) {
    // p <= 0 and p >= 1 short-circuit, so they must not advance the stream. If they did,
    // a scenario with a zero-probability event would perturb every later draw.
    RandomGenerator a{kDemoSeed};
    RandomGenerator b{kDemoSeed};
    for (int i = 0; i < 100; ++i) {
        (void)a.bernoulli(0.0);
        (void)a.bernoulli(1.0);
    }
    CHECK(drawInts(a, 20) == drawInts(b, 20));
}

ORCA_TEST(Random, BernoulliFrequencyIsPlausible) {
    RandomGenerator rng{kDemoSeed};
    constexpr int kTrials = 100000;
    int hits = 0;
    for (int i = 0; i < kTrials; ++i) {
        if (rng.bernoulli(0.25)) {
            ++hits;
        }
    }
    const double rate = static_cast<double>(hits) / kTrials;
    CHECK(rate > 0.24);
    CHECK(rate < 0.26);
}

// ---------------------------------------------------------------------------
// Stream splitting -- the property that makes the design robust, not just deterministic
// ---------------------------------------------------------------------------

ORCA_TEST(Random, ForStreamMatchesIndependentImplementation) {
    // Derived seeds computed by the separate Python implementation.
    const RandomGenerator master{kDemoSeed};
    CHECK_EQ(master.forStream(RandomStream::Orbits).seed(),
             std::uint64_t{11775770208899723441ULL});
    CHECK_EQ(master.forStream(RandomStream::Failures).seed(),
             std::uint64_t{8400887988710039415ULL});
    CHECK_EQ(master.forStream(RandomStream::Debris).seed(),
             std::uint64_t{7044464307299093883ULL});
    CHECK_EQ(master.forStream(RandomStream::Severity).seed(),
             std::uint64_t{17413778150545912176ULL});
    CHECK_EQ(master.forStream(RandomStream::Scenario).seed(),
             std::uint64_t{1442973741200430394ULL});
}

ORCA_TEST(Random, ForStreamIsPureAndDoesNotAdvanceParent) {
    // Deriving a child stream must not perturb the parent's sequence -- otherwise the
    // order in which subsystems are constructed would change the run.
    RandomGenerator a{kDemoSeed};
    RandomGenerator b{kDemoSeed};
    for (int i = 0; i < 50; ++i) {
        (void)a.forStream(RandomStream::Debris);
        (void)a.forStream(RandomStream::Failures);
    }
    CHECK(drawInts(a, 50) == drawInts(b, 50));
}

ORCA_TEST(Random, DifferentStreamsAreIndependent) {
    const RandomGenerator master{kDemoSeed};
    RandomGenerator orbits = master.forStream(RandomStream::Orbits);
    RandomGenerator failures = master.forStream(RandomStream::Failures);
    CHECK(drawInts(orbits, 100) != drawInts(failures, 100));
}

ORCA_TEST(Random, SameStreamFromSameMasterIsIdentical) {
    const RandomGenerator master{kDemoSeed};
    RandomGenerator a = master.forStream(RandomStream::Failures);
    RandomGenerator b = master.forStream(RandomStream::Failures);
    CHECK(drawInts(a, 100) == drawInts(b, 100));
}

ORCA_TEST(Random, AdjacentStreamIdsAreWellSeparated) {
    // Two rounds of SplitMix64 exist so that neighbouring stream ids do not produce
    // correlated early draws. A single XOR would leave streams 1 and 2 visibly similar.
    const RandomGenerator master{kDemoSeed};
    std::set<std::uint64_t> firstDraws;
    for (std::uint64_t id = 0; id < 64; ++id) {
        RandomGenerator child = master.forStream(id);
        firstDraws.insert(child.nextUInt64());
    }
    CHECK_EQ(firstDraws.size(), std::size_t{64});  // no collisions
}

ORCA_TEST(Random, ConsumingOneStreamDoesNotAffectAnother) {
    // The headline property from SIMULATION_MODEL.md §7.2: adding a debris object must
    // not shift the failure schedule. Modelled here by drawing heavily from the debris
    // stream and confirming the failures stream is untouched.
    const RandomGenerator master{kDemoSeed};

    RandomGenerator failuresBaseline = master.forStream(RandomStream::Failures);
    const auto baseline = drawInts(failuresBaseline, 100);

    RandomGenerator debris = master.forStream(RandomStream::Debris);
    (void)drawInts(debris, 10000);  // as if the scenario gained many debris objects

    RandomGenerator failuresAfter = master.forStream(RandomStream::Failures);
    CHECK(drawInts(failuresAfter, 100) == baseline);
}
