#ifndef LAVANDA_GRAPH_NODES_PAN_NODE_H_
#define LAVANDA_GRAPH_NODES_PAN_NODE_H_

#include <memory>

#include "lavanda/graph/node.h"

namespace lavanda {

class PanNode final : public AudioNode {
 public:
  PanNode() noexcept = default;

  void Process(NodeProcessContext& context) noexcept override;

  std::uint32_t input_channel_count() const noexcept override { return 1; }
  std::uint32_t output_channel_count() const noexcept override { return 2; }
  std::uint32_t expected_input_count() const noexcept override { return 1; }

  std::unique_ptr<AudioNode> Clone() const override;

  void SetPan(float pan) noexcept { pan_ = pan; }
  float pan() const noexcept { return pan_; }

 private:
  float pan_ = 0.0f;
};

}  // namespace lavanda

#endif
