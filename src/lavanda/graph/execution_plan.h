#ifndef LAVANDA_GRAPH_EXECUTION_PLAN_H_
#define LAVANDA_GRAPH_EXECUTION_PLAN_H_

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "lavanda/core/audio_buffer.h"
#include "lavanda/graph/node.h"

namespace lavanda {

class GraphExecutionPlan {
 public:
  struct Step {
    std::unique_ptr<AudioNode> node;
    std::uint32_t output_buffer_index = 0;
    std::array<std::uint32_t, kMaxNodeInputs> input_buffer_indices{};
    std::uint32_t input_count = 0;
  };

  GraphExecutionPlan(std::vector<Step> steps, std::vector<AudioBuffer> buffers,
                     std::uint32_t output_buffer_index,
                     std::uint32_t output_channel_count,
                     std::uint32_t max_frames_per_block);

  GraphExecutionPlan(const GraphExecutionPlan&) = delete;
  GraphExecutionPlan& operator=(const GraphExecutionPlan&) = delete;
  GraphExecutionPlan(GraphExecutionPlan&&) = default;
  GraphExecutionPlan& operator=(GraphExecutionPlan&&) = default;

  const std::vector<Step>& steps() const noexcept { return steps_; }

  std::vector<AudioBuffer>& buffers() noexcept { return buffers_; }
  std::uint32_t output_buffer_index() const noexcept {
    return output_buffer_index_;
  }
  std::uint32_t output_channel_count() const noexcept {
    return output_channel_count_;
  }
  std::uint32_t max_frames_per_block() const noexcept {
    return max_frames_per_block_;
  }
  std::uint32_t step_count() const noexcept {
    return static_cast<std::uint32_t>(steps_.size());
  }
  std::uint32_t buffer_count() const noexcept {
    return static_cast<std::uint32_t>(buffers_.size());
  }

 private:
  std::vector<Step> steps_;
  std::vector<AudioBuffer> buffers_;
  std::uint32_t output_buffer_index_;
  std::uint32_t output_channel_count_;
  std::uint32_t max_frames_per_block_;
};

}  // namespace lavanda

#endif
