#ifndef LAVANDA_GRAPH_GRAPH_PLAN_STORE_H_
#define LAVANDA_GRAPH_GRAPH_PLAN_STORE_H_

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "lavanda/core/status.h"
#include "lavanda/graph/execution_plan.h"
#include "lavanda/graph/graph.h"
#include "lavanda/graph/graph_plan_handle.h"

namespace lavanda {

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

  void MarkActivationSubmitted(GraphPlanHandle handle) noexcept;

  bool ReleaseStagedPlan(GraphPlanHandle handle) noexcept;

  // --- Audio-thread only ----------------------------------------------

  bool TryActivate(GraphPlanHandle handle) noexcept;

  GraphExecutionPlan* ActivePlan() noexcept;

  std::uint32_t ActiveGeneration() const noexcept;

 private:
  struct Slot {
    std::atomic<GraphPlanSlotState> state{GraphPlanSlotState::kFree};
    std::atomic<std::uint32_t> generation{0};
    std::unique_ptr<GraphExecutionPlan> plan;
    bool activation_submitted = false;
  };

  std::vector<Slot> slots_;
  std::uint32_t active_slot_index_ = GraphPlanHandle::kInvalidIndex;
};

}  // namespace lavanda

#endif
