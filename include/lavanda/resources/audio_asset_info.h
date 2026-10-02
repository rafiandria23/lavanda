#ifndef LAVANDA_RESOURCES_AUDIO_ASSET_INFO_H_
#define LAVANDA_RESOURCES_AUDIO_ASSET_INFO_H_

#include <cstdint>

namespace lavanda {

struct AudioAssetInfo {
  std::uint32_t sample_rate_hz = 0;
  std::uint32_t channel_count = 0;
  std::uint32_t frame_count = 0;

  double duration_seconds() const noexcept {
    return sample_rate_hz == 0 ? 0.0
                               : static_cast<double>(frame_count) /
                                     static_cast<double>(sample_rate_hz);
  }
};

}  // namespace lavanda

#endif
