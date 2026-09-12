#include "lavanda/graph/nodes/output_node.h"

namespace lavanda {

void OutputNode::Process(NodeProcessContext& context) noexcept {
  if (context.input_count == 0 || context.output.empty()) {
    context.output.Clear();
    return;
  }

  const AudioBufferView& input = context.inputs[0];

  for (std::uint32_t f = 0; f < context.frame_count; ++f) {
    for (std::uint32_t c = 0; c < channel_count_; ++c) {
      context.output(f, c) = input(f, c);
    }
  }
}

}  // namespace lavanda
