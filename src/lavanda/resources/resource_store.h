#ifndef LAVANDA_RESOURCES_RESOURCE_STORE_H_
#define LAVANDA_RESOURCES_RESOURCE_STORE_H_

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "lavanda/core/status.h"
#include "lavanda/resources/audio_asset.h"
#include "lavanda/resources/audio_asset_id.h"
#include "lavanda/resources/audio_asset_info.h"
#include "lavanda/resources/resource_config.h"

namespace lavanda {

class ResourceStore {
 public:
  explicit ResourceStore(const ResourceConfig& config);

  ~ResourceStore() = default;

  ResourceStore(const ResourceStore&) = delete;
  ResourceStore& operator=(const ResourceStore&) = delete;

  // ---- control thread only ------------------------------------------------

  StatusOr<AudioAssetId> Insert(AudioAsset asset, std::string key = {});

  const AudioAsset* Find(AudioAssetId id) const noexcept;
  StatusOr<AudioAssetInfo> GetInfo(AudioAssetId id) const;

  std::optional<AudioAssetId> FindByKey(const std::string& key) const;

  const AudioAsset* Pin(AudioAssetId id) noexcept;

  Status Release(AudioAssetId id);

  std::size_t ReclaimRetired();

  // Diagnostics.
  std::size_t capacity() const noexcept { return slots_.size(); }
  std::size_t resident_count() const noexcept;
  std::size_t retiring_count() const noexcept;
  std::uint64_t total_bytes() const noexcept { return total_bytes_; }
  std::uint32_t pin_count(AudioAssetId id) const noexcept;

  // ---- audio thread (and control thread) ----------------------------------

  const AudioAsset* ResolveForAudio(AudioAssetId id) const noexcept;

  bool Unpin(AudioAssetId id) noexcept;

 private:
  enum class SlotState : std::uint8_t { kFree, kResident, kRetiring };

  struct Slot {
    // Control thread only.
    SlotState state = SlotState::kFree;
    std::unique_ptr<AudioAsset> asset;
    std::string key;
    std::uint64_t bytes = 0;

    // Shared with the audio thread.
    std::atomic<std::uint32_t> generation{1};
    std::atomic<std::uint32_t> pins{0};
    std::atomic<const AudioAsset*> published{nullptr};
  };

  const Slot* Lookup(AudioAssetId id, SlotState expected) const noexcept;
  Slot* Lookup(AudioAssetId id, SlotState expected) noexcept;

  std::vector<Slot> slots_;
  std::unordered_map<std::string, std::uint32_t> key_to_index_;
  std::uint64_t max_total_bytes_;
  std::uint64_t total_bytes_ = 0;
};

}  // namespace lavanda

#endif
