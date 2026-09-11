#ifndef LAVANDA_GRAPH_NODE_H_
#define LAVANDA_GRAPH_NODE_H_

#include <array>
#include <cstdint>

#include "lavanda/core/audio_buffer.h"

namespace lavanda {

inline constexpr std::size_t kMaxNodeInputs = 8;

struct NodeProcessContext {
  std::array<AudioBufferView, kMaxNodeInputs> inputs{};
  std::uint32_t input_count = 0;

  AudioBufferView output;

  std::uint32_t frame_count = 0;
  double sample_rate_hz = 0.0;
  std::uint64_t clock_frame = 0;
};

class AudioNode {
 public:
  virtual ~AudioNode() = default;

  virtual void Process(NodeProcessContext& context) noexcept = 0;

  virtual std::uint32_t input_channel_count() const noexcept = 0;
  virtual std::uint32_t output_channel_count() const noexcept = 0;

  virtual std::uint32_t expected_input_count() const noexcept = 0;
};

}  // namespace lavanda

#endif
