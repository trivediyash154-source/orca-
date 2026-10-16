#include "EventQueue.hpp"

#include "../core/Validation.hpp"

namespace orca {

EventId EventQueue::schedule(Event event) {
    const EventId id(nextEventId_++);
    event.id = id;
    const auto it = events_.insert(std::move(event));
    byId_.emplace(id.value(), it);
    return id;
}

const Event& EventQueue::peek() const {
    requireThat(!events_.empty(), "EventQueue::peek: queue is empty");
    return *events_.begin();
}

Event EventQueue::pop() {
    requireThat(!events_.empty(), "EventQueue::pop: queue is empty");
    const auto it = events_.begin();
    Event front = *it;
    byId_.erase(front.id.value());
    events_.erase(it);
    return front;
}

bool EventQueue::cancel(EventId id) noexcept {
    const auto found = byId_.find(id.value());
    if (found == byId_.end()) {
        return false;
    }
    events_.erase(found->second);
    byId_.erase(found);
    return true;
}

bool EventQueue::contains(EventId id) const noexcept {
    return byId_.find(id.value()) != byId_.end();
}

bool EventQueue::allAtOrAfter(Seconds timestampSec) const noexcept {
    for (const Event& e : events_) {
        if (e.timestampSec < timestampSec) {
            return false;
        }
    }
    return true;
}

}  // namespace orca
