#ifndef LAVANDA_RUNTIME_AUDIO_RUNTIME_H_
#define LAVANDA_RUNTIME_AUDIO_RUNTIME_H_

#include <memory>

#include "lavanda/core/status.h"
#include "lavanda/device/audio_device.h"
#include "lavanda/device/device_config.h"
#include "lavanda/runtime/command.h"
#include "lavanda/runtime/mixing.h"
#include "lavanda/runtime/runtime_config.h"
#include "lavanda/runtime/runtime_stats.h"

namespace lavanda {

class AudioRuntime {
 public:
  explicit AudioRuntime(std::unique_ptr<AudioDevice> device,
                        RuntimeConfig config = RuntimeConfig());
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

  StatusOr<Voice> CreateVoice();

  StatusOr<Bus> CreateBus();

  Bus MasterBus() noexcept;

 private:
  friend class Voice;
  friend class Bus;
  void ReleaseVoiceReservation(VoiceId id) noexcept;
  void ReleaseBusReservation(BusId id) noexcept;

  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace lavanda

#endif
