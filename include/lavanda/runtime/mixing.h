#ifndef LAVANDA_RUNTIME_MIXING_H_
#define LAVANDA_RUNTIME_MIXING_H_

#include <cstdint>

#include "lavanda/core/status.h"
#include "lavanda/runtime/handles.h"
#include "lavanda/spatial/spatial_state.h"

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

  bool IsPlaying() const noexcept;

  Status SetGain(float gain);
  Status RampGain(float target_gain, std::uint32_t duration_frames);

  Status SetPan(float pan);

  Status SetFrequency(float frequency_hz);

  Status SetBus(BusId bus_id);
  Status SetBus(const Bus& bus);

  Status SetEmitterState(const EmitterState& state);
  Status SetSpatialized(bool spatialized);

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
  Status RampGain(float target_gain, std::uint32_t duration_frames);

 private:
  friend class AudioRuntime;
  Bus(AudioRuntime* runtime, BusId id) noexcept : runtime_(runtime), id_(id) {}

  AudioRuntime* runtime_ = nullptr;
  BusId id_;
};

}  // namespace lavanda

#endif
