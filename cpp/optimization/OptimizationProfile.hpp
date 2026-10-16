#pragma once

/// @file OptimizationProfile.hpp
/// A weighting over the four objectives, used to pick a recommendation from an already-
/// computed `ParetoFront` — never to guide the search itself (`ParetoSearch.hpp`'s file
/// comment: no heuristic, and this is not one; it operates strictly after search
/// completes). Re-scoring under a new profile is `O(|front|)` (`OPTIMIZATION_MODEL.md`
/// §6.3), which is what makes changing a UI weight slider cheap enough to be interactive
/// without ever re-running search (Phase 5 brief §32).
///
/// ## Preset weights match `configs/profiles.json` exactly, and that duplication is named
///
/// `configs/profiles.json`'s own comment states it is "data... adding a profile requires
/// no code change" — true once `io/` (JSON loading) exists. Phase 5 does not build `io/`,
/// so the five preset weight vectors below are hardcoded to the *same numbers*, cited
/// against that file rather than invented independently. This is a temporary duplication,
/// not a second source of truth pretending otherwise: whichever phase adds JSON-backed
/// profile loading should replace these factory functions' bodies, not the file's public
/// API, and `optimization_profile_test.cpp` checks the hardcoded values against
/// `configs/profiles.json`'s literal contents so the two cannot silently drift apart.

#include <string_view>

#include "ObjectiveNormalizer.hpp"

namespace orca {

enum class ProfileKind { Fastest, FuelSaving, SafetyFirst, Balanced, Custom };

[[nodiscard]] constexpr std::string_view toString(ProfileKind kind) noexcept {
    switch (kind) {
        case ProfileKind::Fastest: return "FASTEST";
        case ProfileKind::FuelSaving: return "FUEL_SAVING";
        case ProfileKind::SafetyFirst: return "SAFETY_FIRST";
        case ProfileKind::Balanced: return "BALANCED";
        case ProfileKind::Custom: return "CUSTOM";
    }
    return "UNKNOWN";
}

/// @invariant every weight >= 0, sum > 0 (`DATA_MODEL.md` §7). Weights are stored exactly
/// as given — normalisation to sum 1 happens where they are *used* (`MissionScorer.hpp`),
/// not here, so a profile's stored weights always match what a caller can see they passed.
struct OptimizationProfile {
    ProfileKind kind;
    double wTime = 0.0;
    double wFuel = 0.0;
    double wRisk = 0.0;
    double wDistance = 0.0;

    /// @throws std::invalid_argument if any weight is negative, or all four are zero
    ///         (`ARCHITECTURE.md` §11's API-boundary rejection, Phase 5 brief §61).
    static OptimizationProfile custom(double wTime, double wFuel, double wRisk,
                                      double wDistance);

    [[nodiscard]] static OptimizationProfile fastest() noexcept;
    [[nodiscard]] static OptimizationProfile fuelSaving() noexcept;
    [[nodiscard]] static OptimizationProfile safetyFirst() noexcept;
    [[nodiscard]] static OptimizationProfile balanced() noexcept;
};

}  // namespace orca
