#include "lavanda/runtime/bus_system.h"

#include <algorithm>

namespace lavanda {

BusSystem::BusSystem(std::size_t user_bus_capacity,
                     std::size_t max_frames_per_block)
    : control_slots_(user_bus_capacity + 1),
      audio_slots_(user_bus_capacity + 1),
      max_frames_per_block_(std::max<std::size_t>(max_frames_per_block, 1)) {
  for (AudioSlot& slot : audio_slots_) {
    slot.accumulator =
        AudioBuffer(static_cast<std::uint32_t>(max_frames_per_block_), 2);
  }

  audio_slots_[0].active = true;
  control_slots_[0].in_use = true;
}

StatusOr<BusId> BusSystem::ReserveSlot() {
  for (std::size_t i = 0; i < control_slots_.size(); ++i) {
    if (!control_slots_[i].in_use) {
      control_slots_[i].in_use = true;
      control_slots_[i].next_generation += 1;

      BusId id;

      id.index = static_cast<std::uint32_t>(i);
      id.generation = control_slots_[i].next_generation;

      return id;
    }
  }

  return Status(ErrorCode::kResourceExhausted, "bus pool is at capacity");
}

void BusSystem::ReleaseSlot(BusId id) noexcept {
  if (id.index == 0) {
    return;
  }

  if (id.index >= control_slots_.size()) {
    return;
  }

  ControlSlot& slot = control_slots_[id.index];

  if (!slot.in_use || slot.next_generation != id.generation) {
    return;
  }

  slot.in_use = false;
}

bool BusSystem::IsValidTarget(BusId id) const noexcept {
  if (id.index >= audio_slots_.size()) {
    return false;
  }

  const AudioSlot& slot = audio_slots_[id.index];

  return slot.active && slot.generation == id.generation;
}

void BusSystem::ApplyCommand(const Command& command) noexcept {
  switch (command.type) {
    case CommandType::kCreateBus: {
      if (command.bus_id.index == 0) {
        return;
      }

      if (command.bus_id.index >= audio_slots_.size()) {
        return;
      }

      AudioSlot& slot = audio_slots_[command.bus_id.index];

      slot.generation = command.bus_id.generation;
      slot.active = true;
      slot.gain = 1.0f;

      break;
    }
    case CommandType::kDestroyBus: {
      if (command.bus_id.index == 0) {
        return;
      }

      if (!IsValidTarget(command.bus_id)) {
        return;
      }

      audio_slots_[command.bus_id.index].active = false;

      break;
    }
    case CommandType::kSetBusGain: {
      if (!IsValidTarget(command.bus_id)) {
        return;
      }

      audio_slots_[command.bus_id.index].gain = command.value;

      break;
    }
    default:
      break;
  }
}

bool BusSystem::is_active(std::size_t index) const noexcept {
  if (index >= audio_slots_.size()) {
    return false;
  }

  return audio_slots_[index].active;
}

float BusSystem::gain(std::size_t index) const noexcept {
  if (index >= audio_slots_.size()) {
    return 1.0f;
  }

  return audio_slots_[index].gain;
}

std::size_t BusSystem::ResolveBusIndex(BusId id) const noexcept {
  if (id.index < audio_slots_.size()) {
    const AudioSlot& slot = audio_slots_[id.index];

    if (slot.active && slot.generation == id.generation) {
      return id.index;
    }
  }

  return 0;
}

AudioBufferView BusSystem::MutableAccumulator(
    std::size_t index, std::uint32_t frame_count) noexcept {
  if (index >= audio_slots_.size()) {
    index = 0;
  }

  return audio_slots_[index].accumulator.View(frame_count);
}

void BusSystem::ClearActiveAccumulators(std::uint32_t frame_count) noexcept {
  for (AudioSlot& slot : audio_slots_) {
    if (slot.active) {
      slot.accumulator.View(frame_count).Clear();
    }
  }
}

}  // namespace lavanda
