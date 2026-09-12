#ifndef LAVANDA_GRAPH_GRAPH_COMPILER_H_
#define LAVANDA_GRAPH_GRAPH_COMPILER_H_

#include <cstdint>

#include "lavanda/core/status.h"
#include "lavanda/graph/execution_plan.h"
#include "lavanda/graph/graph.h"

namespace lavanda {

class GraphCompiler {
 public:
  GraphCompiler() = delete;

  static StatusOr<GraphExecutionPlan> Compile(
      const AudioGraph& graph, std::uint32_t max_frames_per_block);
};

}  // namespace lavanda

#endif
