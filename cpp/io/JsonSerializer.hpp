#pragma once

/// @file JsonSerializer.hpp
/// Converts `SimulationView.hpp`'s external types to `JsonValue` trees, and those to
/// compact NDJSON-ready text — Phase 9 brief §16-17. Field names and shapes here ARE the
/// wire contract `docs/API_CONTRACT.md`'s Phase 9 section documents; the two must be kept
/// in agreement by hand (no schema-generation tooling exists in this project).

#include <cstdint>
#include <optional>
#include <string>

#include "JsonValue.hpp"
#include "SimulationView.hpp"

namespace orca::io {

[[nodiscard]] JsonValue toJson(const SimulationSnapshot& snapshot);

/// Serializes `snapshot` as a `{"type":"snapshot",...}` message, optionally echoing
/// `reqId` (present when this is a direct reply to a command, absent for an unsolicited
/// push -- `API_CONTRACT.md` §2's "commands expecting a reply carry reqId" rule). Returns
/// one line of compact JSON with no trailing newline; the caller (`main.cpp`'s protocol
/// loop) appends `\n` when writing to stdout.
[[nodiscard]] std::string serializeSnapshotMessage(const SimulationSnapshot& snapshot,
                                                   std::optional<std::uint32_t> reqId = std::nullopt);

/// The `{"type":"ready",...}` message emitted once, immediately after the engine
/// initializes and before the first command is read (brief §28).
[[nodiscard]] std::string serializeReadyMessage(const std::string& engineVersion, int schemaVersion,
                                                const std::vector<std::string>& capabilities);

/// A protocol-level `{"type":"error",...}` message (brief §27). Domain-level outcomes are
/// never represented this way -- this project's engine has no "infeasible" command result
/// to report, since every exposed command is a lifecycle control, not a planning request
/// (`INTEGRATION_ARCHITECTURE.md` explains why the Phase 0 `planResult`-style error split
/// does not apply here).
[[nodiscard]] std::string serializeErrorMessage(const std::string& code, const std::string& message,
                                                std::optional<std::uint32_t> reqId = std::nullopt);

}  // namespace orca::io
