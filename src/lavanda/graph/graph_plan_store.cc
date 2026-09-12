#include "lavanda/graph/graph_plan_store.h"

#include <algorithm>
#include <utility>

#include "lavanda/graph/graph_compiler.h"

namespace lavanda {

GraphPlanStore::GraphPlanStore(std::size_t capacity)
    : slots_(std::max<std::size_t>(capacity, 1)) {}

StatusOr<GraphPlanHandle> GraphPlanStore::BuildAndStage(
    const AudioGraph& graph, std::uint32_t max_frames_per_block) {
  std::size_t free_index = slots_.size();

  for (std::size_t i = 0; i < slots_.size(); ++i) {
    if (slots_[i].state.load(std::memory_order_acquire) ==
        GraphPlanSlotState::kFree) {
      free_index = i;
      break;
    }
  }

  if (free_index == slots_.size()) {
    return Status(ErrorCode::kResourceExhausted,
                  "graph plan store is at capacity");
  }

  StatusOr<GraphExecutionPlan> plan_or =
      GraphCompiler::Compile(graph, max_frames_per_block);

  if (!plan_or.ok()) {
    return plan_or.status();
  }

  Slot& slot = slots_[free_index];

  slot.state.store(GraphPlanSlotState::kBuilding, std::memory_order_relaxed);
  slot.generation += 1;
  slot.plan = std::make_unique<GraphExecutionPlan>(std::move(plan_or.value()));
  slot.state.store(GraphPlanSlotState::kPending, std::memory_order_release);

  GraphPlanHandle handle;

  handle.slot_index = static_cast<std::uint32_t>(free_index);
  handle.generation = slot.generation;

  return handle;
}

void GraphPlanStore::ReleaseStagedPlan(GraphPlanHandle handle) noexcept {
  if (handle.slot_index >= slots_.size()) {
    return;
  }

  Slot& slot = slots_[handle.slot_index];

  if (slot.generation != handle.generation) {
    return;
  }

  if (slot.state.load(std::memory_order_acquire) !=
      GraphPlanSlotState::kPending) {
    return;
  }

  slot.plan.reset();
  slot.state.store(GraphPlanSlotState::kFree, std::memory_order_release);
}

void GraphPlanStore::TryActivate(GraphPlanHandle handle) noexcept {
  if (handle.slot_index >= slots_.size()) {
    return;
  }

  Slot& target = slots_[handle.slot_index];

  if (target.generation != handle.generation) {
    return;
  }

  if (target.state.load(std::memory_order_relaxed) !=
      GraphPlanSlotState::kPending) {
    return;
  }

  if (active_slot_index_ != GraphPlanHandle::kInvalidIndex) {
    Slot& previous = slots_[active_slot_index_];

    previous.state.store(GraphPlanSlotState::kRetired,
                         std::memory_order_relaxed);
    previous.state.store(GraphPlanSlotState::kFree, std::memory_order_release);
  }

  target.state.store(GraphPlanSlotState::kActive, std::memory_order_relaxed);
  active_slot_index_ = handle.slot_index;
}

GraphExecutionPlan* GraphPlanStore::ActivePlan() noexcept {
  if (active_slot_index_ == GraphPlanHandle::kInvalidIndex) {
    return nullptr;
  }

  return slots_[active_slot_index_].plan.get();
}

}  // namespace lavanda
