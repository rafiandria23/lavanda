#include "lavanda/resources/decoder.h"

#include <fstream>
#include <ios>
#include <vector>

#include "lavanda/resources/decoders/wav_decoder.h"

namespace lavanda {

StatusOr<AudioAsset> DecodeAudioFile(const std::string& path,
                                     std::uint32_t max_frames,
                                     std::size_t max_file_bytes) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);

  if (!file) {
    return Status(ErrorCode::kInvalidArgument,
                  "cannot open audio file: " + path);
  }

  const std::streamoff size = file.tellg();

  if (size < 0) {
    return Status(ErrorCode::kPlatformError,
                  "cannot determine size of audio file: " + path);
  }

  if (static_cast<std::uint64_t>(size) > max_file_bytes) {
    return Status(ErrorCode::kResourceExhausted,
                  "audio file exceeds the maximum file size: " + path);
  }

  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));

  file.seekg(0, std::ios::beg);

  if (!bytes.empty() &&
      !file.read(reinterpret_cast<char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()))) {
    return Status(ErrorCode::kPlatformError,
                  "failed to read audio file: " + path);
  }

  return DecodeWav(bytes, max_frames);
}

}  // namespace lavanda
