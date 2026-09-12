#include "lavanda/graph/execution_plan.h"

#include <utility>

namespace lavanda {

GraphExecutionPlan::GraphExecutionPlan(std::vector<Step> steps,
                                       std::vector<AudioBuffer> buffers,
                                       std::uint32_t output_buffer_index,
                                       std::uint32_t output_channel_count,
                                       std::uint32_t max_frames_per_block)
    : steps_(std::move(steps)),
      buffers_(std::move(buffers)),
      output_buffer_index_(output_buffer_index),
      output_channel_count_(output_channel_count),
      max_frames_per_block_(max_frames_per_block) {}

}  // namespace lavanda
