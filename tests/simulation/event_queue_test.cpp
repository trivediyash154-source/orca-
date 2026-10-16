/// `EventQueue` tests — Phase 8 brief §53's event-queue category, §54's same-timestamp
/// ordering test. Every ordering result below was verified directly (`probe_eventqueue.cpp`
/// during development) before being written here.

#include "../../cpp/simulation/EventQueue.hpp"
#include "../TestFramework.hpp"

using namespace orca;

namespace {
Event makeEvent(Seconds t, EventKind kind) {
    return Event{.id = EventId(0), .timestampSec = t, .kind = kind};
}
}  // namespace

ORCA_TEST(EventQueue, EmptyQueueBehavesCorrectly) {
    EventQueue q;
    CHECK(q.empty());
    CHECK_EQ(q.size(), std::size_t{0});
    CHECK_THROWS(q.peek());
    CHECK_THROWS(q.pop());
}

ORCA_TEST(EventQueue, ScheduleAssignsSequentialIds) {
    EventQueue q;
    const EventId a = q.schedule(makeEvent(10.0, EventKind::DebrisAlert));
    const EventId b = q.schedule(makeEvent(20.0, EventKind::DebrisAlert));
    const EventId c = q.schedule(makeEvent(30.0, EventKind::DebrisAlert));
    CHECK_EQ(a.value(), std::uint32_t{0});
    CHECK_EQ(b.value(), std::uint32_t{1});
    CHECK_EQ(c.value(), std::uint32_t{2});
}

ORCA_TEST(EventQueue, EarliestTimestampFiresFirst) {
    EventQueue q;
    q.schedule(makeEvent(300.0, EventKind::RescueComplete));
    q.schedule(makeEvent(100.0, EventKind::RescueComplete));
    q.schedule(makeEvent(200.0, EventKind::RescueComplete));
    CHECK_NEAR(q.pop().timestampSec, 100.0, 1e-9);
    CHECK_NEAR(q.pop().timestampSec, 200.0, 1e-9);
    CHECK_NEAR(q.pop().timestampSec, 300.0, 1e-9);
}

// The brief's own §17 worked example: at the same timestamp, environmental events fire
// before vehicle/system state changes, before mission milestones, before replanning
// triggers, before completion notifications -- regardless of scheduling order.
ORCA_TEST(EventQueue, SameTimestampFiresInDocumentedTierOrder) {
    EventQueue q;
    // Scheduled deliberately in the OPPOSITE order from how they must fire.
    q.schedule(makeEvent(600.0, EventKind::RescueComplete));       // tier 5
    q.schedule(makeEvent(600.0, EventKind::MissionReplan));        // tier 4
    q.schedule(makeEvent(600.0, EventKind::Rendezvous));           // tier 3
    q.schedule(makeEvent(600.0, EventKind::SatelliteFailure));     // tier 2
    q.schedule(makeEvent(600.0, EventKind::DebrisAlert));          // tier 1

    CHECK(q.pop().kind == EventKind::DebrisAlert);
    CHECK(q.pop().kind == EventKind::SatelliteFailure);
    CHECK(q.pop().kind == EventKind::Rendezvous);
    CHECK(q.pop().kind == EventKind::MissionReplan);
    CHECK(q.pop().kind == EventKind::RescueComplete);
}

ORCA_TEST(EventQueue, SameTimestampSameTierFiresInScheduleOrder) {
    EventQueue q;
    const EventId first = q.schedule(makeEvent(50.0, EventKind::DebrisAlert));
    const EventId second = q.schedule(makeEvent(50.0, EventKind::DebrisAlert));
    CHECK(q.pop().id.value() == first.value());
    CHECK(q.pop().id.value() == second.value());
}

ORCA_TEST(EventQueue, CancelRemovesAPendingEvent) {
    EventQueue q;
    const EventId a = q.schedule(makeEvent(10.0, EventKind::DebrisAlert));
    const EventId b = q.schedule(makeEvent(20.0, EventKind::DebrisAlert));
    CHECK(q.cancel(a));
    CHECK(!q.contains(a));
    CHECK(q.contains(b));
    CHECK_EQ(q.size(), std::size_t{1});
    CHECK(q.pop().id.value() == b.value());
}

ORCA_TEST(EventQueue, CancellingAnAlreadyFiredOrUnknownEventReturnsFalse) {
    EventQueue q;
    const EventId a = q.schedule(makeEvent(10.0, EventKind::DebrisAlert));
    CHECK(q.cancel(a));
    CHECK(!q.cancel(a));  // already cancelled
    CHECK(!q.cancel(EventId(999)));  // never scheduled
}

ORCA_TEST(EventQueue, PeekDoesNotRemove) {
    EventQueue q;
    q.schedule(makeEvent(10.0, EventKind::DebrisAlert));
    CHECK_NEAR(q.peek().timestampSec, 10.0, 1e-9);
    CHECK_EQ(q.size(), std::size_t{1});
    CHECK_NEAR(q.peek().timestampSec, 10.0, 1e-9);
}

ORCA_TEST(EventQueue, AllAtOrAfterReflectsThePendingSet) {
    EventQueue q;
    q.schedule(makeEvent(100.0, EventKind::DebrisAlert));
    q.schedule(makeEvent(200.0, EventKind::DebrisAlert));
    CHECK(q.allAtOrAfter(100.0));
    CHECK(!q.allAtOrAfter(150.0));
    (void)q.pop();
    CHECK(q.allAtOrAfter(150.0));
}

ORCA_TEST(EventQueue, EventOrderComparatorIsAStrictWeakOrdering) {
    // Same object never compares less than itself -- a basic strict-weak-ordering sanity
    // check on the comparator `std::multiset` relies on for correctness.
    const Event e = Event{.id = EventId(5), .timestampSec = 10.0, .kind = EventKind::DebrisAlert};
    const EventOrder less;
    CHECK(!less(e, e));
}
