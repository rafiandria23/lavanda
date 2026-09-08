#include "lavanda/core/audio_buffer.h"

#include <algorithm>

namespace lavanda {

void AudioBufferView::Clear() noexcept {
  if (empty()) return;

  std::fill(data_,
            data_ + static_cast<std::size_t>(frame_count_) * channel_count_,
            Sample{0});
}

AudioBuffer::AudioBuffer(std::uint32_t frame_count, std::uint32_t channel_count)
    : storage_(static_cast<std::size_t>(frame_count) * channel_count,
               Sample{0}),
      frame_count_(frame_count),
      channel_count_(channel_count) {}

Sample& AudioBuffer::operator()(std::uint32_t frame,
                                std::uint32_t channel) noexcept {
  return storage_[static_cast<std::size_t>(frame) * channel_count_ + channel];
}

Sample AudioBuffer::operator()(std::uint32_t frame,
                               std::uint32_t channel) const noexcept {
  return storage_[static_cast<std::size_t>(frame) * channel_count_ + channel];
}

AudioBufferView AudioBuffer::View() noexcept {
  return AudioBufferView(storage_.data(), frame_count_, channel_count_);
}

void AudioBuffer::Clear() noexcept {
  std::fill(storage_.begin(), storage_.end(), Sample{0});
}

}  // namespace lavanda
