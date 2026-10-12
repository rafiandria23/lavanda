#ifndef LAVANDA_RUNTIME_COMMAND_QUEUE_H_
#define LAVANDA_RUNTIME_COMMAND_QUEUE_H_

#include <atomic>
#include <cstddef>
#include <type_traits>
#include <vector>

#include "lavanda/core/time.h"
#include "lavanda/runtime/command.h"

namespace lavanda {

struct QueuedCommand {
  Command command;
  FramePosition target_frame = FramePosition{0};
  bool scheduled = false;
};

static_assert(std::is_trivially_copyable_v<QueuedCommand>,
              "QueuedCommand crosses the SPSC queue by value");

class CommandQueue {
 public:
  explicit CommandQueue(std::size_t capacity);

  CommandQueue(const CommandQueue&) = delete;
  CommandQueue& operator=(const CommandQueue&) = delete;

  bool TryPush(const Command& command) noexcept;
  bool TryPush(const QueuedCommand& queued) noexcept;

  bool TryPop(QueuedCommand& out_queued) noexcept;
  bool TryPop(Command& out_command) noexcept;

  std::size_t capacity() const noexcept { return buffer_.size(); }

  std::size_t size_approx() const noexcept;

 private:
  std::vector<QueuedCommand> buffer_;
  std::atomic<std::size_t> head_{0};
  std::atomic<std::size_t> tail_{0};
};

}  // namespace lavanda

#endif
