#include "lavanda/core/audio_format.h"

#include <sstream>

namespace lavanda {

AudioFormat::AudioFormat(double sample_rate_hz, std::uint32_t channel_count,
                         SampleFormat sample_format,
                         ChannelLayout channel_layout)
    : sample_rate_hz_(sample_rate_hz),
      channel_count_(channel_count),
      sample_format_(sample_format),
      channel_layout_(channel_layout) {}

std::uint32_t AudioFormat::bytes_per_frame() const noexcept {
  return channel_count_ *
         static_cast<std::uint32_t>(SampleSizeBytes(sample_format_));
}

bool AudioFormat::IsValid() const noexcept {
  return sample_rate_hz_ > 0.0 && channel_count_ > 0;
}

bool AudioFormat::operator==(const AudioFormat& other) const noexcept {
  return sample_rate_hz_ == other.sample_rate_hz_ &&
         channel_count_ == other.channel_count_ &&
         sample_format_ == other.sample_format_ &&
         channel_layout_ == other.channel_layout_;
}

bool AudioFormat::operator!=(const AudioFormat& other) const noexcept {
  return !(*this == other);
}

std::string AudioFormat::ToString() const {
  const char* format_name = "unknown";

  switch (sample_format_) {
    case SampleFormat::kFloat32:
      format_name = "float32";
      break;
    case SampleFormat::kInt16:
      format_name = "int16";
      break;
    case SampleFormat::kInt32:
      format_name = "int32";
      break;
  }

  std::ostringstream out;

  out << sample_rate_hz_ << " Hz, " << channel_count_ << " ch, " << format_name;

  return out.str();
}

}  // namespace lavanda
