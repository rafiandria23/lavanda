#ifndef LAVANDA_GRAPH_NODES_OSCILLATOR_NODE_H_
#define LAVANDA_GRAPH_NODES_OSCILLATOR_NODE_H_

#include "lavanda/graph/node.h"

namespace lavanda {

class OscillatorNode final : public AudioNode {
 public:
  OscillatorNode() noexcept = default;

  void Process(NodeProcessContext& context) noexcept override;

  std::uint32_t input_channel_count() const noexcept override { return 0; }
  std::uint32_t output_channel_count() const noexcept override { return 1; }
  std::uint32_t expected_input_count() const noexcept override { return 0; }

  void SetFrequency(float frequency_hz) noexcept {
    frequency_hz_ = frequency_hz;
  }
  void SetGain(float gain) noexcept { gain_ = gain; }

  float frequency_hz() const noexcept { return frequency_hz_; }
  float gain() const noexcept { return gain_; }

 private:
  float frequency_hz_ = 440.0f;
  float gain_ = 1.0f;
  double phase_ = 0.0;
};

}  // namespace lavanda

#endif
