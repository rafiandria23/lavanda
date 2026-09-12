#include "lavanda/graph/nodes/gain_node.h"

#include <memory>

namespace lavanda {

void GainNode::Process(NodeProcessContext& context) noexcept {
  if (context.input_count == 0 || context.output.empty()) {
    context.output.Clear();
    return;
  }

  const AudioBufferView input = context.inputs[0];

  for (std::uint32_t f = 0; f < context.frame_count; ++f) {
    for (std::uint32_t c = 0; c < channel_count_; ++c) {
      context.output(f, c) = input(f, c) * gain_;
    }
  }
}

std::unique_ptr<AudioNode> GainNode::Clone() const {
  auto clone = std::make_unique<GainNode>(channel_count_);
  clone->SetGain(gain_);

  return clone;
}

}  // namespace lavanda
