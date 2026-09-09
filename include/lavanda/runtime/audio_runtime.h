#ifndef LAVANDA_RUNTIME_AUDIO_RUNTIME_H_
#define LAVANDA_RUNTIME_AUDIO_RUNTIME_H_

#include <cstddef>
#include <memory>

#include "lavanda/core/status.h"
#include "lavanda/device/audio_device.h"
#include "lavanda/device/device_config.h"
#include "lavanda/runtime/command.h"
#include "lavanda/runtime/runtime_stats.h"

namespace lavanda {

class AudioRuntime {
 public:
  explicit AudioRuntime(std::unique_ptr<AudioDevice> device,
                        std::size_t command_queue_capacity = 64);
  ~AudioRuntime();

  AudioRuntime(const AudioRuntime&) = delete;
  AudioRuntime& operator=(const AudioRuntime&) = delete;
  AudioRuntime(AudioRuntime&&) = delete;
  AudioRuntime& operator=(AudioRuntime&&) = delete;

  Status Start(const DeviceConfig& config = DeviceConfig());

  Status Stop();

  Status Shutdown();

  Status Submit(const Command& command);

  bool is_running() const noexcept;

  RuntimeStats stats() const noexcept;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace lavanda

#endif
