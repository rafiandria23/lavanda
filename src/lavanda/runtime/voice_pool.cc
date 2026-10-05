#include "lavanda/runtime/voice_pool.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "lavanda/resources/audio_asset.h"
#include "lavanda/resources/resource_store.h"

namespace lavanda {

VoicePool::VoicePool(std::size_t capacity, ResourceStore* resource_store)
    : control_slots_(std::max<std::size_t>(capacity, 1)),
      audio_slots_(std::max<std::size_t>(capacity, 1)),
      published_(std::max<std::size_t>(capacity, 1)),
      resource_store_(resource_store) {}

StatusOr<VoiceId> VoicePool::ReserveSlot() {
  for (std::size_t i = 0; i < control_slots_.size(); ++i) {
    if (!control_slots_[i].in_use) {
      control_slots_[i].in_use = true;
      control_slots_[i].next_generation += 1;

      VoiceId id;

      id.index = static_cast<std::uint32_t>(i);
      id.generation = control_slots_[i].next_generation;

      return id;
    }
  }

  return Status(ErrorCode::kResourceExhausted, "voice pool is at capacity");
}

void VoicePool::ReleaseSlot(VoiceId id) noexcept {
  if (id.index >= control_slots_.size()) {
    return;
  }

  ControlSlot& slot = control_slots_[id.index];

  if (!slot.in_use || slot.next_generation != id.generation) {
    return;
  }

  slot.in_use = false;
}

bool VoicePool::IsValidTarget(VoiceId id) const noexcept {
  if (id.index >= audio_slots_.size()) {
    return false;
  }

  const AudioSlot& slot = audio_slots_[id.index];

  return slot.generation == id.generation &&
         slot.state != VoiceState::kInactive;
}

void VoicePool::Publish(std::size_t index) noexcept {
  const AudioSlot& slot = audio_slots_[index];

  const std::uint64_t packed =
      (static_cast<std::uint64_t>(slot.generation) << 8) |
      static_cast<std::uint8_t>(slot.state);

  published_[index].store(packed, std::memory_order_release);
}

VoiceState VoicePool::observed_state(VoiceId id) const noexcept {
  if (id.index >= published_.size()) {
    return VoiceState::kInactive;
  }

  const std::uint64_t packed =
      published_[id.index].load(std::memory_order_acquire);

  if (static_cast<std::uint32_t>(packed >> 8) != id.generation) {
    return VoiceState::kInactive;
  }

  return static_cast<VoiceState>(packed & 0xFFu);
}

void VoicePool::ReleaseAsset(AudioSlot& slot) noexcept {
  if (slot.asset_backed && resource_store_ != nullptr) {
    resource_store_->Unpin(slot.asset_id);
  }

  slot.asset_backed = false;
  slot.asset_id = AudioAssetId{};
  slot.asset = nullptr;
  slot.position_frames = 0;
}

void VoicePool::ApplyCommand(const Command& command) noexcept {
  switch (command.type) {
    case CommandType::kCreateVoice: {
      if (command.voice_id.index >= audio_slots_.size()) {
        return;
      }

      AudioSlot& slot = audio_slots_[command.voice_id.index];

      ReleaseAsset(slot);

      slot = AudioSlot{};

      slot.generation = command.voice_id.generation;
      slot.state = VoiceState::kStopped;

      if (command.asset_id.is_valid()) {
        slot.asset_backed = true;
        slot.asset_id = command.asset_id;
        slot.asset = resource_store_ != nullptr
                         ? resource_store_->ResolveForAudio(command.asset_id)
                         : nullptr;
      }

      Publish(command.voice_id.index);

      break;
    }
    case CommandType::kStartVoice: {
      if (!IsValidTarget(command.voice_id)) {
        return;
      }

      AudioSlot& slot = audio_slots_[command.voice_id.index];

      if (slot.asset_backed && slot.asset == nullptr) {
        return;
      }

      slot.state = VoiceState::kPlaying;

      Publish(command.voice_id.index);

      break;
    }
    case CommandType::kStopVoice: {
      if (!IsValidTarget(command.voice_id)) {
        return;
      }

      AudioSlot& slot = audio_slots_[command.voice_id.index];

      slot.state = VoiceState::kStopped;
      slot.position_frames = 0;

      Publish(command.voice_id.index);

      break;
    }
    case CommandType::kDestroyVoice: {
      if (!IsValidTarget(command.voice_id)) {
        return;
      }

      AudioSlot& slot = audio_slots_[command.voice_id.index];

      ReleaseAsset(slot);

      slot.state = VoiceState::kInactive;

      Publish(command.voice_id.index);

      break;
    }
    case CommandType::kSetVoiceGain: {
      if (!IsValidTarget(command.voice_id)) {
        return;
      }

      audio_slots_[command.voice_id.index].gain = command.value;

      break;
    }
    case CommandType::kSetVoicePan: {
      if (!IsValidTarget(command.voice_id)) {
        return;
      }

      audio_slots_[command.voice_id.index].pan = command.value;

      break;
    }
    case CommandType::kSetVoiceFrequency: {
      if (!IsValidTarget(command.voice_id)) {
        return;
      }

      audio_slots_[command.voice_id.index].frequency_hz = command.value;

      break;
    }
    case CommandType::kSetVoiceBus: {
      if (!IsValidTarget(command.voice_id)) {
        return;
      }

      audio_slots_[command.voice_id.index].target_bus = command.bus_id;

      break;
    }
    default:
      break;
  }
}

bool VoicePool::is_active(std::size_t index) const noexcept {
  if (index >= audio_slots_.size()) {
    return false;
  }

  return audio_slots_[index].state == VoiceState::kPlaying;
}

float VoicePool::gain(std::size_t index) const noexcept {
  if (index >= audio_slots_.size()) {
    return 0.0f;
  }

  return audio_slots_[index].gain;
}

float VoicePool::pan(std::size_t index) const noexcept {
  if (index >= audio_slots_.size()) {
    return 0.0f;
  }

  return audio_slots_[index].pan;
}

BusId VoicePool::target_bus(std::size_t index) const noexcept {
  if (index >= audio_slots_.size()) {
    return kMasterBusId;
  }

  return audio_slots_[index].target_bus;
}

std::uint32_t VoicePool::source_channel_count(
    std::size_t index) const noexcept {
  if (index >= audio_slots_.size()) {
    return 1;
  }

  const AudioSlot& slot = audio_slots_[index];

  if (slot.asset != nullptr) {
    return slot.asset->channel_count();
  }

  return 1;
}

void VoicePool::RenderAssetSource(std::size_t index,
                                  AudioBufferView output) noexcept {
  AudioSlot& slot = audio_slots_[index];
  const AudioAsset* asset = slot.asset;

  if (asset == nullptr || output.channel_count() != asset->channel_count()) {
    output.Clear();
    return;
  }

  const std::uint32_t channels = asset->channel_count();
  const std::uint32_t total_frames = asset->frame_count();
  const std::uint32_t remaining = total_frames - slot.position_frames;
  const std::uint32_t copy_frames = std::min(output.frame_count(), remaining);

  for (std::uint32_t frame = 0; frame < copy_frames; ++frame) {
    for (std::uint32_t channel = 0; channel < channels; ++channel) {
      output(frame, channel) =
          asset->sample(slot.position_frames + frame, channel);
    }
  }

  for (std::uint32_t frame = copy_frames; frame < output.frame_count();
       ++frame) {
    for (std::uint32_t channel = 0; channel < channels; ++channel) {
      output(frame, channel) = 0.0f;
    }
  }

  slot.position_frames += copy_frames;

  if (slot.position_frames >= total_frames) {
    slot.position_frames = 0;
    slot.state = VoiceState::kStopped;

    Publish(index);
  }
}

void VoicePool::RenderVoiceSource(std::size_t index, AudioBufferView output,
                                  double sample_rate_hz) noexcept {
  if (index >= audio_slots_.size()) {
    output.Clear();
    return;
  }

  AudioSlot& slot = audio_slots_[index];

  if (slot.state != VoiceState::kPlaying || sample_rate_hz <= 0.0 ||
      output.empty()) {
    output.Clear();
    return;
  }

  if (slot.asset_backed) {
    RenderAssetSource(index, output);
    return;
  }

  const double phase_increment = 2.0 * std::numbers::pi *
                                 static_cast<double>(slot.frequency_hz) /
                                 sample_rate_hz;

  for (std::uint32_t frame = 0; frame < output.frame_count(); ++frame) {
    output(frame, 0) = static_cast<float>(std::sin(slot.phase));
    slot.phase += phase_increment;
  }
}

}  // namespace lavanda
