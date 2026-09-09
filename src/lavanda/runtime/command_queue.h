#ifndef LAVANDA_RUNTIME_COMMAND_QUEUE_H_
#define LAVANDA_RUNTIME_COMMAND_QUEUE_H_

#include <atomic>
#include <cstddef>
#include <vector>

#include "lavanda/runtime/command.h"

namespace lavanda {

class CommandQueue {
 public:
  explicit CommandQueue(std::size_t capacity);

  CommandQueue(const CommandQueue&) = delete;
  CommandQueue& operator=(const CommandQueue&) = delete;

  bool TryPush(const Command& command) noexcept;

  bool TryPop(Command& out_command) noexcept;

  std::size_t capacity() const noexcept { return buffer_.size(); }

  std::size_t size_approx() const noexcept;

 private:
  std::vector<Command> buffer_;
  std::atomic<std::size_t> head_{0};
  std::atomic<std::size_t> tail_{0};
};

}  // namespace lavanda

#endif
