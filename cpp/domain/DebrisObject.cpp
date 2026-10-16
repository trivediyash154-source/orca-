#include "DebrisObject.hpp"

#include <format>

#include "../core/Math.hpp"
#include "../core/Validation.hpp"

namespace orca {

DebrisObject::DebrisObject(DebrisId id,
                           Orbit orbit,
                           Kilometres hazardRadiusKm,
                           double densityFactor,
                           bool detected)
    : id_(id),
      orbit_(orbit),
      hazardRadiusKm_(hazardRadiusKm),
      densityFactor_(densityFactor),
      detected_(detected) {
    requireThat(hazardRadiusKm_ > 0.0,
                std::format("Debris {}: hazardRadiusKm ({:.3f}) must be positive",
                            toString(id_), hazardRadiusKm_));
    requireThat(inUnitInterval(densityFactor_),
                std::format("Debris {}: densityFactor ({:.4f}) must lie in [0,1]",
                            toString(id_), densityFactor_));
}

Vector3 DebrisObject::positionAtKm(Seconds timeSec) const noexcept {
    return orbit_.positionAtKm(timeSec);
}

void DebrisObject::markDetected() noexcept {
    detected_ = true;
}

}  // namespace orca
