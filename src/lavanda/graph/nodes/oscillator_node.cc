#include "lavanda/graph/nodes/oscillator_node.h"

#include <cmath>
#include <memory>
#include <numbers>

namespace lavanda {

void OscillatorNode::Process(NodeProcessContext& context) noexcept {
  if (context.sample_rate_hz <= 0.0 || context.output.empty()) {
    context.output.Clear();
    return;
  }

  const double phase_increment = 2.0 * std::numbers::pi *
                                 static_cast<double>(frequency_hz_) /
                                 context.sample_rate_hz;

  for (std::uint32_t f = 0; f < context.frame_count; ++f) {
    context.output(f, 0) = static_cast<float>(std::sin(phase_)) * gain_;
    phase_ += phase_increment;
  }
}

std::unique_ptr<AudioNode> OscillatorNode::Clone() const {
  auto clone = std::make_unique<OscillatorNode>();

  clone->SetFrequency(frequency_hz_);
  clone->SetGain(gain_);

  return clone;
}

}  // namespace lavanda
