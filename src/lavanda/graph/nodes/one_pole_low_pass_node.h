#ifndef LAVANDA_GRAPH_NODES_ONE_POLE_LOW_PASS_NODE_H_
#define LAVANDA_GRAPH_NODES_ONE_POLE_LOW_PASS_NODE_H_

#include <array>
#include <cstdint>

#include "lavanda/graph/node.h"

namespace lavanda {

class OnePoleLowPassNode final : public AudioNode {
 public:
  explicit OnePoleLowPassNode(std::uint32_t channel_count) noexcept
      : channel_count_(channel_count) {}

  void Process(NodeProcessContext& context) noexcept override;

  std::uint32_t input_channel_count() const noexcept override {
    return channel_count_;
  }
  std::uint32_t output_channel_count() const noexcept override {
    return channel_count_;
  }
  std::uint32_t expected_input_count() const noexcept override { return 1; }

  void SetCutoffHz(float cutoff_hz) noexcept { cutoff_hz_ = cutoff_hz; }
  float cutoff_hz() const noexcept { return cutoff_hz_; }

 private:
  static constexpr std::uint32_t kMaxChannels = 2;

  std::uint32_t channel_count_;
  float cutoff_hz_ = 1000.0f;
  std::array<float, kMaxChannels> previous_output_{};
};

}  // namespace lavanda

#endif
