#pragma once

/// @file EventQueue.hpp
/// A chronological, deterministic event schedule — Phase 8 brief §13-15.
///
/// ## Ordering, precisely
///
/// `(timestampSec, eventPriorityTier(kind), id)`, ascending, all three compared in that
/// order. Timestamp is the primary key (earlier fires first); `eventPriorityTier()`
/// (`Event.hpp`) is the documented same-timestamp policy (brief §17); `id` — assigned
/// sequentially at `schedule()` time, never incidental — is the final, guaranteed-unique
/// tiebreaker, so two events of the same kind at the same instant still have one
/// well-defined order rather than depending on `std::multiset`'s insertion-order behaviour
/// for equal keys (which the standard does not actually guarantee stably across all
/// operations). This is the same "deterministic through to a guaranteed-unique final key"
/// discipline `MissionScorer.hpp`'s tie-break chain already established in Phase 5.
///
/// ## Why `std::multiset`, not `std::priority_queue`
///
/// `cancel()` needs to remove an arbitrary, already-scheduled event by ID.
/// `std::priority_queue` supports no such operation short of rebuilding the whole
/// container; `std::multiset` erases a specific iterator in `O(log n)` and still gives
/// `O(log n)` insert and `O(1)` access to the minimum via `begin()`.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <unordered_map>

#include "Event.hpp"

namespace orca {

/// The three-key ordering the file comment documents, as a comparator so it is stated
/// exactly once and used everywhere this queue's order matters.
struct EventOrder {
    [[nodiscard]] bool operator()(const Event& a, const Event& b) const noexcept {
        if (a.timestampSec != b.timestampSec) {
            return a.timestampSec < b.timestampSec;
        }
        const int tierA = eventPriorityTier(a.kind);
        const int tierB = eventPriorityTier(b.kind);
        if (tierA != tierB) {
            return tierA < tierB;
        }
        return a.id.value() < b.id.value();
    }
};

class EventQueue {
public:
    EventQueue() = default;

    /// Assigns the next sequential `EventId` (overwriting whatever `event.id` held) and
    /// inserts it. Returns the assigned ID.
    EventId schedule(Event event);

    [[nodiscard]] bool empty() const noexcept { return events_.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return events_.size(); }

    /// The earliest-ordered event, by `EventOrder`. Does not remove it.
    ///
    /// @throws std::invalid_argument if the queue is empty.
    [[nodiscard]] const Event& peek() const;

    /// Removes and returns the earliest-ordered event.
    ///
    /// @throws std::invalid_argument if the queue is empty.
    [[nodiscard]] Event pop();

    /// Removes a specific, still-pending event by ID. `false` if no such event is pending
    /// (already fired, or never scheduled) -- not an error, since a caller cancelling a
    /// debris alert that already resolved itself is a normal race, not a bug.
    bool cancel(EventId id) noexcept;

    [[nodiscard]] bool contains(EventId id) const noexcept;

    /// Every currently pending event's timestamp is `>= timestampSec`. Used by
    /// determinism tests to assert the queue never holds a past-due event across a tick
    /// boundary.
    [[nodiscard]] bool allAtOrAfter(Seconds timestampSec) const noexcept;

private:
    std::multiset<Event, EventOrder> events_;

    /// Side index from event ID to its iterator in `events_`, so `cancel()`/`contains()`
    /// do not need to know an event's timestamp/kind to locate it -- `std::multiset`
    /// iterators remain valid across inserts/erases of OTHER elements (node-based
    /// container), so caching them here is safe.
    std::unordered_map<std::uint32_t, std::multiset<Event, EventOrder>::iterator> byId_;

    std::uint32_t nextEventId_ = 0;
};

}  // namespace orca
