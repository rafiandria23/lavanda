#ifndef LAVANDA_RUNTIME_EVENT_SCHEDULER_H_
#define LAVANDA_RUNTIME_EVENT_SCHEDULER_H_

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

#include "lavanda/core/time.h"
#include "lavanda/runtime/command.h"

namespace lavanda {

static_assert(std::is_trivially_copyable_v<Command>,
              "Command must stay trivially copyable: it crosses the SPSC queue "
              "and lives in the audio-thread event heap");

struct ScheduledEvent {
  FramePosition target_frame = FramePosition{0};
  std::uint64_t sequence = 0;
  Command command;
};

static_assert(std::is_trivially_copyable_v<ScheduledEvent>,
              "ScheduledEvent must be trivially copyable");

class EventScheduler {
 public:
  explicit EventScheduler(std::size_t capacity);

  EventScheduler(const EventScheduler&) = delete;
  EventScheduler& operator=(const EventScheduler&) = delete;

  std::size_t capacity() const noexcept { return heap_.size(); }
  std::size_t size() const noexcept { return size_; }
  bool empty() const noexcept { return size_ == 0; }

  bool Push(FramePosition target_frame, const Command& command) noexcept;

  bool PeekNextTarget(FramePosition& out_target) const noexcept;

  bool PopDue(FramePosition frame, ScheduledEvent& out_event) noexcept;

  void Clear() noexcept;

 private:
  std::vector<ScheduledEvent> heap_;
  std::size_t size_ = 0;
  std::uint64_t next_sequence_ = 0;
};

class EventReservations {
 public:
  explicit EventReservations(std::size_t capacity) noexcept
      : capacity_(capacity) {}

  EventReservations(const EventReservations&) = delete;
  EventReservations& operator=(const EventReservations&) = delete;

  bool TryReserve() noexcept;

  void Release() noexcept;

  std::size_t outstanding() const noexcept {
    return outstanding_.load(std::memory_order_acquire);
  }
  std::size_t capacity() const noexcept { return capacity_; }

 private:
  std::size_t capacity_;
  std::atomic<std::size_t> outstanding_{0};
};

}  // namespace lavanda

#endif
