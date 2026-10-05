#include "lavanda/resources/resource_loader.h"

#include <filesystem>
#include <limits>
#include <optional>
#include <system_error>
#include <utility>

#include "lavanda/resources/audio_asset.h"
#include "lavanda/resources/decoder.h"
#include "lavanda/resources/resampler.h"

namespace lavanda {
namespace {

StatusOr<std::string> MakeKey(const std::string& path,
                              std::uint32_t target_rate_hz) {
  std::error_code error;
  const std::filesystem::path canonical =
      std::filesystem::weakly_canonical(std::filesystem::path(path), error);

  if (error) {
    return Status(
        ErrorCode::kInvalidArgument,
        "cannot resolve asset path '" + path + "': " + error.message());
  }

  return canonical.string() + "@" + std::to_string(target_rate_hz);
}

StatusOr<AudioAsset> Prepare(const std::string& path,
                             std::uint32_t target_rate_hz,
                             const ResourceConfig& config) {
  StatusOr<AudioAsset> decoded = DecodeAudioFile(
      path, std::numeric_limits<std::uint32_t>::max(), config.max_file_bytes);

  if (!decoded.ok()) {
    return decoded.status();
  }

  AudioAsset& native = decoded.value();

  if (native.sample_rate_hz() != target_rate_hz) {
    return ResampleAsset(native, target_rate_hz, config.max_frames_per_asset);
  }

  if (native.frame_count() > config.max_frames_per_asset) {
    return Status(ErrorCode::kResourceExhausted,
                  "asset has " + std::to_string(native.frame_count()) +
                      " frames; the maximum is " +
                      std::to_string(config.max_frames_per_asset));
  }

  return std::move(native);
}

}  // namespace

StatusOr<AudioAssetId> ResourceLoader::Load(
    const std::string& path, std::uint32_t target_sample_rate_hz) {
  if (path.empty()) {
    return Status(ErrorCode::kInvalidArgument, "asset path is empty");
  }

  if (target_sample_rate_hz == 0 ||
      target_sample_rate_hz > kMaxAssetSampleRateHz) {
    return Status(ErrorCode::kInvalidArgument,
                  "target sample rate " +
                      std::to_string(target_sample_rate_hz) +
                      " Hz is out of range");
  }

  StatusOr<std::string> key = MakeKey(path, target_sample_rate_hz);

  if (!key.ok()) {
    return key.status();
  }

  if (std::optional<AudioAssetId> existing = store_.FindByKey(key.value())) {
    return *existing;
  }

  store_.ReclaimRetired();

  if (!store_.HasFreeSlot()) {
    return Status(ErrorCode::kResourceExhausted,
                  "resource store is full; release an asset before loading '" +
                      path + "'");
  }

  StatusOr<AudioAsset> prepared = Prepare(path, target_sample_rate_hz, config_);

  if (!prepared.ok()) {
    return Status(prepared.status().code(),
                  path + ": " + prepared.status().message());
  }

  return store_.Insert(std::move(prepared.value()), key.value());
}

}  // namespace lavanda
