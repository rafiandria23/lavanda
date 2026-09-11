#include "lavanda/graph/nodes/one_pole_low_pass_node.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace lavanda {

void OnePoleLowPassNode::Process(NodeProcessContext& context) noexcept {
  if (context.input_count == 0 || context.sample_rate_hz <= 0.0 ||
      context.output.empty()) {
    context.output.Clear();
    return;
  }

  const AudioBufferView& input = context.inputs[0];
  const float alpha =
      1.0f - std::exp(-2.0f * std::numbers::pi_v<float> * cutoff_hz_ /
                      static_cast<float>(context.sample_rate_hz));
  const std::uint32_t channels = std::min(channel_count_, kMaxChannels);

  for (std::uint32_t c = 0; c < channels; ++c) {
    float y = previous_output_[c];

    for (std::uint32_t f = 0; f < context.frame_count; ++f) {
      y = y + alpha * (input(f, c) - y);
      context.output(f, c) = y;
    }

    previous_output_[c] = y;
  }
}

}  // namespace lavanda
