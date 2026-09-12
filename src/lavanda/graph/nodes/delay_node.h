#ifndef LAVANDA_GRAPH_NODES_DELAY_NODE_H_
#define LAVANDA_GRAPH_NODES_DELAY_NODE_H_

#include <cstdint>
#include <memory>
#include <vector>

#include "lavanda/graph/node.h"

namespace lavanda {

class DelayNode final : public AudioNode {
 public:
  DelayNode(std::uint32_t channel_count, std::uint32_t max_delay_frames);

  void Process(NodeProcessContext& context) noexcept override;

  std::uint32_t input_channel_count() const noexcept override {
    return channel_count_;
  }
  std::uint32_t output_channel_count() const noexcept override {
    return channel_count_;
  }
  std::uint32_t expected_input_count() const noexcept override { return 1; }

  std::unique_ptr<AudioNode> Clone() const override;

  void SetDelayFrames(std::uint32_t delay_frames) noexcept;
  std::uint32_t delay_frames() const noexcept { return delay_frames_; }
  std::uint32_t max_delay_frames() const noexcept { return max_delay_frames_; }

 private:
  static constexpr std::uint32_t kMaxChannels = 2;

  std::uint32_t channel_count_;
  std::uint32_t max_delay_frames_;
  std::uint32_t delay_frames_ = 0;
  std::uint32_t write_index_ = 0;
  std::vector<float> buffer_;
};

}  // namespace lavanda

#endif
