#include "EmergencyGenerator.hpp"

#include <algorithm>
#include <format>

#include "../core/Validation.hpp"

namespace orca {

namespace {

constexpr EmergencyKind kAllKinds[] = {
    EmergencyKind::PropulsionFailure, EmergencyKind::PowerFailure,
    EmergencyKind::CommsLoss,         EmergencyKind::FuelCritical,
    EmergencyKind::CollisionDamage,   EmergencyKind::Thermal,
};

constexpr Severity kAllSeverities[] = {
    Severity::Low,
    Severity::Moderate,
    Severity::High,
    Severity::Critical,
};

std::vector<SatelliteId> eligibleSatellites(const SimulationState& state) {
    std::vector<SatelliteId> eligible;
    for (const auto& [rawId, satellite] : state.satellites) {
        if (!satellite.requiresRescue() && !satellite.isTerminal()) {
            eligible.push_back(satellite.id());
        }
    }
    // Deterministic order: unordered_map iteration order is NOT specified by the standard
    // and can differ across runs/platforms even with identical contents, which would make
    // "the Nth eligible satellite" mean different things from run to run. Sorting by raw
    // ID makes the draw depend only on `rng`'s sequence, never on hash-bucket layout.
    std::sort(eligible.begin(), eligible.end(),
             [](SatelliteId a, SatelliteId b) { return a.value() < b.value(); });
    return eligible;
}

}  // namespace

void scheduleScriptedFailures(EventQueue& queue, const std::vector<ScriptedFailure>& failures) {
    for (const ScriptedFailure& f : failures) {
        queue.schedule(Event{
            .id = EventId(0),  // overwritten by schedule()
            .timestampSec = f.atSec,
            .kind = EventKind::SatelliteFailure,
            .satellite = f.satellite,
            .emergencyKind = f.kind,
            .severity = f.severity,
            .survivalWindowSec = f.survivalWindowSec,
            .detail = std::format("scripted failure: {}", toString(f.kind)),
        });
    }
}

void scheduleRandomFailures(EventQueue& queue, const SimulationState& state,
                            const RandomFailureConfig& config, RandomGenerator& rng) {
    requireThat(config.failureCount >= 0,
                "scheduleRandomFailures: failureCount must not be negative");
    std::vector<SatelliteId> pool = eligibleSatellites(state);
    requireThat(static_cast<std::size_t>(config.failureCount) <= pool.size(),
                std::format("scheduleRandomFailures: requested {} failures but only {} "
                            "eligible satellites exist",
                            config.failureCount, pool.size()));

    for (int i = 0; i < config.failureCount; ++i) {
        // Draw without replacement: pick a random remaining index, swap it to the end,
        // shrink the effective pool -- the standard Fisher-Yates-style partial shuffle,
        // using only `RandomGenerator`'s portable integer draw.
        const auto remaining = static_cast<std::int64_t>(pool.size()) - 1 -
                              static_cast<std::int64_t>(i);
        const std::int64_t pick = rng.nextIntInRange(0, remaining);
        const std::size_t pickIndex = static_cast<std::size_t>(pick);
        const std::size_t lastIndex = static_cast<std::size_t>(remaining);
        std::swap(pool[pickIndex], pool[lastIndex]);
        const SatelliteId chosen = pool[lastIndex];

        const Seconds atSec = rng.nextDoubleInRange(0.0, config.durationSec);
        const EmergencyKind kind =
            kAllKinds[static_cast<std::size_t>(rng.nextIntInRange(0, 5))];
        const Severity severity =
            kAllSeverities[static_cast<std::size_t>(rng.nextIntInRange(0, 3))];

        queue.schedule(Event{
            .id = EventId(0),
            .timestampSec = atSec,
            .kind = EventKind::SatelliteFailure,
            .satellite = chosen,
            .emergencyKind = kind,
            .severity = severity,
            .survivalWindowSec = config.survivalWindowSec,
            .detail = std::format("seeded failure: {} ({})", toString(kind),
                                  toString(severity)),
        });
    }
}

}  // namespace orca
