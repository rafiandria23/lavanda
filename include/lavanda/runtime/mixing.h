#ifndef LAVANDA_RUNTIME_MIXING_H_
#define LAVANDA_RUNTIME_MIXING_H_

#include "lavanda/core/status.h"
#include "lavanda/runtime/handles.h"

namespace lavanda {

class AudioRuntime;
class Bus;

class Voice {
 public:
  Voice() noexcept = default;

  bool is_valid() const noexcept {
    return runtime_ != nullptr && id_.is_valid();
  }
  VoiceId id() const noexcept { return id_; }

  Status Start();
  Status Stop();

  Status Destroy();

  Status SetGain(float gain);

  Status SetPan(float pan);

  Status SetFrequency(float frequency_hz);

  Status SetBus(BusId bus_id);
  Status SetBus(const Bus& bus);

 private:
  friend class AudioRuntime;
  Voice(AudioRuntime* runtime, VoiceId id) noexcept
      : runtime_(runtime), id_(id) {}

  AudioRuntime* runtime_ = nullptr;
  VoiceId id_;
};

class Bus {
 public:
  Bus() noexcept = default;

  bool is_valid() const noexcept {
    return runtime_ != nullptr && id_.is_valid();
  }
  BusId id() const noexcept { return id_; }

  Status Destroy();

  Status SetGain(float gain);

 private:
  friend class AudioRuntime;
  Bus(AudioRuntime* runtime, BusId id) noexcept : runtime_(runtime), id_(id) {}

  AudioRuntime* runtime_ = nullptr;
  BusId id_;
};

}  // namespace lavanda

#endif
