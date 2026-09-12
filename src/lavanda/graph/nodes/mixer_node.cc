#include "lavanda/graph/nodes/mixer_node.h"

#include <algorithm>
#include <memory>

#include "lavanda/runtime/mixer_math.h"

namespace lavanda {

MixerNode::MixerNode(std::uint32_t channel_count,
                     std::uint32_t input_count) noexcept
    : channel_count_(channel_count),
      input_count_(
          std::min(input_count, static_cast<std::uint32_t>(kMaxNodeInputs))) {}

void MixerNode::Process(NodeProcessContext& context) noexcept {
  context.output.Clear();

  const std::uint32_t count = std::min(context.input_count, input_count_);

  for (std::uint32_t i = 0; i < count; ++i) {
    AccumulateInto(context.output, context.inputs[i], 1.0f);
  }
}

std::unique_ptr<AudioNode> MixerNode::Clone() const {
  return std::make_unique<MixerNode>(channel_count_, input_count_);
}

}  // namespace lavanda
