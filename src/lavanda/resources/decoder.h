#ifndef LAVANDA_RESOURCES_DECODER_H_
#define LAVANDA_RESOURCES_DECODER_H_

#include <cstddef>
#include <cstdint>
#include <string>

#include "lavanda/core/status.h"
#include "lavanda/resources/audio_asset.h"

namespace lavanda {

StatusOr<AudioAsset> DecodeAudioFile(const std::string& path,
                                     std::uint32_t max_frames,
                                     std::size_t max_file_bytes);

}  // namespace lavanda

#endif
