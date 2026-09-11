#ifndef LAVANDA_RUNTIME_RUNTIME_CONFIG_H_
#define LAVANDA_RUNTIME_RUNTIME_CONFIG_H_

#include <cstddef>

namespace lavanda {

struct RuntimeConfig {
  RuntimeConfig() = default;

  RuntimeConfig(std::size_t command_queue_capacity_value) noexcept
      : command_queue_capacity(command_queue_capacity_value) {}

  std::size_t command_queue_capacity = 64;

  std::size_t max_voices = 32;

  std::size_t max_user_buses = 8;

  std::size_t max_frames_per_block = 4096;
};

}  // namespace lavanda

#endif
