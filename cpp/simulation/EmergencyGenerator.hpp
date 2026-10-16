#pragma once

/// @file EmergencyGenerator.hpp
/// Schedules `SatelliteFailure` events — Phase 8 brief §22-24. Two independent modes, kept
/// as two distinct code paths rather than one parameterised blend, so a scripted demo
/// scenario's timing is never perturbed by touching the random-mode parameters and vice
/// versa (brief §23: "keep these modes separate").
///
/// ## This class schedules; it does not create emergencies
///
/// `EmergencyGenerator` populates the `EventQueue` with `SatelliteFailure` events at
/// determined times -- it never touches a `Satellite`'s health or constructs an
/// `Emergency`. That happens in `EventDispatcher.cpp` when the event actually fires,
/// preserving the same "the surprise is in the detection, not the schedule" principle
/// `DebrisObject.hpp`'s file comment states for debris: the scenario (or, here, the
/// schedule) is fully determined up front; only when an event is PROCESSED does the world
/// actually change.

#include <vector>

#include "../core/RandomGenerator.hpp"
#include "../core/Types.hpp"
#include "../core/Units.hpp"
#include "EventQueue.hpp"
#include "SimulationState.hpp"

namespace orca {

/// One scripted failure: a known satellite, at a known time, with a known cause and
/// severity -- brief §23's "known events at exact simulation times."
struct ScriptedFailure {
    Seconds atSec;
    SatelliteId satellite;
    EmergencyKind kind;
    Severity severity;
    Seconds survivalWindowSec;
};

/// Seeded-random mode configuration -- brief §23's "events generated from seed + scenario
/// parameters." Deliberately simple: `failureCount` satellites, chosen uniformly at random
/// without replacement from every eligible satellite in `state`, each given a uniformly
/// random failure time in `[0, durationSec]` and a uniformly random kind/severity. This is
/// not a calibrated reliability model (brief §42: "do not implement high-fidelity...
/// physics" applies equally to failure statistics) -- it is a simple, fully deterministic
/// generator whose only job is to produce a reproducible, varied schedule.
struct RandomFailureConfig {
    int failureCount = 0;
    Seconds durationSec = 3600.0;

    /// Every generated emergency uses this same survival window -- a modelling
    /// simplification (brief §42's "simple deterministic models"), not a claim that real
    /// survival windows are uniform. A scripted scenario can vary this per-failure via
    /// `ScriptedFailure::survivalWindowSec` instead.
    Seconds survivalWindowSec = 3600.0;
};

/// Schedules every `ScriptedFailure` as a `SatelliteFailure` event, in the order given
/// (the queue's own ordering, not insertion order, decides final firing order -- see
/// `EventQueue.hpp`).
void scheduleScriptedFailures(EventQueue& queue, const std::vector<ScriptedFailure>& failures);

/// Draws `config.failureCount` distinct eligible satellites from `state` (eligible: not
/// already `Failed`/`Rescued`/`Lost` -- `Satellite::isTerminal()` covers the latter two,
/// `requiresRescue()` the former) using `rng`, and schedules one `SatelliteFailure` event
/// per draw at a uniformly random time in `[0, config.durationSec]`.
///
/// @param rng Caller-supplied, already derived via `RandomGenerator::forStream
///        (RandomStream::Failures)` -- this function never constructs its own generator
///        or reads any global random state (brief §22's explicit requirement).
///
/// @throws std::invalid_argument if `config.failureCount` exceeds the number of eligible
///         satellites in `state`, or is negative.
void scheduleRandomFailures(EventQueue& queue, const SimulationState& state,
                            const RandomFailureConfig& config, RandomGenerator& rng);

}  // namespace orca
