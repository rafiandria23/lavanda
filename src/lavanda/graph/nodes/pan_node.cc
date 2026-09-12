#include "lavanda/graph/nodes/pan_node.h"

#include <memory>

#include "lavanda/runtime/mixer_math.h"

namespace lavanda {

void PanNode::Process(NodeProcessContext& context) noexcept {
  context.output.Clear();

  if (context.input_count == 0) {
    return;
  }

  StereoGains gains = EqualPowerPan(pan_, 1.0f);

  AccumulateMonoToStereo(context.output, context.inputs[0], gains);
}

std::unique_ptr<AudioNode> PanNode::Clone() const {
  auto clone = std::make_unique<PanNode>();

  clone->SetPan(pan_);

  return clone;
}

}  // namespace lavanda
