#include "lavanda/runtime/voice_pool.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace lavanda {

VoicePool::VoicePool(std::size_t capacity)
    : control_slots_(std::max<std::size_t>(capacity, 1)),
      audio_slots_(std::max<std::size_t>(capacity, 1)) {}

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

void VoicePool::ApplyCommand(const Command& command) noexcept {
  switch (command.type) {
    case CommandType::kCreateVoice: {
      if (command.voice_id.index >= audio_slots_.size()) {
        return;
      }

      AudioSlot& slot = audio_slots_[command.voice_id.index];

      slot = AudioSlot{};

      slot.generation = command.voice_id.generation;
      slot.state = VoiceState::kStopped;

      break;
    }
    case CommandType::kStartVoice: {
      if (!IsValidTarget(command.voice_id)) {
        return;
      }

      audio_slots_[command.voice_id.index].state = VoiceState::kPlaying;

      break;
    }
    case CommandType::kStopVoice: {
      if (!IsValidTarget(command.voice_id)) {
        return;
      }

      audio_slots_[command.voice_id.index].state = VoiceState::kStopped;

      break;
    }
    case CommandType::kDestroyVoice: {
      if (!IsValidTarget(command.voice_id)) {
        return;
      }

      audio_slots_[command.voice_id.index].state = VoiceState::kInactive;

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

void VoicePool::RenderVoiceSource(std::size_t index,
                                  AudioBufferView mono_output,
                                  double sample_rate_hz) noexcept {
  if (index >= audio_slots_.size()) {
    mono_output.Clear();
    return;
  }

  AudioSlot& slot = audio_slots_[index];

  if (slot.state != VoiceState::kPlaying || sample_rate_hz <= 0.0 ||
      mono_output.empty()) {
    mono_output.Clear();
    return;
  }

  const double phase_increment = 2.0 * std::numbers::pi *
                                 static_cast<double>(slot.frequency_hz) /
                                 sample_rate_hz;

  for (std::uint32_t frame = 0; frame < mono_output.frame_count(); ++frame) {
    mono_output(frame, 0) = static_cast<float>(std::sin(slot.phase));
    slot.phase += phase_increment;
  }
}

}  // namespace lavanda
