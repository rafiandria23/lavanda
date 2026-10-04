#ifndef LAVANDA_RESOURCES_AUDIO_ASSET_H_
#define LAVANDA_RESOURCES_AUDIO_ASSET_H_

#include <cstdint>

#include "lavanda/core/audio_buffer.h"
#include "lavanda/core/sample.h"
#include "lavanda/core/status.h"
#include "lavanda/resources/audio_asset_info.h"

namespace lavanda {

inline constexpr std::uint32_t kMaxAssetChannels = 2;

class AudioAsset {
 public:
  static StatusOr<AudioAsset> Create(AudioBuffer samples,
                                     std::uint32_t sample_rate_hz);

  AudioAsset(AudioAsset&&) noexcept = default;
  AudioAsset& operator=(AudioAsset&&) noexcept = default;
  AudioAsset(const AudioAsset&) = delete;
  AudioAsset& operator=(const AudioAsset&) = delete;

  std::uint32_t sample_rate_hz() const noexcept { return sample_rate_hz_; }
  std::uint32_t channel_count() const noexcept {
    return samples_.channel_count();
  }
  std::uint32_t frame_count() const noexcept { return samples_.frame_count(); }
  double duration_seconds() const noexcept { return info().duration_seconds(); }

  AudioAssetInfo info() const noexcept {
    return {sample_rate_hz_, channel_count(), frame_count()};
  }

  Sample sample(std::uint32_t frame, std::uint32_t channel) const noexcept {
    return samples_(frame, channel);
  }

  const Sample* data() const noexcept { return samples_.data(); }

 private:
  AudioAsset(AudioBuffer samples, std::uint32_t sample_rate_hz) noexcept
      : samples_(std::move(samples)), sample_rate_hz_(sample_rate_hz) {}

  AudioBuffer samples_;
  std::uint32_t sample_rate_hz_ = 0;
};

}  // namespace lavanda

#endif
