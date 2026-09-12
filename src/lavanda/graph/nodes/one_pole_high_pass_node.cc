#include "lavanda/graph/nodes/one_pole_high_pass_node.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <numbers>

namespace lavanda {

void OnePoleHighPassNode::Process(NodeProcessContext& context) noexcept {
  if (context.input_count == 0 || context.sample_rate_hz <= 0.0 ||
      context.output.empty()) {
    context.output.Clear();
    return;
  }

  const AudioBufferView& input = context.inputs[0];
  const float dt = 1.0f / static_cast<float>(context.sample_rate_hz);
  const float rc = 1.0f / (2.0f * std::numbers::pi_v<float> * cutoff_hz_);
  const float alpha = rc / (rc + dt);
  const std::uint32_t channels = std::min(channel_count_, kMaxChannels);

  for (std::uint32_t c = 0; c < channels; ++c) {
    float prev_in = previous_input_[c];
    float prev_out = previous_output_[c];

    for (std::uint32_t f = 0; f < context.frame_count; ++f) {
      const float x = input(f, c);
      const float y = alpha * (prev_out + x - prev_in);

      context.output(f, c) = y;

      prev_in = x;
      prev_out = y;
    }

    previous_input_[c] = prev_in;
    previous_output_[c] = prev_out;
  }
}

std::unique_ptr<AudioNode> OnePoleHighPassNode::Clone() const {
  auto clone = std::make_unique<OnePoleHighPassNode>(channel_count_);
  clone->SetCutoffHz(cutoff_hz_);

  return clone;
}

}  // namespace lavanda
