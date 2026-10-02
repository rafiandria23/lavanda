#ifndef LAVANDA_GRAPH_NODES_MIXER_NODE_H_
#define LAVANDA_GRAPH_NODES_MIXER_NODE_H_

#include <memory>

#include "lavanda/graph/node.h"

namespace lavanda {

class MixerNode final : public AudioNode {
 public:
  MixerNode(std::uint32_t channel_count, std::uint32_t input_count) noexcept;

  void Process(NodeProcessContext& context) noexcept override;

  std::uint32_t input_channel_count() const noexcept override {
    return channel_count_;
  }
  std::uint32_t output_channel_count() const noexcept override {
    return channel_count_;
  }
  std::uint32_t expected_input_count() const noexcept override {
    return input_count_;
  }

  std::unique_ptr<AudioNode> Clone() const override;

 private:
  std::uint32_t channel_count_;
  std::uint32_t input_count_;
};

}  // namespace lavanda

#endif
