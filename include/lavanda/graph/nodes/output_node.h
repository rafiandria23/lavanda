#ifndef LAVANDA_GRAPH_NODES_OUTPUT_NODE_H_
#define LAVANDA_GRAPH_NODES_OUTPUT_NODE_H_

#include <memory>

#include "lavanda/graph/node.h"

namespace lavanda {

class OutputNode final : public AudioNode {
 public:
  explicit OutputNode(std::uint32_t channel_count) noexcept
      : channel_count_(channel_count) {}

  void Process(NodeProcessContext& context) noexcept override;

  std::uint32_t input_channel_count() const noexcept override {
    return channel_count_;
  }
  std::uint32_t output_channel_count() const noexcept override {
    return channel_count_;
  }
  std::uint32_t expected_input_count() const noexcept override { return 1; }

  std::unique_ptr<AudioNode> Clone() const override;

 private:
  std::uint32_t channel_count_;
};

}  // namespace lavanda

#endif
