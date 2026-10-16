#pragma once

/// @file CommandProtocol.hpp
/// Parses Node → engine NDJSON commands — Phase 9 brief §19-20, extended by Phase 11 §50.
/// Lifecycle commands the real `SimulationEngine` (Phase 8) supports: `start`, `pause`,
/// `resume`, `reset`, `step`, `advance`, `setSpeed`, `getSnapshot`, `shutdown`. No
/// `plan`/`assign`/`getGraph`/`traceAlgorithm`/`exportResults` — those would require new
/// C++ entry points into the Phase 5/6 SEARCH stack that `SimulationEngine` itself does not
/// expose (brief §20: "do not invent frontend controls for functionality that does not
/// exist"). **`rescoreParetoFront` is the one exception, added in Phase 11**: it does not
/// search — it calls `MissionScorer::selectRecommendedPlan()` directly on a mission's
/// ALREADY-CAPTURED `SimulationState::missionParetoHistory` entry (Phase 10/11) under a
/// different `OptimizationProfile`, which is real, existing, tested Phase 5 scoring logic
/// with no new transfer evaluation or search involved -- exactly the "existing front + new
/// weights -> new recommendation, no new search" property brief §50 requires.

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>

#include "JsonValue.hpp"

namespace orca::io {

enum class CommandName {
    Start,
    Pause,
    Resume,
    Reset,
    Step,
    Advance,
    SetSpeed,
    GetSnapshot,
    Shutdown,
    RescoreParetoFront,
    RunAlgorithm,
};

[[nodiscard]] std::string toString(CommandName name) noexcept;

struct Command {
    CommandName name;
    std::optional<std::uint32_t> reqId;

    /// Set only for `Advance`.
    std::optional<double> seconds;
    /// Set only for `SetSpeed`.
    std::optional<double> multiplier;
    /// Set only for `RescoreParetoFront`.
    std::optional<std::string> missionId;
    /// Set only for `RescoreParetoFront` -- one of "FASTEST" | "FUEL_SAVING" |
    /// "SAFETY_FIRST" | "BALANCED".
    std::optional<std::string> profile;
    /// Set only for `RunAlgorithm` -- one of "DIJKSTRA" | "ASTAR" | "PARETO" | "HUNGARIAN".
    std::optional<std::string> algorithm;
    /// Set only for `RunAlgorithm`'s search algorithms (Dijkstra/A*/Pareto); omitted for
    /// Hungarian, which runs on every currently available vehicle/pending emergency, not one
    /// specific pair. If omitted for a search algorithm too, the engine picks the first
    /// available vehicle/pending emergency itself (Phase 12 brief §16: a real run, not
    /// requiring the caller to already know valid IDs).
    std::optional<std::string> vehicleId;
    std::optional<std::string> emergencyId;
};

/// Thrown for a well-formed JSON object that is not a valid command (unknown `cmd`, or a
/// required parameter missing/wrong-typed) -- distinct from `JsonError`, which covers
/// malformed JSON syntax itself. The caller (`main.cpp`) maps this to a protocol-level
/// `{"type":"error","code":"UNKNOWN_COMMAND"|"INVALID_PARAMS",...}` message.
class CommandError : public std::invalid_argument {
public:
    explicit CommandError(const std::string& code, const std::string& message)
        : std::invalid_argument(message), code_(code) {}
    [[nodiscard]] const std::string& code() const noexcept { return code_; }

private:
    std::string code_;
};

/// Parses one command object. @throws CommandError; does not throw `JsonError` (the caller
/// is expected to have already parsed `line` into a `JsonValue` via `parseJson()` and
/// caught malformed-syntax errors separately).
[[nodiscard]] Command parseCommand(const JsonValue& json);

}  // namespace orca::io
