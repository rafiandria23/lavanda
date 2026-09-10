#ifndef LAVANDA_RUNTIME_VOICE_POOL_H_
#define LAVANDA_RUNTIME_VOICE_POOL_H_

#include <cstddef>
#include <cstdint>
#include <vector>

#include "lavanda/core/audio_buffer.h"
#include "lavanda/core/status.h"
#include "lavanda/runtime/command.h"
#include "lavanda/runtime/handles.h"

namespace lavanda {

enum class VoiceState : std::uint8_t {
  kInactive,
  kStopped,
  kPlaying,
};

class VoicePool {
 public:
  explicit VoicePool(std::size_t capacity);

  VoicePool(const VoicePool&) = delete;
  VoicePool& operator=(const VoicePool&) = delete;

  std::size_t capacity() const noexcept { return audio_slots_.size(); }

  StatusOr<VoiceId> ReserveSlot();

  void ReleaseSlot(VoiceId id) noexcept;

  void ApplyCommand(const Command& command) noexcept;

  bool is_active(std::size_t index) const noexcept;
  float gain(std::size_t index) const noexcept;
  float pan(std::size_t index) const noexcept;
  BusId target_bus(std::size_t index) const noexcept;

  void RenderVoiceSource(std::size_t index, AudioBufferView mono_output,
                         double sample_rate_hz) noexcept;

 private:
  struct ControlSlot {
    bool in_use = false;
    std::uint32_t next_generation = 0;
  };

  struct AudioSlot {
    VoiceState state = VoiceState::kInactive;
    std::uint32_t generation = 0;
    float frequency_hz = 440.0f;
    float gain = 1.0f;
    float pan = 0.0f;
    BusId target_bus = kMasterBusId;
    double phase = 0.0;
  };

  bool IsValidTarget(VoiceId id) const noexcept;

  std::vector<ControlSlot> control_slots_;  // control-thread only
  std::vector<AudioSlot> audio_slots_;      // audio-thread only
};

}  // namespace lavanda

#endif
