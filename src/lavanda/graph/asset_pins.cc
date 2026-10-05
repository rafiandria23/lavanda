#include "lavanda/graph/asset_pins.h"

#include <utility>

#include "lavanda/resources/resource_store.h"

namespace lavanda {

AssetPins::~AssetPins() { ReleaseAll(); }

AssetPins::AssetPins(AssetPins&& other) noexcept
    : store_(other.store_), ids_(std::move(other.ids_)) {
  other.store_ = nullptr;
  other.ids_.clear();
}

AssetPins& AssetPins::operator=(AssetPins&& other) noexcept {
  if (this != &other) {
    ReleaseAll();

    store_ = other.store_;
    ids_ = std::move(other.ids_);

    other.store_ = nullptr;
    other.ids_.clear();
  }

  return *this;
}

const AudioAsset* AssetPins::Pin(AudioAssetId id) {
  if (store_ == nullptr) {
    return nullptr;
  }

  ids_.reserve(ids_.size() + 1);

  const AudioAsset* asset = store_->Pin(id);

  if (asset != nullptr) {
    ids_.push_back(id);
  }

  return asset;
}

void AssetPins::ReleaseAll() noexcept {
  if (store_ != nullptr) {
    for (const AudioAssetId& id : ids_) {
      store_->Unpin(id);
    }
  }

  ids_.clear();
}

}  // namespace lavanda
