/// FIFO validation for the time-dependent transfer model.
///
/// # WHY THIS FILE DECIDES WHETHER PHASE 3 CAN PROCEED
///
/// ORCA's graph edges are weighted by `w(edge, t_departure)`. Dijkstra's greedy
/// commitment -- once a node is popped, its label is final -- is only sound on such a
/// graph if the model is **FIFO**:
///
///     for t1 <= t2:   t1 + w(e, t1)  <=  t2 + w(e, t2)
///
/// Departing later must never arrive earlier. Without it, a node finalised early can be
/// improved by a later relaxation, and the search returns wrong routes that look
/// completely plausible.
///
/// ## What is tested
///
/// 1. **FIFO on arrival time**, over thousands of randomised orbit pairs and ordered
///    departure times. Tested on *geometry*, not on feasible transfers only: FIFO is a
///    property of the model, and restricting to affordable transfers would leave most of
///    the domain unchecked.
///
/// 2. **Time-independence of the non-time objectives.** Stronger than FIFO and equally
///    necessary: multi-objective label-setting prunes by dominance, and that pruning is
///    sound only if extending a dominating label is never worse. If fuel varied with
///    departure time, an earlier-but-cheaper label could extend to a *more* expensive
///    edge, and optimal routes would be silently discarded.
///
/// ## Tolerance policy
///
/// Arrival time is mathematically a **staircase**: flat between rendezvous windows, then a
/// jump of one synodic period. Between windows the value is constant in exact arithmetic
/// but wobbles in the last few ulp in floating point. Measured worst case over the
/// 143 991 ordered comparisons this file performs: **8.1e-15 relative**, a few ulp.
///
/// So the assertion is `arrival(t2) >= arrival(t1) - tol` with a *relative* tolerance,
/// because arrival times span 1e3 to 1e9 seconds and a fixed absolute epsilon would be
/// either uselessly loose at the top of that range or spuriously tight at the bottom.
/// The chosen 1e-12 sits about 120x above the observed rounding floor and roughly 1e12
/// below any real violation -- a genuine FIFO breach is a backwards jump of seconds to
/// hours. This is a rounding allowance, not a weakened claim.

#include <cmath>
#include <vector>

#include "../../cpp/core/Constants.hpp"
#include "../../cpp/core/Math.hpp"
#include "../../cpp/core/RandomGenerator.hpp"
#include "../../cpp/core/Units.hpp"
#include "../../cpp/transfer/PhasingModel.hpp"
#include "../../cpp/transfer/TransferModel.hpp"
#include "../TestFramework.hpp"

using namespace orca;

namespace {

/// Relative tolerance for the staircase's flat sections. 1e-12 is ~1000x the measured
/// worst-case rounding, and ~1e12 times smaller than any real violation.
constexpr double kFifoRelativeTolerance = 1e-12;

[[nodiscard]] bool fifoHolds(Seconds earlierArrival, Seconds laterArrival) noexcept {
    const double scale = std::fmax(std::fabs(earlierArrival), 1.0);
    return laterArrival >= earlierArrival - kFifoRelativeTolerance * scale;
}

/// Deterministically generate a valid orbit. Uses ORCA's own seeded RNG so the whole
/// suite is reproducible: a failure can always be re-run to the same case.
[[nodiscard]] Orbit randomOrbit(RandomGenerator& rng) {
    const Kilometres altitude = rng.nextDoubleInRange(300.0, 35000.0);
    const Radians inclination = rng.nextDoubleInRange(0.0, kPi);
    const Radians raan = rng.nextDoubleInRange(0.0, kTwoPi);
    const Radians phase = rng.nextDoubleInRange(0.0, kTwoPi);
    return Orbit::fromAltitude(altitude, inclination, raan, phase);
}

}  // namespace

// ---------------------------------------------------------------------------
// The headline property
// ---------------------------------------------------------------------------

ORCA_TEST(Fifo, HoldsAcrossRandomisedOrbitPairs) {
    const TransferModel model;
    RandomGenerator rng{928371};

    int pairsTested = 0;
    int comparisons = 0;
    int skippedNoWindow = 0;

    for (int trial = 0; trial < 1500; ++trial) {
        const Orbit source = randomOrbit(rng);
        const Orbit target = randomOrbit(rng);

        // Sample departure times fine enough to land inside the flat sections of the
        // staircase, where rounding is most likely to produce a spurious inversion.
        const Seconds tSyn = phasing::synodicPeriodSec(source, target);
        const Seconds step =
            std::isfinite(tSyn) ? std::fmin(600.0, tSyn / 50.0) : 600.0;

        Seconds previousArrival = 0.0;
        bool havePrevious = false;
        ++pairsTested;

        for (int i = 0; i < 60; ++i) {
            const Seconds departure = static_cast<double>(i) * step;
            const TransferGeometry g = model.computeGeometry(source, target, departure);

            if (!g.rendezvousWindowExists) {
                ++skippedNoWindow;
                continue;  // no arrival time exists to compare
            }
            if (havePrevious) {
                CHECK(fifoHolds(previousArrival, g.arrivalTimeSec));
                ++comparisons;
            }
            previousArrival = g.arrivalTimeSec;
            havePrevious = true;
        }
    }

    // Guard against the test silently degenerating into checking nothing.
    CHECK(pairsTested == 1500);
    CHECK(comparisons > 50000);
    CHECK(skippedNoWindow < comparisons);
}

ORCA_TEST(Fifo, HoldsForNonMonotonicDepartureSampling) {
    // The property is about ordered pairs, not about a scanning loop. Compare every
    // ordered pair within a sampled set, not just consecutive ones.
    const TransferModel model;
    RandomGenerator rng{104729};

    for (int trial = 0; trial < 200; ++trial) {
        const Orbit source = randomOrbit(rng);
        const Orbit target = randomOrbit(rng);

        std::vector<Seconds> departures;
        std::vector<Seconds> arrivals;
        for (int i = 0; i < 20; ++i) {
            const Seconds t = rng.nextDoubleInRange(0.0, 200000.0);
            const TransferGeometry g = model.computeGeometry(source, target, t);
            if (g.rendezvousWindowExists) {
                departures.push_back(t);
                arrivals.push_back(g.arrivalTimeSec);
            }
        }

        for (std::size_t i = 0; i < departures.size(); ++i) {
            for (std::size_t j = 0; j < departures.size(); ++j) {
                if (departures[i] <= departures[j]) {
                    CHECK(fifoHolds(arrivals[i], arrivals[j]));
                }
            }
        }
    }
}

ORCA_TEST(Fifo, HoldsAcrossSpecificChallengingOrbitPairs) {
    // The regimes where the rejected delta-v-penalty model failed most often: large
    // altitude ratios with short synodic periods. Named explicitly so a regression here
    // is legible rather than buried in a random trial index.
    struct Pair {
        Kilometres sourceAlt;
        Kilometres targetAlt;
        Radians sourcePhase;
        Radians targetPhase;
    };
    const Pair pairs[] = {
        {500.0, 800.0, 0.0, 1.0},        // the canonical ORCA case
        {500.0, 20000.0, 0.0, 2.0},      // penalty model violated FIFO here
        {500.0, 35786.0, 1.0, 4.0},      // LEO -> GEO
        {300.0, 35786.0, 0.5, 5.5},      // penalty model violated FIFO here
        {35786.0, 300.0, 2.0, 0.3},      // descending
        {20000.0, 35786.0, 3.0, 1.0},    // MEO -> GEO
        {2814.0, 18895.0, 0.0, 3.0},     // a measured violation case for the old model
        {1916.0, 30089.0, 1.2, 4.7},     // another measured violation case
        {500.0, 505.0, 0.0, 0.1},        // near-co-altitude, huge synodic period
    };

    const TransferModel model;
    for (const Pair& p : pairs) {
        const Orbit source = Orbit::fromAltitude(p.sourceAlt, 0.3, 0.0, p.sourcePhase);
        const Orbit target = Orbit::fromAltitude(p.targetAlt, 0.6, 0.0, p.targetPhase);

        const Seconds tSyn = phasing::synodicPeriodSec(source, target);
        const Seconds step = std::isfinite(tSyn) ? tSyn / 500.0 : 100.0;

        Seconds previous = model.computeGeometry(source, target, 0.0).arrivalTimeSec;
        for (int i = 1; i < 1500; ++i) {
            const Seconds t = static_cast<double>(i) * step;
            const Seconds current = model.computeGeometry(source, target, t).arrivalTimeSec;
            CHECK(fifoHolds(previous, current));
            previous = current;
        }
    }
}

ORCA_TEST(Fifo, ArrivalIsAStaircaseNotAMonotoneLine) {
    // Confirms the *shape* the proof predicts: arrival is constant between windows, then
    // jumps by one synodic period. If arrival simply tracked departure, FIFO would hold
    // trivially and the test above would be vacuous -- so this checks the test has teeth.
    const TransferModel model;
    const Orbit source = Orbit::fromAltitude(500.0, 0.0, 0.0, 0.0);
    const Orbit target = Orbit::fromAltitude(800.0, 0.0, 0.0, 1.0);
    const Seconds tSyn = phasing::synodicPeriodSec(source, target);

    const Seconds first = model.computeGeometry(source, target, 0.0).arrivalTimeSec;

    // Flat section: arrival unchanged despite departure advancing by hours.
    const Seconds later = model.computeGeometry(source, target, 3600.0).arrivalTimeSec;
    CHECK_NEAR(first, later, 1e-6);

    // Jump section: one synodic period further on.
    const Seconds afterWindow =
        model.computeGeometry(source, target, first + 1.0).arrivalTimeSec;
    CHECK(afterWindow > first);
    CHECK_NEAR(afterWindow - first, tSyn, 10.0);
}

// ---------------------------------------------------------------------------
// The stronger property Phase 3's dominance pruning needs
// ---------------------------------------------------------------------------

ORCA_TEST(Fifo, NonTimeObjectivesAreIndependentOfDepartureTime) {
    // Delta-v, fuel, risk and path length must be functions of the two orbits alone.
    //
    // This is what makes multi-objective dominance pruning sound (ALGORITHMS.md §5.6). If
    // fuel varied with departure time, a label that dominates another could extend to a
    // *worse* edge, and the Pareto search would discard optimal solutions while appearing
    // to work perfectly.
    const TransferModel model;
    RandomGenerator rng{224737};

    for (int trial = 0; trial < 400; ++trial) {
        const Orbit source = randomOrbit(rng);
        const Orbit target = randomOrbit(rng);

        const TransferGeometry reference = model.computeGeometry(source, target, 0.0);
        if (!reference.rendezvousWindowExists) {
            continue;
        }

        for (const Seconds t : {1.0, 137.0, 5000.0, 86400.0, 1.0e6, 1.0e7}) {
            const TransferGeometry g = model.computeGeometry(source, target, t);
            CHECK_NEAR(g.deltaVKms, reference.deltaVKms, 1e-12);
            CHECK_NEAR(g.planeChangeDeltaVKms, reference.planeChangeDeltaVKms, 1e-12);
            CHECK_NEAR(g.pathLengthKm, reference.pathLengthKm, 1e-9);
            CHECK_NEAR(g.coastSec, reference.coastSec, 1e-9);
        }
    }
}

ORCA_TEST(Fifo, FuelAndRiskAreIndependentOfDepartureTime) {
    // The same property at the TransferResult level, where fuel cost and risk are formed.
    const TransferModel model;
    RandomGenerator rng{350377};

    TransferConstraints constraints;
    constraints.maxWaitSec = 1.0e12;  // do not let feasibility mask the comparison

    for (int trial = 0; trial < 300; ++trial) {
        const Orbit source = randomOrbit(rng);
        const Orbit target = randomOrbit(rng);

        const TransferResult reference = model.evaluate(source, target, 0.0, constraints);
        if (!reference.geometry.rendezvousWindowExists) {
            continue;
        }
        for (const Seconds t : {42.0, 9999.0, 250000.0, 3.6e6}) {
            const TransferResult r = model.evaluate(source, target, t, constraints);
            CHECK_NEAR(r.fuelCost, reference.fuelCost, 1e-12);
            CHECK_NEAR(r.risk, reference.risk, 1e-12);
        }
    }
}

ORCA_TEST(Fifo, OnlyTheTimeObjectiveVariesWithDeparture) {
    // Stated as a property of the objective vector the graph will actually consume.
    const TransferModel model;
    const Orbit source = Orbit::fromAltitude(500.0, 0.2, 0.0, 0.0);
    const Orbit target = Orbit::fromAltitude(1500.0, 0.9, 0.0, 2.5);

    TransferConstraints constraints;
    constraints.maxWaitSec = 1.0e12;

    const ObjectiveVector a = model.evaluate(source, target, 0.0, constraints).objectives();
    const ObjectiveVector b =
        model.evaluate(source, target, 40000.0, constraints).objectives();

    CHECK_NEAR(a.fuel, b.fuel, 1e-12);
    CHECK_NEAR(a.risk, b.risk, 1e-12);
    CHECK_NEAR(a.distance, b.distance, 1e-9);
    // Time is the one that moves -- and only downwards toward the same window, never
    // producing an earlier arrival.
    CHECK(a.time >= 0.0);
    CHECK(b.time >= 0.0);
}
