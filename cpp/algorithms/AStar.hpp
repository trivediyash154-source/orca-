#pragma once

/// @file AStar.hpp
/// Earliest-arrival A* over a time-dependent `MissionGraph` — the same problem Dijkstra
/// solves (Dijkstra.hpp), guided by the admissible lower bound in `AStarHeuristic.hpp`.
///
/// # WHAT IS SHARED WITH DIJKSTRA, AND WHY NOTHING MORE IS
///
/// `PathResult`, `PathLeg` and `SearchMetrics` are reused verbatim — they were already
/// generic data structures with nothing Dijkstra-specific baked in, so there was nothing
/// to extract. `SearchConstraints` (Dijkstra.hpp) is reused for the same reason: both
/// algorithms build the identical `TransferConstraints` from the identical five fields.
///
/// There is deliberately **no** `PathSearch`/`BaseSearch` interface. Dijkstra and A* are
/// two free functions with a similar outer shape and genuinely different internals
/// (priority key, heuristic evaluation, reopening policy) — forcing a shared base class
/// over that would be inheritance for two implementations of one abstract idea neither of
/// which needs the other to be substitutable at runtime. This was a decision made *after*
/// A* existed to compare against, per the Phase 4 brief §5, not assumed in advance.
///
/// # f = g + h, PRECISELY
///
/// `g(n)` is the **absolute arrival time** at `n` — the same quantity Dijkstra's
/// `bestArrival` is, not elapsed time since search start. `h(n)` never depends on time
/// (`AStarHeuristic.hpp` §2), so `f(n) = g(n) + h(n)` is well-defined without needing to
/// decide what "elapsed" means relative to a scenario epoch.
///
/// # REOPENING, NOT A PERMANENT CLOSED SET
///
/// The heuristic is proven admissible unconditionally but only proven *consistent* within
/// ORCA's realistic altitude range (`AStarHeuristic.hpp` §5). Under mere admissibility, a
/// node's first-popped label is not guaranteed optimal — a later relaxation can still
/// improve it. So, unlike Dijkstra, a node here is never permanently marked "done": if a
/// strictly better `g` arrives after a node has already been expanded, it is expanded
/// again. `bestG[]` plus the same lazy-deletion comparison Dijkstra uses is sufficient to
/// implement this correctly with no separate closed-set bookkeeping — see AStar.cpp.
///
/// This costs nothing in the common case: within the range where the heuristic is
/// consistent, reopening simply never triggers (matches ALGORITHMS.md §4.3's original
/// framing of "admissible but not consistent implies reopening", now with a heuristic
/// where that condition is precisely characterised rather than assumed).
///
/// # WHY THE *FIRST* GOAL POP IS STILL SAFE TO TRUST
///
/// The paragraph above says a node's first-popped label is not trustworthy in general —
/// so why does AStar.cpp break out of the loop on the *first* pop of `target`, rather than
/// draining the queue further to be sure nothing better remains? Because the target is not
/// "any node": the claim below is specifically about it, and it holds under admissibility
/// alone, with no consistency assumption and no closed-set trickery.
///
/// Let C* be the true optimal arrival time at `target`, achieved along some real path
/// v0=source, v1, ..., vk=target (edge costs here are transfer times, always >= 0, so a
/// shortest path's prefixes are themselves shortest paths to their endpoints). Suppose, at
/// the moment `target` is first popped, some node vj on that path has *not yet* had
/// `bestG[vj]` reach its true optimal value g*(vj) (if every vj already has, target is one
/// of them, and the claim holds trivially). Take the smallest such index j > 0. Node
/// v(j-1) already has `bestG` = g*(v(j-1)) by minimality of j (or is `source` itself, whose
/// bestG starts at g*(source) = startTimeSec). At the point v(j-1) was expanded with that
/// value, every outgoing edge was relaxed — including the real edge to vj on the optimal
/// path — so `bestG[vj]` <= g*(vj) after that expansion; since g*(vj) is by definition the
/// minimum over all paths, `bestG[vj]` cannot be smaller either. So vj is sitting in the
/// open queue with g = g*(vj) at that point (or was already popped with it, contradicting
/// the choice of j as not-yet-optimal). Its priority is therefore
///
///   f(vj) = g*(vj) + h(vj) <= g*(vj) + trueRemainingCost(vj -> target) = C*
///
/// by admissibility of h (`AStarHeuristic.hpp` §4 — h never overestimates true remaining
/// cost). Now, for `target` to have been popped *before* vj with some g(target) > C*, the
/// min-heap property requires f(target) <= f(vj) <= C*, i.e. g(target) <= C* (h(target)=0,
/// since the heuristic between a node and its own orbit is always 0 — same-radius case).
/// That contradicts g(target) > C*. And g(target) can never be *below* C* either, since
/// every g is the cost of some real, already-relaxed path, and C* is the minimum over all
/// of them. So g(target) = C* exactly, the first time target is popped — no further
/// draining of the queue can improve on it. This is the standard admissible-A*-with-
/// reopening optimality argument (Hart, Nilsson & Raphael 1968); re-derived here in full,
/// rather than cited, per the Phase 4 brief's requirement that reasoning be shown.

#include <functional>
#include <vector>

#include "../graph/MissionGraph.hpp"
#include "../transfer/TransferModel.hpp"
#include "AStarHeuristic.hpp"
#include "Dijkstra.hpp"  // SearchConstraints
#include "PathResult.hpp"

namespace orca {

/// A lower-bound estimator: remaining time from `current`'s orbit to `goal`'s orbit.
/// Defaults to `astar_heuristic::estimateRemainingTimeSec`. Overridable so tests can run
/// the h=0 ablation (Phase 4 brief §45-46) without a second search implementation.
using AStarHeuristicFn = std::function<Seconds(const Orbit& current, const Orbit& goal)>;

/// One node's (g, h, f) at the moment it was expanded — for tests and future
/// visualisation only (Phase 4 brief §24); never read by the search itself.
struct AStarTraceStep {
    NodeId node;
    Seconds g = 0.0;
    Seconds h = 0.0;
    Seconds f = 0.0;
    int expansionOrder = 0;  ///< 0-based; a node touched twice (reopened) appears twice
};

/// Find the earliest-arrival path from `source` to `target` using A*, departing no
/// earlier than `startTimeSec`.
///
/// Priority key is `f = g + h`; deterministic tie-break on equal `f` is: smaller `f`
/// first (by construction of the ordering), then smaller `g`, then smaller `NodeId`
/// (Phase 4 brief §18, §25) — see `EarlierFFirst` in AStar.cpp.
///
/// @param heuristic Defaults to the real, proven-admissible estimator. Pass a lambda
///        returning `0.0` to run the h=0 control (reduces to Dijkstra's search order,
///        `AStarTest.ZeroHeuristicMatchesDijkstra`).
/// @param trace When non-null, appended with one `AStarTraceStep` per node expansion, in
///        expansion order. `nullptr` (the default) costs nothing extra.
///
/// @throws std::invalid_argument if `source` or `target` is not a node in `graph`.
[[nodiscard]] PathResult findEarliestArrivalAStar(
    const MissionGraph& graph,
    const TransferModel& transferModel,
    NodeId source,
    NodeId target,
    Seconds startTimeSec,
    const SearchConstraints& constraints = {},
    AStarHeuristicFn heuristic = &astar_heuristic::estimateRemainingTimeSec,
    std::vector<AStarTraceStep>* trace = nullptr);

}  // namespace orca
