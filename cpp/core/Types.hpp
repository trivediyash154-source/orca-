#pragma once

/// @file Types.hpp
/// Strongly-typed entity identifiers and the domain enumerations.
///
/// ## Why IDs are wrapped rather than aliased
///
/// `using SatelliteId = uint32_t` compiles happily when you pass a vehicle ID to a
/// function expecting a satellite ID. That is a silent, plausible, hard-to-find bug, and
/// the graph builder is full of places where both are in scope at once. Wrapping makes it
/// a compile error at zero runtime cost — `Id<T>` is a single `uint32_t` with no vtable
/// and no padding.
///
/// The wrapper is a template over a tag type so the six ID types cost one implementation
/// rather than six near-identical structs. This is less code than the naive version, not
/// more, which is the test for whether an abstraction is earning its place.
///
/// ## Absence
///
/// There is no "invalid ID" sentinel. ID 0 is a perfectly good ID (SAT-000 exists), and
/// sentinel values invite the bug where an uninitialised field reads as a real entity.
/// Absence is `std::optional<SatelliteId>`, which the type system enforces. The default
/// constructor is deleted for the same reason.
///
/// ## Enumerator naming
///
/// C++ enumerators are PascalCase (idiomatic). `toString()` returns the SCREAMING_SNAKE
/// form used by the JSON wire contract (API_CONTRACT.md §4), so the mapping lives in one
/// place instead of being scattered through io/.

#include <cstdint>
#include <format>
#include <functional>
#include <string>
#include <string_view>

namespace orca {

// ---------------------------------------------------------------------------
// Strongly-typed identifiers
// ---------------------------------------------------------------------------

/// A strongly-typed entity identifier.
///
/// @tparam Tag A tag struct carrying the display prefix and zero-pad width. The tag is
///             never instantiated; it exists solely to make `Id<SatelliteTag>` and
///             `Id<VehicleTag>` distinct types.
template <typename Tag>
class Id {
public:
    using Value = std::uint32_t;

    Id() = delete;  ///< Absence is std::optional<Id>, never a sentinel value.

    constexpr explicit Id(Value value) noexcept : value_(value) {}

    [[nodiscard]] constexpr Value value() const noexcept { return value_; }

    friend constexpr bool operator==(Id, Id) noexcept = default;
    friend constexpr auto operator<=>(Id, Id) noexcept = default;

private:
    Value value_;
};

/// Human-readable form, e.g. `SAT-017`. Used in logs and invariant-violation messages.
template <typename Tag>
[[nodiscard]] std::string toString(Id<Tag> id) {
    return std::format("{}-{:0{}}", Tag::kPrefix, id.value(), Tag::kWidth);
}

struct SatelliteTag {
    static constexpr std::string_view kPrefix = "SAT";
    static constexpr int kWidth = 3;
};
struct VehicleTag {
    static constexpr std::string_view kPrefix = "RV";
    static constexpr int kWidth = 2;
};
struct EmergencyTag {
    static constexpr std::string_view kPrefix = "E";
    static constexpr int kWidth = 3;
};
struct MissionTag {
    static constexpr std::string_view kPrefix = "M";
    static constexpr int kWidth = 3;
};
struct DebrisTag {
    static constexpr std::string_view kPrefix = "D";
    static constexpr int kWidth = 2;
};
struct PlanTag {
    static constexpr std::string_view kPrefix = "P";
    static constexpr int kWidth = 4;
};
struct NodeTag {
    static constexpr std::string_view kPrefix = "N";
    static constexpr int kWidth = 4;
};
struct EdgeTag {
    static constexpr std::string_view kPrefix = "EDGE";
    static constexpr int kWidth = 4;
};

using SatelliteId = Id<SatelliteTag>;
using VehicleId = Id<VehicleTag>;
using EmergencyId = Id<EmergencyTag>;
using MissionId = Id<MissionTag>;
using DebrisId = Id<DebrisTag>;
using PlanId = Id<PlanTag>;

/// Graph identifiers live in core/ rather than graph/ because `MissionPlan` (domain)
/// stores a route as a sequence of edges, and domain/ must not depend on graph/.
/// See ARCHITECTURE.md §5 for the dependency direction this preserves.
using NodeId = Id<NodeTag>;
using EdgeId = Id<EdgeTag>;

/// Phase 5: an index into `ParetoSearch`'s flat label pool, not a pointer — so predecessor
/// chains survive the pool's `std::vector` reallocating as it grows (ALGORITHMS.md §5.3).
/// Lives here rather than in algorithms/ for the same reason `NodeId`/`EdgeId` do: nothing
/// downstream needs it, but the `Id<Tag>` machinery itself is core/ vocabulary.
struct LabelTag {
    static constexpr std::string_view kPrefix = "LBL";
    static constexpr int kWidth = 6;
};
using LabelId = Id<LabelTag>;

/// Phase 8: the simulation event queue's identity, assigned sequentially at scheduling
/// time (never a memory address, never random -- `simulation/EventQueue.hpp`'s own
/// discipline for reproducible logs and deterministic same-timestamp ordering). Lives here
/// for the same reason `LabelId`/`NodeId` do: the `Id<Tag>` machinery is core/ vocabulary,
/// even though only `simulation/` uses this particular tag.
struct EventTag {
    static constexpr std::string_view kPrefix = "EVT";
    static constexpr int kWidth = 6;
};
using EventId = Id<EventTag>;

/// Seed type for all deterministic generation. See RandomGenerator.hpp.
using Seed = std::uint64_t;

// ---------------------------------------------------------------------------
// Domain enumerations
// ---------------------------------------------------------------------------

/// Lifecycle of a tracked satellite. `Rescued` and `Lost` are terminal.
enum class SatelliteStatus { Nominal, Warning, Critical, Failed, Rescued, Lost };

/// Lifecycle of a rescue vehicle.
enum class VehicleStatus { Idle, Assigned, Transferring, OnStation, Returning, Disabled };

/// What went wrong aboard a satellite.
enum class EmergencyKind {
    PropulsionFailure,
    PowerFailure,
    CommsLoss,
    FuelCritical,
    CollisionDamage,
    Thermal
};

/// How bad it is. Feeds the priority model (OPTIMIZATION_MODEL.md §7).
enum class Severity { Low, Moderate, High, Critical };

/// Lifecycle of an emergency. `Resolved`, `Expired` are terminal.
enum class EmergencyState { Pending, Assigned, InProgress, Resolved, Expired, Unreachable };

/// Lifecycle of a committed mission. `Success`, `Failed`, `Aborted` are terminal.
enum class MissionState {
    Planned,
    Launched,
    InTransit,
    Replanning,
    Rendezvous,
    Success,
    Failed,
    Aborted
};

/// Orbital band. Classification only — carries no behaviour.
enum class OrbitBand { Leo, Meo, Geo };

/// Which hard constraint made a plan infeasible (API_CONTRACT.md §4.4).
enum class ConstraintKind { DeltaVBudget, SurvivalWindow, RiskCeiling, NoAsset, Unreachable };

// ---------------------------------------------------------------------------
// Wire-format names
// ---------------------------------------------------------------------------
//
// These return the SCREAMING_SNAKE strings used by the JSON contract. Keeping the
// mapping here rather than in io/ means there is exactly one place where an enumerator
// and its wire name can drift apart.

[[nodiscard]] constexpr std::string_view toString(SatelliteStatus s) noexcept {
    switch (s) {
        case SatelliteStatus::Nominal:  return "NOMINAL";
        case SatelliteStatus::Warning:  return "WARNING";
        case SatelliteStatus::Critical: return "CRITICAL";
        case SatelliteStatus::Failed:   return "FAILED";
        case SatelliteStatus::Rescued:  return "RESCUED";
        case SatelliteStatus::Lost:     return "LOST";
    }
    return "UNKNOWN";
}

[[nodiscard]] constexpr std::string_view toString(VehicleStatus s) noexcept {
    switch (s) {
        case VehicleStatus::Idle:         return "IDLE";
        case VehicleStatus::Assigned:     return "ASSIGNED";
        case VehicleStatus::Transferring: return "TRANSFERRING";
        case VehicleStatus::OnStation:    return "ON_STATION";
        case VehicleStatus::Returning:    return "RETURNING";
        case VehicleStatus::Disabled:     return "DISABLED";
    }
    return "UNKNOWN";
}

[[nodiscard]] constexpr std::string_view toString(EmergencyKind k) noexcept {
    switch (k) {
        case EmergencyKind::PropulsionFailure: return "PROPULSION_FAILURE";
        case EmergencyKind::PowerFailure:      return "POWER_FAILURE";
        case EmergencyKind::CommsLoss:         return "COMMS_LOSS";
        case EmergencyKind::FuelCritical:      return "FUEL_CRITICAL";
        case EmergencyKind::CollisionDamage:   return "COLLISION_DAMAGE";
        case EmergencyKind::Thermal:           return "THERMAL";
    }
    return "UNKNOWN";
}

[[nodiscard]] constexpr std::string_view toString(Severity s) noexcept {
    switch (s) {
        case Severity::Low:      return "LOW";
        case Severity::Moderate: return "MODERATE";
        case Severity::High:     return "HIGH";
        case Severity::Critical: return "CRITICAL";
    }
    return "UNKNOWN";
}

[[nodiscard]] constexpr std::string_view toString(EmergencyState s) noexcept {
    switch (s) {
        case EmergencyState::Pending:     return "PENDING";
        case EmergencyState::Assigned:    return "ASSIGNED";
        case EmergencyState::InProgress:  return "IN_PROGRESS";
        case EmergencyState::Resolved:    return "RESOLVED";
        case EmergencyState::Expired:     return "EXPIRED";
        case EmergencyState::Unreachable: return "UNREACHABLE";
    }
    return "UNKNOWN";
}

[[nodiscard]] constexpr std::string_view toString(MissionState s) noexcept {
    switch (s) {
        case MissionState::Planned:    return "PLANNED";
        case MissionState::Launched:   return "LAUNCHED";
        case MissionState::InTransit:  return "IN_TRANSIT";
        case MissionState::Replanning: return "REPLANNING";
        case MissionState::Rendezvous: return "RENDEZVOUS";
        case MissionState::Success:    return "SUCCESS";
        case MissionState::Failed:     return "FAILED";
        case MissionState::Aborted:    return "ABORTED";
    }
    return "UNKNOWN";
}

[[nodiscard]] constexpr std::string_view toString(OrbitBand b) noexcept {
    switch (b) {
        case OrbitBand::Leo: return "LEO";
        case OrbitBand::Meo: return "MEO";
        case OrbitBand::Geo: return "GEO";
    }
    return "UNKNOWN";
}

[[nodiscard]] constexpr std::string_view toString(ConstraintKind k) noexcept {
    switch (k) {
        case ConstraintKind::DeltaVBudget:   return "DELTA_V_BUDGET";
        case ConstraintKind::SurvivalWindow: return "SURVIVAL_WINDOW";
        case ConstraintKind::RiskCeiling:    return "RISK_CEILING";
        case ConstraintKind::NoAsset:        return "NO_ASSET";
        case ConstraintKind::Unreachable:    return "UNREACHABLE";
    }
    return "UNKNOWN";
}

/// Numeric severity used by the priority model (OPTIMIZATION_MODEL.md §7).
[[nodiscard]] constexpr double severityScore(Severity s) noexcept {
    switch (s) {
        case Severity::Low:      return 0.25;
        case Severity::Moderate: return 0.50;
        case Severity::High:     return 0.75;
        case Severity::Critical: return 1.00;
    }
    return 0.0;
}

}  // namespace orca

// ---------------------------------------------------------------------------
// Hashing
// ---------------------------------------------------------------------------
//
// Enables ID -> entity hash maps (PRD.md §10). Note that ORCA prefers dense
// vector indexing where IDs are contiguous; this exists for the sparse cases.

template <typename Tag>
struct std::hash<orca::Id<Tag>> {
    [[nodiscard]] std::size_t operator()(orca::Id<Tag> id) const noexcept {
        return std::hash<typename orca::Id<Tag>::Value>{}(id.value());
    }
};
