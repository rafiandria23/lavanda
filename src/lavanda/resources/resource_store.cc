#include "lavanda/resources/resource_store.h"

#include <new>
#include <utility>

#include "lavanda/core/sample.h"

namespace lavanda {
namespace {

std::uint64_t BytesOf(const AudioAsset& asset) noexcept {
  return std::uint64_t{asset.frame_count()} * asset.channel_count() *
         sizeof(Sample);
}

}  // namespace

ResourceStore::ResourceStore(const ResourceConfig& config)
    : slots_(config.max_assets), max_total_bytes_(config.max_total_bytes) {}

const ResourceStore::Slot* ResourceStore::Lookup(
    AudioAssetId id, SlotState expected) const noexcept {
  if (id.index >= slots_.size()) {
    return nullptr;
  }

  const Slot& slot = slots_[id.index];

  if (slot.state != expected) {
    return nullptr;
  }

  if (slot.generation.load(std::memory_order_relaxed) != id.generation) {
    return nullptr;
  }

  return &slot;
}

ResourceStore::Slot* ResourceStore::Lookup(AudioAssetId id,
                                           SlotState expected) noexcept {
  return const_cast<Slot*>(std::as_const(*this).Lookup(id, expected));
}

StatusOr<AudioAssetId> ResourceStore::Insert(AudioAsset asset,
                                             std::string key) {
  ReclaimRetired();

  if (!key.empty() && key_to_index_.count(key) != 0) {
    return Status(ErrorCode::kInvalidArgument,
                  "an asset with this key is already resident: " + key);
  }

  const std::uint64_t bytes = BytesOf(asset);

  if (bytes > max_total_bytes_ || total_bytes_ > max_total_bytes_ - bytes) {
    return Status(ErrorCode::kResourceExhausted,
                  "resource memory budget exceeded (" +
                      std::to_string(max_total_bytes_) + " bytes)");
  }

  std::size_t index = slots_.size();

  for (std::size_t i = 0; i < slots_.size(); ++i) {
    if (slots_[i].state == SlotState::kFree) {
      index = i;
      break;
    }
  }

  if (index == slots_.size()) {
    return Status(
        ErrorCode::kResourceExhausted,
        "resource store is full (" + std::to_string(slots_.size()) + " slots)");
  }

  const auto slot_index = static_cast<std::uint32_t>(index);
  std::unique_ptr<AudioAsset> owned;

  try {
    owned = std::make_unique<AudioAsset>(std::move(asset));

    if (!key.empty()) {
      key_to_index_.emplace(key, slot_index);
    }
  } catch (const std::bad_alloc) {
    return Status(ErrorCode::kResourceExhausted,
                  "out of memory while registering asset");
  }

  Slot& slot = slots_[index];

  slot.asset = std::move(owned);
  slot.key = std::move(key);
  slot.bytes = bytes;
  slot.pins.store(0, std::memory_order_relaxed);
  slot.state = SlotState::kResident;
  slot.published.store(slot.asset.get(), std::memory_order_release);

  total_bytes_ += bytes;

  return AudioAssetId{slot_index,
                      slot.generation.load(std::memory_order_relaxed)};
}

const AudioAsset* ResourceStore::Find(AudioAssetId id) const noexcept {
  const Slot* slot = Lookup(id, SlotState::kResident);

  return slot != nullptr ? slot->asset.get() : nullptr;
}

StatusOr<AudioAssetInfo> ResourceStore::GetInfo(AudioAssetId id) const {
  const AudioAsset* asset = Find(id);

  if (asset == nullptr) {
    return Status(ErrorCode::kInvalidArgument,
                  "unknown, stale, or released asset id");
  }

  return asset->info();
}

std::optional<AudioAssetId> ResourceStore::FindByKey(
    const std::string& key) const {
  const auto it = key_to_index_.find(key);

  if (it == key_to_index_.end()) {
    return std::nullopt;
  }

  const Slot& slot = slots_[it->second];

  return AudioAssetId{it->second,
                      slot.generation.load(std::memory_order_relaxed)};
}

const AudioAsset* ResourceStore::Pin(AudioAssetId id) noexcept {
  Slot* slot = Lookup(id, SlotState::kResident);

  if (slot == nullptr) {
    return nullptr;
  }

  slot->pins.fetch_add(1, std::memory_order_relaxed);

  return slot->asset.get();
}

Status ResourceStore::Release(AudioAssetId id) {
  Slot* slot = Lookup(id, SlotState::kResident);

  if (slot == nullptr) {
    return Status(
        ErrorCode::kInvalidArgument,
        "cannot release: unknown, stale, or already released asset id");
  }

  slot->state = SlotState::kRetiring;

  if (!slot->key.empty()) {
    key_to_index_.erase(slot->key);
    slot->key.clear();
  }

  ReclaimRetired();

  return Status::Ok();
}

std::size_t ResourceStore::ReclaimRetired() {
  std::size_t reclaimed = 0;

  for (Slot& slot : slots_) {
    if (slot.state != SlotState::kRetiring) {
      continue;
    }

    if (slot.pins.load(std::memory_order_acquire) != 0) {
      continue;
    }

    std::uint32_t next = slot.generation.load(std::memory_order_relaxed) + 1u;

    if (next == 0) {
      next = 1;
    }

    slot.generation.store(next, std::memory_order_release);
    slot.published.store(nullptr, std::memory_order_release);
    slot.asset.reset();

    total_bytes_ -= slot.bytes;

    slot.bytes = 0;
    slot.state = SlotState::kFree;

    ++reclaimed;
  }

  return reclaimed;
}

std::size_t ResourceStore::resident_count() const noexcept {
  std::size_t count = 0;

  for (const Slot& slot : slots_) {
    if (slot.state == SlotState::kResident) {
      ++count;
    }
  }

  return count;
}

std::size_t ResourceStore::retiring_count() const noexcept {
  std::size_t count = 0;

  for (const Slot& slot : slots_) {
    if (slot.state == SlotState::kRetiring) {
      ++count;
    }
  }

  return count;
}

std::uint32_t ResourceStore::pin_count(AudioAssetId id) const noexcept {
  if (id.index >= slots_.size()) {
    return 0;
  }

  const Slot& slot = slots_[id.index];

  if (slot.generation.load(std::memory_order_acquire) != id.generation) {
    return 0;
  }

  return slot.pins.load(std::memory_order_acquire);
}

const AudioAsset* ResourceStore::ResolveForAudio(
    AudioAssetId id) const noexcept {
  if (id.index >= slots_.size()) {
    return nullptr;
  }

  const Slot& slot = slots_[id.index];

  if (slot.generation.load(std::memory_order_acquire) != id.generation) {
    return nullptr;
  }

  return slot.published.load(std::memory_order_acquire);
}

bool ResourceStore::Unpin(AudioAssetId id) noexcept {
  if (id.index >= slots_.size()) {
    return false;
  }

  Slot& slot = slots_[id.index];

  if (slot.generation.load(std::memory_order_acquire) != id.generation) {
    return false;
  }

  const std::uint32_t previous =
      slot.pins.fetch_sub(1, std::memory_order_release);

  if (previous == 0) {
    slot.pins.fetch_add(1, std::memory_order_relaxed);
    return false;
  }

  return true;
}

}  // namespace lavanda
