#include "lavanda/runtime/command_queue.h"

#include <algorithm>

namespace lavanda {

CommandQueue::CommandQueue(std::size_t capacity)
    : buffer_(std::max<std::size_t>(capacity, 1)) {}

bool CommandQueue::TryPush(const Command& command) noexcept {
  const std::size_t head = head_.load(std::memory_order_relaxed);
  const std::size_t tail = tail_.load(std::memory_order_acquire);

  if (head - tail >= buffer_.size()) {
    return false;
  }

  buffer_[head % buffer_.size()] = command;
  head_.store(head + 1, std::memory_order_release);

  return true;
}

bool CommandQueue::TryPop(Command& out_command) noexcept {
  const std::size_t tail = tail_.load(std::memory_order_relaxed);
  const std::size_t head = head_.load(std::memory_order_acquire);

  if (tail == head) {
    return false;
  }

  out_command = buffer_[tail % buffer_.size()];
  tail_.store(tail + 1, std::memory_order_release);

  return true;
}

std::size_t CommandQueue::size_approx() const noexcept {
  const std::size_t head = head_.load(std::memory_order_acquire);
  const std::size_t tail = tail_.load(std::memory_order_acquire);

  return head - tail;
}

}  // namespace lavanda
