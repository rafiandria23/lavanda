#include "lavanda/resources/audio_asset.h"

#include <utility>

namespace lavanda {

StatusOr<AudioAsset> AudioAsset::Create(AudioBuffer samples,
                                        std::uint32_t sample_rate_hz) {
  if (sample_rate_hz == 0 || sample_rate_hz > kMaxAssetSampleRateHz) {
    return Status(ErrorCode::kInvalidArgument,
                  "AudioAsset sample rate is out of range");
  }

  if (samples.channel_count() == 0) {
    return Status(ErrorCode::kInvalidArgument,
                  "AudioAsset must have at least one channel");
  }

  if (samples.channel_count() > kMaxAssetChannels) {
    return Status(ErrorCode::kUnsupportedFormat,
                  "AudioAsset supports at most 2 channels");
  }

  if (samples.frame_count() == 0) {
    return Status(ErrorCode::kInvalidArgument,
                  "AudioAsset must contain at least one frame");
  }

  return AudioAsset(std::move(samples), sample_rate_hz);
}

}  // namespace lavanda
