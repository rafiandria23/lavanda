#include "lavanda/runtime/event_scheduler.h"

#include <algorithm>
#include <cstddef>

namespace lavanda {

namespace {

struct Later {
  bool operator()(const ScheduledEvent& a,
                  const ScheduledEvent& b) const noexcept {
    if (a.target_frame != b.target_frame) {
      return a.target_frame > b.target_frame;
    }

    return a.sequence > b.sequence;
  }
};

}  // namespace

EventScheduler::EventScheduler(std::size_t capacity) : heap_(capacity) {}

bool EventScheduler::Push(FramePosition target_frame,
                          const Command& command) noexcept {
  if (size_ >= heap_.size()) {
    return false;
  }

  heap_[size_] = ScheduledEvent{.target_frame = target_frame,
                                .sequence = next_sequence_,
                                .command = command};

  ++next_sequence_;
  ++size_;

  std::push_heap(heap_.begin(),
                 heap_.begin() + static_cast<std::ptrdiff_t>(size_), Later{});

  return true;
}

bool EventScheduler::PeekNextTarget(FramePosition& out_target) const noexcept {
  if (size_ == 0) {
    return false;
  }

  out_target = heap_.front().target_frame;

  return true;
}

bool EventScheduler::PopDue(FramePosition frame,
                            ScheduledEvent& out_event) noexcept {
  if (size_ == 0 || heap_.front().target_frame > frame) {
    return false;
  }

  std::pop_heap(heap_.begin(),
                heap_.begin() + static_cast<std::ptrdiff_t>(size_), Later{});

  --size_;
  out_event = heap_[size_];

  return true;
}

void EventScheduler::Clear() noexcept { size_ = 0; }

bool EventReservations::TryReserve() noexcept {
  std::size_t current = outstanding_.load(std::memory_order_relaxed);

  while (current < capacity_) {
    if (outstanding_.compare_exchange_weak(current, current + 1,
                                           std::memory_order_acq_rel,
                                           std::memory_order_relaxed)) {
      return true;
    }
  }

  return false;
}

void EventReservations::Release() noexcept {
  outstanding_.fetch_sub(1, std::memory_order_release);
}

}  // namespace lavanda
