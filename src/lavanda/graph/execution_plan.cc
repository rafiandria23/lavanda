#include "lavanda/graph/execution_plan.h"

#include <limits>
#include <utility>

namespace lavanda {

GraphExecutionPlan::GraphExecutionPlan(std::vector<Step> steps,
                                       std::vector<NodeId> step_source_ids,
                                       std::vector<AudioBuffer> buffers,
                                       std::uint32_t output_buffer_index,
                                       std::uint32_t output_channel_count,
                                       std::uint32_t max_frames_per_block,
                                       AssetPins pins)
    : pins_(std::move(pins)),
      steps_(std::move(steps)),
      step_source_ids_(std::move(step_source_ids)),
      buffers_(std::move(buffers)),
      output_buffer_index_(output_buffer_index),
      output_channel_count_(output_channel_count),
      max_frames_per_block_(max_frames_per_block) {}

std::uint32_t GraphExecutionPlan::FindStepIndexForNode(
    NodeId id) const noexcept {
  for (std::uint32_t i = 0; i < step_source_ids_.size(); ++i) {
    if (step_source_ids_[i] == id) {
      return i;
    }
  }

  return std::numeric_limits<std::uint32_t>::max();
}

}  // namespace lavanda
