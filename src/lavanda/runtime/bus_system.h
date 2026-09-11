#ifndef LAVANDA_RUNTIME_BUS_SYSTEM_H_
#define LAVANDA_RUNTIME_BUS_SYSTEM_H_

#include <cstddef>
#include <cstdint>
#include <vector>

#include "lavanda/core/audio_buffer.h"
#include "lavanda/core/status.h"
#include "lavanda/runtime/command.h"
#include "lavanda/runtime/handles.h"

namespace lavanda {

class BusSystem {
 public:
  BusSystem(std::size_t user_bus_capacity, std::size_t max_frames_per_block);

  BusSystem(const BusSystem&) = delete;
  BusSystem& operator=(const BusSystem&) = delete;

  std::size_t total_capacity() const noexcept { return audio_slots_.size(); }
  std::size_t user_bus_capacity() const noexcept {
    return audio_slots_.size() - 1;
  }
  std::size_t max_frames_per_block() const noexcept {
    return max_frames_per_block_;
  }

  StatusOr<BusId> ReserveSlot();

  void ReleaseSlot(BusId id) noexcept;

  void ApplyCommand(const Command& command) noexcept;

  bool is_active(std::size_t index) const noexcept;
  float gain(std::size_t index) const noexcept;

  std::size_t ResolveBusIndex(BusId id) const noexcept;

  AudioBufferView MutableAccumulator(std::size_t index,
                                     std::uint32_t frame_count) noexcept;

  void ClearActiveAccumulators(std::uint32_t frame_count) noexcept;

 private:
  struct ControlSlot {
    bool in_use = false;
    std::uint32_t next_generation = 0;
  };

  struct AudioSlot {
    bool active = false;
    std::uint32_t generation = 0;
    float gain = 1.0f;
    AudioBuffer accumulator;
  };

  bool IsValidTarget(BusId id) const noexcept;

  std::vector<ControlSlot> control_slots_;
  std::vector<AudioSlot> audio_slots_;
  std::size_t max_frames_per_block_;
};

}  // namespace lavanda

#endif
