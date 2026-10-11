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

AudioBufferView AudioBuffer::View(std::uint32_t frame_count) noexcept {
  const std::uint32_t clamped = std::min(frame_count, frame_count_);

  return AudioBufferView(storage_.data(), clamped, channel_count_);
}

void AudioBuffer::Clear() noexcept {
  std::fill(storage_.begin(), storage_.end(), Sample{0});
}

AudioBufferView AudioBufferView::SubView(
    std::uint32_t frame_offset, std::uint32_t frame_count) const noexcept {
  if (data_ == nullptr || frame_offset >= frame_count_) {
    return AudioBufferView();
  }

  const std::uint32_t available = frame_count_ - frame_offset;

  return AudioBufferView(
      data_ + static_cast<std::size_t>(frame_offset) * channel_count_,
      std::min(frame_count, available), channel_count_);
}

}  // namespace lavanda
