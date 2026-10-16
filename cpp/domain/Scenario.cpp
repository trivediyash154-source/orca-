#include "Scenario.hpp"

#include <algorithm>
#include <format>
#include <unordered_set>
#include <utility>

#include "../core/Validation.hpp"

namespace orca {

namespace {

/// Throw if any ID in `items` repeats. Duplicated IDs would make lookup ambiguous and,
/// worse, would silently produce two graph nodes for one physical object.
template <typename Container, typename Projection>
void requireUniqueIds(const Container& items, Projection idOf, std::string_view kind) {
    using IdType = decltype(idOf(*items.begin()));
    std::unordered_set<IdType> seen;
    seen.reserve(items.size());
    for (const auto& item : items) {
        const auto id = idOf(item);
        requireThat(seen.insert(id).second,
                    std::format("Scenario: duplicate {} id {}", kind, toString(id)));
    }
}

}  // namespace

Scenario::Scenario(std::string name,
                   Seed seed,
                   SimulationConfig config,
                   std::vector<Satellite> satellites,
                   std::vector<RescueVehicle> vehicles,
                   std::vector<DebrisObject> debris)
    : name_(std::move(name)),
      seed_(seed),
      config_(config),
      satellites_(std::move(satellites)),
      vehicles_(std::move(vehicles)),
      debris_(std::move(debris)) {
    requireThat(!name_.empty(), "Scenario: name must not be empty");
    requireThat(config_.timeStepSec > 0.0,
                std::format("Scenario '{}': timeStepSec ({:.4f}) must be positive", name_,
                            config_.timeStepSec));
    requireThat(config_.durationSec > 0.0,
                std::format("Scenario '{}': durationSec ({:.4f}) must be positive", name_,
                            config_.durationSec));
    requireThat(config_.snapshotRateHz > 0.0,
                std::format("Scenario '{}': snapshotRateHz ({:.4f}) must be positive",
                            name_, config_.snapshotRateHz));

    requireUniqueIds(satellites_, [](const Satellite& s) { return s.id(); }, "satellite");
    requireUniqueIds(vehicles_, [](const RescueVehicle& v) { return v.id(); }, "vehicle");
    requireUniqueIds(debris_, [](const DebrisObject& d) { return d.id(); }, "debris");
}

std::size_t Scenario::objectCount() const noexcept {
    return satellites_.size() + vehicles_.size() + debris_.size();
}

const Satellite* Scenario::findSatellite(SatelliteId id) const noexcept {
    const auto it = std::find_if(satellites_.begin(), satellites_.end(),
                                 [id](const Satellite& s) { return s.id() == id; });
    return it == satellites_.end() ? nullptr : &*it;
}

const RescueVehicle* Scenario::findVehicle(VehicleId id) const noexcept {
    const auto it = std::find_if(vehicles_.begin(), vehicles_.end(),
                                 [id](const RescueVehicle& v) { return v.id() == id; });
    return it == vehicles_.end() ? nullptr : &*it;
}

const DebrisObject* Scenario::findDebris(DebrisId id) const noexcept {
    const auto it = std::find_if(debris_.begin(), debris_.end(),
                                 [id](const DebrisObject& d) { return d.id() == id; });
    return it == debris_.end() ? nullptr : &*it;
}

}  // namespace orca
