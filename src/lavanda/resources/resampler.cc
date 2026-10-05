#include "lavanda/resources/resampler.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

#include "lavanda/core/audio_buffer.h"
#include "lavanda/core/sample.h"

namespace lavanda {

StatusOr<AudioAsset> ResampleAsset(const AudioAsset& source,
                                   std::uint32_t target_rate_hz,
                                   std::uint32_t max_frames) {
  if (target_rate_hz == 0 || target_rate_hz > kMaxAssetSampleRateHz) {
    return Status(ErrorCode::kInvalidArgument,
                  "resample target rate " + std::to_string(target_rate_hz) +
                      " Hz is out of range");
  }

  const double source_rate = source.sample_rate_hz();
  const double target_rate = target_rate_hz;
  const std::uint32_t in_frames = source.frame_count();
  const std::uint32_t channels = source.channel_count();

  const double scaled =
      static_cast<double>(in_frames) * target_rate / source_rate;
  const std::uint64_t out_frames_64 = std::max<std::uint64_t>(
      1, static_cast<std::uint64_t>(std::llround(scaled)));

  if (out_frames_64 > max_frames) {
    return Status(ErrorCode::kResourceExhausted,
                  "resampled asset would have " +
                      std::to_string(out_frames_64) +
                      " frames; the maximum is " + std::to_string(max_frames));
  }

  const auto out_frames = static_cast<std::uint32_t>(out_frames_64);
  const std::uint32_t last = in_frames - 1;

  AudioBuffer out(out_frames, channels);

  for (std::uint32_t i = 0; i < out_frames; ++i) {
    const double position = static_cast<double>(i) * source_rate / target_rate;
    std::uint32_t index = static_cast<std::uint32_t>(position);

    if (index > last) {
      index = last;
    }

    const std::uint32_t next = std::min(index + 1, last);
    const auto fraction =
        static_cast<float>(position - static_cast<double>(index));

    for (std::uint32_t channel = 0; channel < channels; ++channel) {
      const Sample a = source.sample(index, channel);
      const Sample b = source.sample(next, channel);

      out(i, channel) = a + (b - a) * fraction;
    }
  }

  return AudioAsset::Create(std::move(out), target_rate_hz);
}

}  // namespace lavanda
