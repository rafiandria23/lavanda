#ifndef LAVANDA_CORE_AUDIO_BUFFER_H_
#define LAVANDA_CORE_AUDIO_BUFFER_H_

#include <cstddef>
#include <cstdint>
#include <vector>

#include "lavanda/core/sample.h"

namespace lavanda {

class AudioBufferView {
 public:
  AudioBufferView() noexcept = default;
  AudioBufferView(Sample* data, std::uint32_t frame_count,
                  std::uint32_t channel_count) noexcept
      : data_(data), frame_count_(frame_count), channel_count_(channel_count) {}

  std::uint32_t frame_count() const noexcept { return frame_count_; }
  std::uint32_t channel_count() const noexcept { return channel_count_; }

  Sample& operator()(std::uint32_t frame, std::uint32_t channel) noexcept {
    return data_[static_cast<std::size_t>(frame) * channel_count_ + channel];
  }
  Sample operator()(std::uint32_t frame, std::uint32_t channel) const noexcept {
    return data_[static_cast<std::size_t>(frame) * channel_count_ + channel];
  }

  void Clear() noexcept;

  bool empty() const noexcept {
    return data_ == nullptr || frame_count_ == 0 || channel_count_ == 0;
  }

 private:
  Sample* data_ = nullptr;
  std::uint32_t frame_count_ = 0;
  std::uint32_t channel_count_ = 0;
};

class AudioBuffer {
 public:
  AudioBuffer() noexcept = default;
  AudioBuffer(std::uint32_t frame_count, std::uint32_t channel_count);

  std::uint32_t frame_count() const noexcept { return frame_count_; }
  std::uint32_t channel_count() const noexcept { return channel_count_; }

  Sample& operator()(std::uint32_t frame, std::uint32_t channel) noexcept;
  Sample operator()(std::uint32_t frame, std::uint32_t channel) const noexcept;

  AudioBufferView View() noexcept;
  void Clear() noexcept;

 private:
  std::vector<Sample> storage_;
  std::uint32_t frame_count_ = 0;
  std::uint32_t channel_count_ = 0;
};

}  // namespace lavanda

#endif
