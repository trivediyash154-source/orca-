#pragma once

/// @file AStarHeuristic.hpp
/// The admissible, consistent lower bound A* uses for earliest-arrival search.
///
/// # WHAT THIS FUNCTION ESTIMATES, PRECISELY
///
/// A lower bound on the **additional** time to reach the goal, starting from `current`,
/// under ORCA's own transfer model — not a real-world aerospace guarantee, not a Δv
/// estimate, not orbital period, not elapsed time since search start. See §12 of the Phase
/// 4 brief: "true" means the minimum remaining cost under the *same* ORCA transfer model
/// being searched, nothing more.
///
/// It does **not** depend on the current absolute time, and that omission is a claim, not
/// an oversight: see §2 below.
///
/// # THE DERIVATION — READ THIS BEFORE CHANGING THE FORMULA
///
/// ## 1. Reducing "remaining time" to "remaining coast time"
///
/// The true remaining time along any route is `sum(wait_i + coast_i)` over its legs. Every
/// `wait_i >= 0` (PhasingModel.hpp never produces a negative wait), so
///
///     true remaining time  >=  sum(coast_i)
///
/// unconditionally, regardless of phase, inclination, or departure time. A lower bound on
/// `sum(coast_i)` alone is therefore automatically a lower bound on the true remaining
/// time. This is *why* the heuristic can ignore phase and departure time entirely and
/// still be safe — it is not that phase does not matter, it is that ignoring it can only
/// make the bound looser, never wrong.
///
/// ## 2. Why h(n) needs no time argument
///
/// Because step 1 already discards the wait term, and `coast(ra, rb)` — the Hohmann
/// half-period `pi * sqrt(((ra+rb)/2)^3 / mu)` — does not depend on when the transfer
/// happens, only on the two radii (TransferModel.hpp's second Phase 2 property). So
/// `h(n)` is a pure function of two orbits, never of the clock.
///
/// ## 3. The subtlety Phase 4 found: direct Hohmann is NOT unconditionally a valid bound
///
/// ALGORITHMS.md previously asserted `h_time(n) = coast(r(n), r(goal))` (the direct
/// Hohmann half-period) is admissible because "no impulsive two-burn transfer between
/// these radii is faster." That is true of a *single* transfer, but admissibility needs a
/// lower bound over every possible *route*, including multi-hop ones, and the two claims
/// are not the same thing.
///
/// `coast(ra, rb) = C * ((ra+rb)/2)^1.5` depends on the *average* of the two radii, not
/// their difference, so it does not satisfy the triangle inequality in general. A route
/// that dips to a lower intermediate radius before continuing can, in principle, have a
/// smaller total coast time than the direct hop — because a hop's cost only grows with the
/// *average* of its endpoints, spending part of a journey at a much lower radius can pull
/// that average down for one leg without proportionally penalising the other.
///
/// This was found and confirmed by compiling and running the actual `hohmann::compute()`
/// formula, not by the abstract algebra alone (the algebra alone was checked too, and an
/// earlier version of this derivation drew the wrong conclusion from it by conflating a
/// removable discontinuity at `ra == rb` — coast is exactly 0 there, pure plane change —
/// with the smooth curve's limit approaching it, which is not zero. The code was the
/// tie-breaker, and is why every claim below cites a verification method rather than
/// asserting the algebra alone).
///
/// **Empirically, within ORCA's realistic operating range, this never bites**: an
/// exhaustive local search over every intermediate radius found *zero* cases where a
/// detour beats the direct hop, for target separations up to 1,000,000 km — roughly 28x
/// geostationary altitude, and utterly beyond any scenario ORCA models (PRD.md scopes the
/// project to Earth-orbital rescue). The violation only appears beyond roughly
/// 1,000,000-10,000,000 km of separation, where the detour's saving is a few thousand
/// seconds against a total measured in tens of millions.
///
/// ## 4. The fix: a second, unconditionally-safe term, taken as a minimum
///
/// Rather than relying on "ORCA will never reach that regime" as the correctness argument,
/// the heuristic adds a second term that is safe *for any input, no scope caveat needed*,
/// and takes the smaller of the two:
///
///     h(n) = min( coast(r(n), r(goal)),                                 // direct
///                 coast(r(n), R_earth) + coast(R_earth, r(goal)) )      // floor detour
///
/// The second term is the cost of detouring through Earth's own surface radius — the
/// global floor every orbit in ORCA is constructed above (`Orbit::fromRadius` rejects
/// anything at or below it). Since `coast(ra, x) + coast(x, rb)`, as a function of the free
/// via-radius `x`, is strictly increasing in `x` for `x > 0` (its derivative is a sum of
/// two square roots, always positive), its infimum over every physically valid `x` is
/// approached at the smallest one — `R_earth` — and no real node can sit below that floor.
/// So no real route, of any length, can beat this term: the "dip lower to save cost" move
/// this whole derivation is defending against is structurally unavailable once the
/// reference point already *is* the lowest point anything can be.
///
/// This makes `h` **admissible for every input**, not merely within a documented range —
/// verified over an exhaustive local search across realistic altitude combinations
/// (`heuristic_admissibility_test.cpp`), and true by construction beyond it.
///
/// ## 5. Consistency
///
/// The floor-detour term is consistent unconditionally, by the same "already at the floor"
/// argument applied with `R_earth` as the fixed endpoint (verified directly: sampled
/// `(n, n', goal)` triples spanning near-Earth-surface to far beyond GEO, zero violations —
/// `heuristic_consistency_test.cpp`'s `WideRangeFarBeyondRealisticScenariosStillSatisfies-
/// Consistency`).
///
/// The direct term is **not** consistent in general — this was checked directly, not
/// assumed, and the check found real counterexamples even within ORCA's realistic range:
/// with `n` and `goal` both near GEO and `n'` near low LEO, `direct(n)` can exceed
/// `c(n,n') + direct(n')` by several thousand seconds
/// (`heuristic_consistency_test.cpp`'s `BothComponentsAreIndividuallyConsistentAcross-
/// RealisticRange` records this, rather than asserting it away). An earlier draft of this
/// section claimed the combined heuristic's consistency followed from "the minimum of two
/// individually consistent heuristics is itself consistent" — that lemma is correct as
/// stated (proof: at successor `n'`, whichever component realises `min(h1(n'),h2(n'))` is,
/// by its own consistency, an upper bound for `h(n)` too, since `h(n) <= h_i(n)` for either
/// `i`), but it does not apply here, because its precondition — *both* components
/// individually consistent — is false for `direct`. That was a genuine error in this
/// file, not a simplification; it is corrected here rather than left to be rediscovered.
///
/// What actually holds, checked directly rather than inferred from the (inapplicable)
/// lemma: `min(direct, floorDetour)` — the combined, actually-used heuristic — has zero
/// observed consistency violations within ORCA's realistic range, including a 400,000-
/// sample adversarial search deliberately concentrated on the exact regime that breaks
/// `direct` alone (source/goal near GEO, intermediate near LEO — see the scratch
/// exploration referenced in the Phase 4 report). This is an empirical, sampled property of
/// the combined function, not a proven one; no compositional argument from the parts is
/// claimed. `AStar.cpp` does not rely on consistency for correctness regardless (it uses
/// reopening, safe under admissibility alone — proven, not sampled, in AStar.hpp's
/// termination section), so this is a documented, sampled property, never a load-bearing
/// assumption.
///
/// ## 6. What is deliberately not in this formula
///
/// No inclination term. Coast time does not depend on inclination in this model — only
/// delta-v does (TransferModel.hpp) — so an inclination-based addition would not lower-
/// bound *time*, whatever else it might be a valid bound on. `ALGORITHMS.md` §4.3's
/// `h_fuel` remains the right heuristic *if* a delta-v-objective search is ever built; it
/// is not relevant here, because this A* implementation optimises time only (Phase 4
/// brief §6), and folding an unrelated objective's lower bound into a time estimate would
/// not be a lower bound on time at all.

#include "../core/Units.hpp"
#include "../domain/Orbit.hpp"

namespace orca::astar_heuristic {

/// A lower bound on the additional time to travel from `current`'s orbit to `goal`'s
/// orbit, under ORCA's transfer model. See the file comment for the full derivation.
///
/// Read-only: takes orbits by const reference, computes nothing but a return value, and
/// mutates no graph, simulation, or transfer state (Phase 4 brief §11).
[[nodiscard]] Seconds estimateRemainingTimeSec(const Orbit& current, const Orbit& goal) noexcept;

/// The two component terms and which one `estimateRemainingTimeSec` selected, exposed for
/// tests and future diagnostics (Phase 4 brief §24) — never used by the search itself,
/// which only needs the combined value.
struct Breakdown {
    Seconds directHohmannSec = 0.0;
    Seconds floorDetourSec = 0.0;
    Seconds chosenSec = 0.0;
    bool floorDetourWasTighter = false;
};

[[nodiscard]] Breakdown computeBreakdown(const Orbit& current, const Orbit& goal) noexcept;

}  // namespace orca::astar_heuristic
