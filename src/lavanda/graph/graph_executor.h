#ifndef LAVANDA_GRAPH_GRAPH_EXECUTOR_H_
#define LAVANDA_GRAPH_GRAPH_EXECUTOR_H_

#include <cstdint>

#include "lavanda/core/audio_buffer.h"
#include "lavanda/graph/execution_plan.h"

namespace lavanda {

class GraphExecutor {
 public:
  GraphExecutor() = delete;

  static void Render(GraphExecutionPlan& plan, AudioBufferView output,
                     std::uint32_t frame_count, double sample_rate_hz,
                     std::uint64_t clock_frame) noexcept;
};

}  // namespace lavanda

#endif
