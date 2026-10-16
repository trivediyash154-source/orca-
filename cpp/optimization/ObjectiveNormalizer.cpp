#include "ObjectiveNormalizer.hpp"

#include "../core/Validation.hpp"

namespace orca {

ObjectiveVector computeIdeal(const std::vector<ObjectiveVector>& front) {
    if (front.empty()) {
        return ObjectiveVector{};
    }
    ObjectiveVector ideal = front.front();
    for (std::size_t i = 1; i < front.size(); ++i) {
        for (std::size_t dim = 0; dim < kObjectiveCount; ++dim) {
            if (front[i][dim] < ideal[dim]) {
                ideal[dim] = front[i][dim];
            }
        }
    }
    return ideal;
}

ObjectiveVector computeNadir(const std::vector<ObjectiveVector>& front) {
    if (front.empty()) {
        return ObjectiveVector{};
    }
    ObjectiveVector nadir = front.front();
    for (std::size_t i = 1; i < front.size(); ++i) {
        for (std::size_t dim = 0; dim < kObjectiveCount; ++dim) {
            if (front[i][dim] > nadir[dim]) {
                nadir[dim] = front[i][dim];
            }
        }
    }
    return nadir;
}

NormalizedObjectives normalize(const ObjectiveVector& cost, const ObjectiveVector& ideal,
                               const ObjectiveVector& nadir) {
    NormalizedObjectives result;
    for (std::size_t dim = 0; dim < kObjectiveCount; ++dim) {
        requireThat(nadir[dim] >= ideal[dim] - kNormalizationDegenerateEpsilon,
                    "normalize: nadir must be componentwise >= ideal");
        const double spread = nadir[dim] - ideal[dim];
        const double value = (spread < kNormalizationDegenerateEpsilon)
                                 ? 0.0
                                 : (cost[dim] - ideal[dim]) / spread;
        switch (dim) {
            case 0: result.time = value; break;
            case 1: result.fuel = value; break;
            case 2: result.risk = value; break;
            case 3: result.distance = value; break;
            default: break;
        }
    }
    return result;
}

}  // namespace orca
