#ifndef LAVANDA_RESOURCES_DECODERS_WAV_DECODER_H_
#define LAVANDA_RESOURCES_DECODERS_WAV_DECODER_H_

#include <cstdint>
#include <span>

#include "lavanda/core/status.h"
#include "lavanda/resources/audio_asset.h"

namespace lavanda {

StatusOr<AudioAsset> DecodeWav(std::span<const std::uint8_t> bytes,
                               std::uint32_t max_frames);

}  // namespace lavanda

#endif
