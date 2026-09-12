#ifndef LAVANDA_GRAPH_GRAPH_PLAN_STORE_H_
#define LAVANDA_GRAPH_GRAPH_PLAN_STORE_H_

#include <atomic>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

#include "lavanda/core/status.h"
#include "lavanda/graph/execution_plan.h"
#include "lavanda/graph/graph.h"

namespace lavanda {

struct GraphPlanHandle {
  static constexpr std::uint32_t kInvalidIndex =
      std::numeric_limits<std::uint32_t>::max();

  std::uint32_t slot_index = kInvalidIndex;
  std::uint32_t generation = 0;

  bool is_valid() const noexcept { return slot_index != kInvalidIndex; }
};

enum class GraphPlanSlotState : std::uint8_t {
  kFree,
  kBuilding,
  kPending,
  kActive,
  kRetired,
};

class GraphPlanStore {
 public:
  explicit GraphPlanStore(std::size_t capacity);

  GraphPlanStore(const GraphPlanStore&) = delete;
  GraphPlanStore& operator=(const GraphPlanStore&) = delete;

  std::size_t capacity() const noexcept { return slots_.size(); }

  // --- Control-thread only -------------------------------------------

  StatusOr<GraphPlanHandle> BuildAndStage(const AudioGraph& graph,
                                          std::uint32_t max_frames_per_block);

  void ReleaseStagedPlan(GraphPlanHandle handle) noexcept;

  // --- Audio-thread only ----------------------------------------------

  void TryActivate(GraphPlanHandle handle) noexcept;

  GraphExecutionPlan* ActivePlan() noexcept;

 private:
  struct Slot {
    std::atomic<GraphPlanSlotState> state{GraphPlanSlotState::kFree};
    std::uint32_t generation = 0;
    std::unique_ptr<GraphExecutionPlan> plan;
  };

  std::vector<Slot> slots_;
  std::uint32_t active_slot_index_ = GraphPlanHandle::kInvalidIndex;
};

}  // namespace lavanda

#endif
