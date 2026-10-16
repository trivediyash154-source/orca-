#include "Subsystems.hpp"

#include <format>

namespace orca {

Subsystems Subsystems::validated(double power, double propulsion, double comms,
                                 double thermal) {
    Subsystems s{power, propulsion, comms, thermal};
    requireThat(s.allInUnitInterval(),
                std::format("Subsystems: all health values must lie in [0,1] "
                            "(power={:.4f}, propulsion={:.4f}, comms={:.4f}, thermal={:.4f})",
                            power, propulsion, comms, thermal));
    return s;
}

}  // namespace orca
