#ifndef LAVANDA_RUNTIME_VOICE_POOL_H_
#define LAVANDA_RUNTIME_VOICE_POOL_H_

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "lavanda/core/audio_buffer.h"
#include "lavanda/core/status.h"
#include "lavanda/resources/audio_asset_id.h"
#include "lavanda/runtime/command.h"
#include "lavanda/runtime/gain_ramp.h"
#include "lavanda/runtime/handles.h"

namespace lavanda {

class AudioAsset;
class ResourceStore;

enum class VoiceState : std::uint8_t {
  kInactive,
  kStopped,
  kPlaying,
};

class VoicePool {
 public:
  explicit VoicePool(std::size_t capacity,
                     ResourceStore* resource_store = nullptr);

  VoicePool(const VoicePool&) = delete;
  VoicePool& operator=(const VoicePool&) = delete;

  std::size_t capacity() const noexcept { return audio_slots_.size(); }

  StatusOr<VoiceId> ReserveSlot();

  void ReleaseSlot(VoiceId id) noexcept;

  void RecordSourceChannels(VoiceId id, std::uint32_t channels) noexcept;

  std::uint32_t reserved_source_channels(VoiceId id) const noexcept;

  void ApplyCommand(const Command& command, std::uint64_t frame = 0) noexcept;

  bool is_active(std::size_t index) const noexcept;
  float gain(std::size_t index) const noexcept;
  GainRamp gain_ramp(std::size_t index) const noexcept;
  float pan(std::size_t index) const noexcept;
  BusId target_bus(std::size_t index) const noexcept;

  std::uint32_t source_channel_count(std::size_t index) const noexcept;

  void RenderVoiceSource(std::size_t index, AudioBufferView output,
                         double sample_rate_hz) noexcept;

  VoiceState observed_state(VoiceId id) const noexcept;

 private:
  struct ControlSlot {
    bool in_use = false;
    std::uint32_t next_generation = 0;
    std::uint32_t source_channels = 1;
  };

  struct AudioSlot {
    VoiceState state = VoiceState::kInactive;
    std::uint32_t generation = 0;
    float frequency_hz = 440.0f;
    GainRamp gain;
    float pan = 0.0f;
    BusId target_bus = kMasterBusId;
    double phase = 0.0;

    bool asset_backed = false;
    AudioAssetId asset_id;
    const AudioAsset* asset = nullptr;
    std::uint32_t position_frames = 0;
  };

  bool IsValidTarget(VoiceId id) const noexcept;
  void Publish(std::size_t index) noexcept;
  void ReleaseAsset(AudioSlot& slot) noexcept;
  void RenderAssetSource(std::size_t index, AudioBufferView output) noexcept;

  std::vector<ControlSlot> control_slots_;  // control-thread only
  std::vector<AudioSlot> audio_slots_;      // audio-thread only
  std::vector<std::atomic<std::uint64_t>>
      published_;  // audio writes, any reads
  ResourceStore* resource_store_;
};

}  // namespace lavanda

#endif
