#include "lavanda/graph/nodes/delay_node.h"

#include <algorithm>

namespace lavanda {

DelayNode::DelayNode(std::uint32_t channel_count,
                     std::uint32_t max_delay_frames)
    : channel_count_(channel_count),
      max_delay_frames_(std::max<std::uint32_t>(max_delay_frames, 1)),
      buffer_(static_cast<std::size_t>(kMaxChannels) * max_delay_frames_,
              0.0f) {}

void DelayNode::SetDelayFrames(std::uint32_t delay_frames) noexcept {
  delay_frames_ = std::min(delay_frames, max_delay_frames_);
}

void DelayNode::Process(NodeProcessContext& context) noexcept {
  if (context.input_count == 0 || context.output.empty()) {
    context.output.Clear();
    return;
  }

  const AudioBufferView& input = context.inputs[0];
  const std::uint32_t channels = std::min(channel_count_, kMaxChannels);

  std::uint32_t write_index = write_index_;

  for (std::uint32_t f = 0; f < context.frame_count; ++f) {
    for (std::uint32_t c = 0; c < channels; ++c) {
      float* channel_buffer =
          &buffer_[static_cast<std::size_t>(c) * max_delay_frames_];
      channel_buffer[write_index] = input(f, c);

      const std::uint32_t read_index =
          (write_index + max_delay_frames_ - delay_frames_) % max_delay_frames_;

      context.output(f, c) = channel_buffer[read_index];
    }

    write_index = (write_index + 1) % max_delay_frames_;
  }

  write_index_ = write_index;
}

}  // namespace lavanda
