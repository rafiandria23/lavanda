#ifndef LAVANDA_GRAPH_NODE_ID_H_
#define LAVANDA_GRAPH_NODE_ID_H_

#include <cstdint>
#include <limits>

namespace lavanda {

struct NodeId {
  static constexpr std::uint32_t kInvalidIndex =
      std::numeric_limits<std::uint32_t>::max();

  std::uint32_t index = kInvalidIndex;
  std::uint32_t generation = 0;

  bool is_valid() const noexcept { return index != kInvalidIndex; }

  bool operator==(const NodeId& other) const noexcept {
    return index == other.index && generation == other.generation;
  }
  bool operator!=(const NodeId& other) const noexcept {
    return !(*this == other);
  }
};

}  // namespace lavanda

#endif
