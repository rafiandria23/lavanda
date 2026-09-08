#ifndef LAVANDA_CORE_AUDIO_FORMAT_H_
#define LAVANDA_CORE_AUDIO_FORMAT_H_

#include <cstdint>
#include <string>

#include "lavanda/core/channel_layout.h"
#include "lavanda/core/sample.h"

namespace lavanda {

class AudioFormat {
 public:
  AudioFormat() noexcept = default;
  AudioFormat(double sample_rate_hz, std::uint32_t channel_count,
              SampleFormat sample_format,
              ChannelLayout channel_layout = ChannelLayout::kUnspecified);

  double sample_rate_hz() const noexcept { return sample_rate_hz_; }
  std::uint32_t channel_count() const noexcept { return channel_count_; }
  SampleFormat sample_format() const noexcept { return sample_format_; }
  ChannelLayout channel_layout() const noexcept { return channel_layout_; }

  std::uint32_t bytes_per_frame() const noexcept;
  bool IsValid() const noexcept;

  bool operator==(const AudioFormat& other) const noexcept;
  bool operator!=(const AudioFormat& other) const noexcept;

  std::string ToString() const;

 private:
  double sample_rate_hz_ = 0.0;
  std::uint32_t channel_count_ = 0;
  SampleFormat sample_format_ = SampleFormat::kFloat32;
  ChannelLayout channel_layout_ = ChannelLayout::kUnspecified;
};

}  // namespace lavanda

#endif
