#include "lavanda/runtime/voice_pool.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "lavanda/core/sample.h"
#include "lavanda/resources/audio_asset.h"
#include "lavanda/resources/resource_store.h"
#include "lavanda/runtime/mixer_math.h"
#include "lavanda/spatial/spatial_math.h"
#include "lavanda/spatial/spatial_oscillator.h"

namespace lavanda {

namespace {

std::uint32_t SourceChannelsOf(const AudioAsset* asset) noexcept {
  return asset != nullptr ? asset->channel_count() : 1;
}

}  // namespace

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
      control_slots_[i].source_channels = 1;

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

void VoicePool::RecordSourceChannels(VoiceId id,
                                     std::uint32_t channels) noexcept {
  if (id.index >= control_slots_.size()) {
    return;
  }

  ControlSlot& slot = control_slots_[id.index];

  if (!slot.in_use || slot.next_generation != id.generation) {
    return;
  }

  slot.source_channels = channels;
}

std::uint32_t VoicePool::reserved_source_channels(VoiceId id) const noexcept {
  if (id.index >= control_slots_.size()) {
    return 0;
  }

  const ControlSlot& slot = control_slots_[id.index];

  if (!slot.in_use || slot.next_generation != id.generation) {
    return 0;
  }

  return slot.source_channels;
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
  slot.cursor_frames = 0.0;
}

void VoicePool::ApplyCommand(const Command& command,
                             std::uint64_t frame) noexcept {
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

      if (slot.state != VoiceState::kPlaying) {
        slot.snap_spatial = true;
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
      slot.cursor_frames = 0.0;

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

      audio_slots_[command.voice_id.index].gain.Set(command.value);

      break;
    }
    case CommandType::kRampVoiceGain: {
      if (!IsValidTarget(command.voice_id)) {
        return;
      }

      audio_slots_[command.voice_id.index].gain.Start(frame, command.value,
                                                      command.duration_frames);

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
    case CommandType::kSetVoiceEmitterState: {
      if (!IsValidTarget(command.voice_id)) {
        return;
      }

      audio_slots_[command.voice_id.index].emitter =
          EmitterState{command.spatial.position, command.spatial.velocity};

      break;
    }
    case CommandType::kSetVoiceSpatialization: {
      if (!IsValidTarget(command.voice_id)) {
        return;
      }

      AudioSlot& slot = audio_slots_[command.voice_id.index];
      const bool enable = command.value == 1.0f;

      if (enable == slot.spatialized) {
        break;
      }

      if (!enable) {
        if (slot.asset_backed && slot.asset != nullptr) {
          const double limit = static_cast<double>(slot.asset->frame_count());

          slot.position_frames = static_cast<std::uint32_t>(
              std::clamp(std::floor(slot.cursor_frames), 0.0, limit));
        }

        slot.spatialized = false;

        break;
      }

      if (SourceChannelsOf(slot.asset) != 1) {
        break;
      }

      slot.cursor_frames = static_cast<double>(slot.position_frames);

      if (slot.state == VoiceState::kPlaying) {
        const StereoGains manual = EqualPowerPan(slot.pan, 1.0f);

        slot.spatial_left.Set(manual.left);
        slot.spatial_right.Set(manual.right);
        slot.spatial_ratio.Set(1.0f);
        slot.snap_spatial = false;
      } else {
        slot.snap_spatial = true;
      }

      slot.spatialized = true;

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

  return audio_slots_[index].gain.target();
}

GainRamp VoicePool::gain_ramp(std::size_t index) const noexcept {
  if (index >= audio_slots_.size()) {
    return GainRamp(0.0f);
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

bool VoicePool::is_spatialized(std::size_t index) const noexcept {
  return index < audio_slots_.size() && audio_slots_[index].spatialized;
}

void VoicePool::UpdateSpatial(std::size_t index, const ListenerState& listener,
                              const SpatialConfig& config,
                              std::uint32_t smoothing_frames,
                              std::uint64_t now) noexcept {
  if (index >= audio_slots_.size()) {
    return;
  }

  AudioSlot& slot = audio_slots_[index];

  if (!slot.spatialized) {
    return;
  }

  const SpatialTarget target =
      ComputeSpatialTarget(listener, slot.emitter, config);

  if (slot.snap_spatial) {
    slot.spatial_left.Set(target.gains.left);
    slot.spatial_right.Set(target.gains.right);
    slot.spatial_ratio.Set(target.playback_ratio);
    slot.snap_spatial = false;

    return;
  }

  if (target.gains.left == slot.spatial_left.target() &&
      target.gains.right == slot.spatial_right.target() &&
      target.playback_ratio == slot.spatial_ratio.target()) {
    return;
  }

  slot.spatial_left.Start(now, target.gains.left, smoothing_frames);
  slot.spatial_right.Start(now, target.gains.right, smoothing_frames);
  slot.spatial_ratio.Start(now, target.playback_ratio, smoothing_frames);
}

GainRamp VoicePool::spatial_left_ramp(std::size_t index) const noexcept {
  if (index >= audio_slots_.size()) {
    return GainRamp(0.0f);
  }

  return audio_slots_[index].spatial_left;
}

GainRamp VoicePool::spatial_right_ramp(std::size_t index) const noexcept {
  if (index >= audio_slots_.size()) {
    return GainRamp(0.0f);
  }

  return audio_slots_[index].spatial_right;
}

void VoicePool::RenderAssetSourceSpatial(std::size_t index,
                                         AudioBufferView output,
                                         std::uint64_t first_frame) noexcept {
  AudioSlot& slot = audio_slots_[index];
  const AudioAsset* asset = slot.asset;

  if (asset == nullptr || asset->channel_count() != 1 ||
      output.channel_count() != 1) {
    output.Clear();
    return;
  }

  const std::uint32_t total_frames = asset->frame_count();
  const std::uint32_t frame_count = output.frame_count();
  const double end = static_cast<double>(total_frames);
  double cursor = slot.cursor_frames;
  std::uint32_t frame = 0;

  for (; frame < frame_count && cursor < end; ++frame) {
    const std::uint32_t whole = static_cast<std::uint32_t>(cursor);
    const double fraction = cursor - static_cast<double>(whole);
    const Sample current = asset->sample(whole, 0);
    const Sample next =
        whole + 1 < total_frames ? asset->sample(whole + 1, 0) : Sample{0};

    output(frame, 0) =
        current + (next - current) * static_cast<Sample>(fraction);

    cursor +=
        static_cast<double>(slot.spatial_ratio.ValueAt(first_frame + frame));
  }

  for (; frame < frame_count; ++frame) {
    output(frame, 0) = 0.0f;
  }

  slot.cursor_frames = cursor;

  if (cursor >= end) {
    slot.cursor_frames = 0.0;
    slot.position_frames = 0;
    slot.state = VoiceState::kStopped;

    Publish(index);
  }
}

void VoicePool::RenderVoiceSourceSpatial(std::size_t index,
                                         AudioBufferView output,
                                         double sample_rate_hz,
                                         std::uint64_t first_frame) noexcept {
  if (index >= audio_slots_.size()) {
    output.Clear();
    return;
  }

  AudioSlot& slot = audio_slots_[index];

  if (slot.state != VoiceState::kPlaying || !(sample_rate_hz > 0.0) ||
      output.empty()) {
    output.Clear();
    return;
  }

  if (slot.asset_backed) {
    RenderAssetSourceSpatial(index, output, first_frame);
    return;
  }

  RenderDopplerSine(output, slot.phase, slot.frequency_hz, slot.spatial_ratio,
                    first_frame, sample_rate_hz);
}

}  // namespace lavanda
