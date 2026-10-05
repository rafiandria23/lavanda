#ifndef LAVANDA_RESOURCES_RESAMPLER_H_
#define LAVANDA_RESOURCES_RESAMPLER_H_

#include <cstdint>

#include "lavanda/core/status.h"
#include "lavanda/resources/audio_asset.h"

namespace lavanda {

StatusOr<AudioAsset> ResampleAsset(const AudioAsset& source,
                                   std::uint32_t target_rate_hz,
                                   std::uint32_t max_frames);

}  // namespace lavanda

#endif
