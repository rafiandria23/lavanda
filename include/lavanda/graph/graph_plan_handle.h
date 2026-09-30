#ifndef LAVANDA_GRAPH_GRAPH_PLAN_HANDLE_H_
#define LAVANDA_GRAPH_GRAPH_PLAN_HANDLE_H_

#include <cstdint>
#include <limits>

namespace lavanda {

struct GraphPlanHandle {
  static constexpr std::uint32_t kInvalidIndex =
      std::numeric_limits<std::uint32_t>::max();

  std::uint32_t slot_index = kInvalidIndex;
  std::uint32_t generation = 0;

  bool is_valid() const noexcept { return slot_index != kInvalidIndex; }
};

}  // namespace lavanda

#endif
