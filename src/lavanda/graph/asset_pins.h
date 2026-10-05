#ifndef LAVANDA_GRAPH_ASSET_PINS_H_
#define LAVANDA_GRAPH_ASSET_PINS_H_

#include <cstddef>
#include <vector>

#include "lavanda/resources/audio_asset_id.h"

namespace lavanda {

class AudioAsset;
class ResourceStore;

class AssetPins {
 public:
  AssetPins() noexcept = default;
  explicit AssetPins(ResourceStore* store) noexcept : store_(store) {}
  ~AssetPins();

  AssetPins(const AssetPins&) = delete;
  AssetPins& operator=(const AssetPins&) = delete;
  AssetPins(AssetPins&& other) noexcept;
  AssetPins& operator=(AssetPins&& other) noexcept;

  const AudioAsset* Pin(AudioAssetId id);

  std::size_t count() const noexcept { return ids_.size(); }

 private:
  void ReleaseAll() noexcept;

  ResourceStore* store_ = nullptr;
  std::vector<AudioAssetId> ids_;
};

}  // namespace lavanda

#endif
