#pragma once

/// @file Subsystems.hpp
/// Per-subsystem health, shared by satellites and rescue vehicles.
///
/// Four subsystems, each a health fraction in [0,1] where 1.0 is fully healthy. These
/// are not decorative: SIMULATION_MODEL.md §5.1 draws two of the five risk factors
/// directly from them — `comms` health, and the worst subsystem across the vehicle.

#include <algorithm>

#include "../core/Math.hpp"
#include "../core/Validation.hpp"

namespace orca {

/// Which subsystem a damage event targets.
enum class Subsystem { Power, Propulsion, Comms, Thermal };

[[nodiscard]] constexpr std::string_view toString(Subsystem s) noexcept {
    switch (s) {
        case Subsystem::Power:      return "POWER";
        case Subsystem::Propulsion: return "PROPULSION";
        case Subsystem::Comms:      return "COMMS";
        case Subsystem::Thermal:    return "THERMAL";
    }
    return "UNKNOWN";
}

/// Health of each subsystem, in [0,1]. 1.0 is nominal, 0.0 is failed.
struct Subsystems {
    double power = 1.0;
    double propulsion = 1.0;
    double comms = 1.0;
    double thermal = 1.0;

    /// All-healthy state.
    [[nodiscard]] static constexpr Subsystems nominal() noexcept { return Subsystems{}; }

    /// @throws std::invalid_argument if any component is outside [0,1].
    static Subsystems validated(double power, double propulsion, double comms,
                                double thermal);

    [[nodiscard]] constexpr double get(Subsystem s) const noexcept {
        switch (s) {
            case Subsystem::Power:      return power;
            case Subsystem::Propulsion: return propulsion;
            case Subsystem::Comms:      return comms;
            case Subsystem::Thermal:    return thermal;
        }
        return 0.0;
    }

    constexpr void set(Subsystem s, double value) noexcept {
        const double v = clampUnit(value);
        switch (s) {
            case Subsystem::Power:      power = v; return;
            case Subsystem::Propulsion: propulsion = v; return;
            case Subsystem::Comms:      comms = v; return;
            case Subsystem::Thermal:    thermal = v; return;
        }
    }

    /// Health of the worst subsystem. Drives status thresholds and the vehicle-health
    /// risk factor: a spacecraft is only as capable as its weakest system, so the
    /// minimum is the right aggregate rather than the mean.
    [[nodiscard]] constexpr double worst() const noexcept {
        return std::min(std::min(power, propulsion), std::min(comms, thermal));
    }

    [[nodiscard]] constexpr bool allInUnitInterval() const noexcept {
        return inUnitInterval(power) && inUnitInterval(propulsion) &&
               inUnitInterval(comms) && inUnitInterval(thermal);
    }

    friend constexpr bool operator==(const Subsystems&, const Subsystems&) noexcept = default;
};

}  // namespace orca
