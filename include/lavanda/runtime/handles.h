#ifndef LAVANDA_RUNTIME_HANDLES_H_
#define LAVANDA_RUNTIME_HANDLES_H_

#include <cstdint>
#include <limits>

namespace lavanda {

struct VoiceId {
  static constexpr std::uint32_t kInvalidIndex =
      std::numeric_limits<std::uint32_t>::max();

  std::uint32_t index = kInvalidIndex;
  std::uint32_t generation = 0;

  bool is_valid() const noexcept { return index != kInvalidIndex; }

  bool operator==(const VoiceId& other) const noexcept {
    return index == other.index && generation == other.generation;
  }
  bool operator!=(const VoiceId& other) const noexcept {
    return !(*this == other);
  }
};

struct BusId {
  static constexpr std::uint32_t kInvalidIndex =
      std::numeric_limits<std::uint32_t>::max();

  std::uint32_t index = kInvalidIndex;
  std::uint32_t generation = 0;

  bool is_valid() const noexcept { return index != kInvalidIndex; }

  bool operator==(const BusId& other) const noexcept {
    return index == other.index && generation == other.generation;
  }
  bool operator!=(const BusId& other) const noexcept {
    return !(*this == other);
  }
};

inline constexpr BusId kMasterBusId{0, 0};

}  // namespace lavanda

#endif
