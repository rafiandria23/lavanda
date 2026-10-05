#include "lavanda/resources/resource_store.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <utility>

namespace lavanda {
namespace {

AudioAsset MakeAsset(std::uint32_t frames = 4, std::uint32_t channels = 1,
                     std::uint32_t rate_hz = 48000) {
  AudioBuffer buffer(frames, channels);

  for (std::uint32_t f = 0; f < frames; ++f) {
    for (std::uint32_t c = 0; c < channels; ++c) {
      buffer(f, c) = static_cast<Sample>(f);
    }
  }

  return std::move(AudioAsset::Create(std::move(buffer), rate_hz).value());
}

// A 4-frame mono asset is 4 * 1 * sizeof(Sample) = 16 bytes.
constexpr std::uint64_t kSmallAssetBytes = 4 * sizeof(Sample);

ResourceConfig ConfigWith(std::size_t max_assets,
                          std::uint64_t max_total_bytes = 1u << 20) {
  ResourceConfig config;
  config.max_assets = max_assets;
  config.max_total_bytes = max_total_bytes;

  return config;
}

AudioAssetId InsertOk(ResourceStore& store, const std::string& key = "") {
  StatusOr<AudioAssetId> result = store.Insert(MakeAsset(), key);

  EXPECT_TRUE(result.ok()) << result.status().message();

  return result.ok() ? result.value() : AudioAssetId{};
}

TEST(ResourceStoreTest, InsertMakesAssetResolvable) {
  ResourceStore store(ConfigWith(4));
  StatusOr<AudioAssetId> inserted =
      store.Insert(MakeAsset(8, 2, 44100), "a.wav");

  ASSERT_TRUE(inserted.ok());

  const AudioAssetId id = inserted.value();

  EXPECT_TRUE(id.is_valid());

  const AudioAsset* asset = store.Find(id);

  ASSERT_NE(asset, nullptr);
  EXPECT_EQ(asset->frame_count(), 8u);
  EXPECT_EQ(asset->channel_count(), 2u);
  EXPECT_EQ(asset->sample_rate_hz(), 44100u);
  EXPECT_EQ(store.ResolveForAudio(id), asset);

  StatusOr<AudioAssetInfo> info = store.GetInfo(id);

  ASSERT_TRUE(info.ok());
  EXPECT_EQ(info.value().frame_count, 8u);

  EXPECT_EQ(store.resident_count(), 1u);
  EXPECT_EQ(store.total_bytes(), 8u * 2u * sizeof(Sample));
}

TEST(ResourceStoreTest, StaleIdCannotResolveReusedSlot) {
  ResourceStore store(ConfigWith(1));
  const AudioAssetId id_a = InsertOk(store);

  ASSERT_TRUE(store.Release(id_a).ok());  // unpinned, so reclaimed immediately

  const AudioAssetId id_b = InsertOk(store);

  EXPECT_EQ(id_b.index, id_a.index);  // the slot was reused
  EXPECT_NE(id_b.generation, id_a.generation);
  EXPECT_EQ(id_b.generation, id_a.generation + 1);

  EXPECT_EQ(store.Find(id_a), nullptr);
  EXPECT_EQ(store.ResolveForAudio(id_a), nullptr);
  EXPECT_FALSE(store.GetInfo(id_a).ok());
  EXPECT_FALSE(store.Release(id_a).ok());

  ASSERT_NE(store.Find(id_b), nullptr);
  EXPECT_EQ(store.ResolveForAudio(id_b), store.Find(id_b));
}

TEST(ResourceStoreTest, UnknownAndStaleIdsAreRejected) {
  ResourceStore store(ConfigWith(2));

  EXPECT_EQ(store.Find(AudioAssetId{}), nullptr);
  EXPECT_EQ(store.Pin(AudioAssetId{}), nullptr);
  EXPECT_FALSE(store.Release(AudioAssetId{}).ok());
  EXPECT_FALSE(store.Release(AudioAssetId{99, 1}).ok());

  // A free slot: right generation, but nothing published.
  EXPECT_EQ(store.ResolveForAudio(AudioAssetId{0, 1}), nullptr);
  EXPECT_EQ(store.ResolveForAudio(AudioAssetId{99, 1}), nullptr);
}

TEST(ResourceStoreTest, SlotCapacityIsEnforcedAtTheBoundary) {
  ResourceStore store(ConfigWith(2));
  const AudioAssetId first = InsertOk(store);

  InsertOk(store);

  EXPECT_EQ(store.resident_count(), 2u);

  StatusOr<AudioAssetId> third = store.Insert(MakeAsset());

  ASSERT_FALSE(third.ok());
  EXPECT_EQ(third.status().code(), ErrorCode::kResourceExhausted);

  ASSERT_TRUE(store.Release(first).ok());
  EXPECT_TRUE(store.Insert(MakeAsset()).ok());
}

TEST(ResourceStoreTest, ZeroCapacityStoreRejectsEverything) {
  ResourceStore store(ConfigWith(0));
  StatusOr<AudioAssetId> result = store.Insert(MakeAsset());

  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), ErrorCode::kResourceExhausted);
}

TEST(ResourceStoreTest, MemoryLimitIsEnforcedAtTheBoundary) {
  // Exactly at the limit is allowed...
  ResourceStore at_limit(ConfigWith(8, 2 * kSmallAssetBytes));
  const AudioAssetId first = InsertOk(at_limit);

  InsertOk(at_limit);

  EXPECT_EQ(at_limit.total_bytes(), 2 * kSmallAssetBytes);

  StatusOr<AudioAssetId> over = at_limit.Insert(MakeAsset());

  ASSERT_FALSE(over.ok());
  EXPECT_EQ(over.status().code(), ErrorCode::kResourceExhausted);

  // ...and releasing frees the budget again.
  ASSERT_TRUE(at_limit.Release(first).ok());
  EXPECT_EQ(at_limit.total_bytes(), kSmallAssetBytes);
  EXPECT_TRUE(at_limit.Insert(MakeAsset()).ok());

  // One byte under the second asset's need is rejected.
  ResourceStore under(ConfigWith(8, 2 * kSmallAssetBytes - 1));

  InsertOk(under);

  EXPECT_FALSE(under.Insert(MakeAsset()).ok());
}

TEST(ResourceStoreTest, FailedInsertLeavesNoTrace) {
  ResourceStore store(ConfigWith(1));
  const AudioAssetId kept = InsertOk(store, "a.wav");

  StatusOr<AudioAssetId> failed = store.Insert(MakeAsset(), "b.wav");

  ASSERT_FALSE(failed.ok());

  EXPECT_FALSE(store.FindByKey("b.wav").has_value());
  EXPECT_EQ(store.resident_count(), 1u);
  EXPECT_EQ(store.total_bytes(), kSmallAssetBytes);
  ASSERT_TRUE(store.FindByKey("a.wav").has_value());
  EXPECT_EQ(store.FindByKey("a.wav").value(), kept);
}

TEST(ResourceStoreTest, DuplicateKeysAreRejectedWhileResident) {
  ResourceStore store(ConfigWith(4));
  const AudioAssetId id = InsertOk(store, "a.wav");

  StatusOr<AudioAssetId> duplicate = store.Insert(MakeAsset(), "a.wav");

  ASSERT_FALSE(duplicate.ok());
  EXPECT_EQ(duplicate.status().code(), ErrorCode::kInvalidArgument);
  EXPECT_EQ(store.resident_count(), 1u);

  ASSERT_TRUE(store.Release(id).ok());
  EXPECT_FALSE(store.FindByKey("a.wav").has_value());
  EXPECT_TRUE(store.Insert(MakeAsset(), "a.wav").ok());
}

TEST(ResourceStoreTest, PinnedAssetSurvivesRelease) {
  ResourceStore store(ConfigWith(1));
  const AudioAssetId id = InsertOk(store);

  const AudioAsset* pinned = store.Pin(id);

  ASSERT_NE(pinned, nullptr);
  EXPECT_EQ(store.pin_count(id), 1u);

  // Release succeeds, but the asset must stay alive for its user.
  ASSERT_TRUE(store.Release(id).ok());
  EXPECT_EQ(store.resident_count(), 0u);
  EXPECT_EQ(store.retiring_count(), 1u);
  EXPECT_EQ(store.total_bytes(), kSmallAssetBytes);
  EXPECT_EQ(store.ResolveForAudio(id), pinned);
  EXPECT_EQ(pinned->frame_count(), 4u);  // still readable

  // New users are refused, and the slot is not reusable yet.
  EXPECT_EQ(store.Find(id), nullptr);
  EXPECT_EQ(store.Pin(id), nullptr);

  StatusOr<AudioAssetId> blocked = store.Insert(MakeAsset());

  ASSERT_FALSE(blocked.ok());
  EXPECT_EQ(blocked.status().code(), ErrorCode::kResourceExhausted);

  // Last pin released -> reclaimable.
  EXPECT_TRUE(store.Unpin(id));
  EXPECT_EQ(store.pin_count(id), 0u);
  EXPECT_EQ(store.ReclaimRetired(), 1u);
  EXPECT_EQ(store.retiring_count(), 0u);
  EXPECT_EQ(store.total_bytes(), 0u);
  EXPECT_EQ(store.ResolveForAudio(id), nullptr);
  EXPECT_TRUE(store.Insert(MakeAsset()).ok());
}

TEST(ResourceStoreTest, InsertReclaimsUnpinnedRetirees) {
  ResourceStore store(ConfigWith(1));
  const AudioAssetId a = InsertOk(store, "a.wav");

  ASSERT_NE(store.Pin(a), nullptr);
  ASSERT_TRUE(store.Release(a).ok());  // retiring, still pinned
  ASSERT_TRUE(store.Unpin(a));         // audio side finished

  // No explicit ReclaimRetired(): Insert makes room by itself.
  EXPECT_TRUE(store.Insert(MakeAsset(), "b.wav").ok());
  EXPECT_EQ(store.retiring_count(), 0u);
}

TEST(ResourceStoreTest, AssetStaysUntilEveryPinIsReleased) {
  ResourceStore store(ConfigWith(2));
  const AudioAssetId id = InsertOk(store);

  ASSERT_NE(store.Pin(id), nullptr);  // e.g. a voice
  ASSERT_NE(store.Pin(id), nullptr);  // e.g. a graph plan
  EXPECT_EQ(store.pin_count(id), 2u);
  ASSERT_TRUE(store.Release(id).ok());

  EXPECT_TRUE(store.Unpin(id));
  EXPECT_EQ(store.ReclaimRetired(), 0u);
  EXPECT_EQ(store.retiring_count(), 1u);

  EXPECT_TRUE(store.Unpin(id));
  EXPECT_EQ(store.ReclaimRetired(), 1u);
  EXPECT_EQ(store.retiring_count(), 0u);
}

TEST(ResourceStoreTest, KeyIsFreedAtReleaseEvenWhilePinned) {
  ResourceStore store(ConfigWith(2));
  const AudioAssetId old_id = InsertOk(store, "a.wav");

  ASSERT_NE(store.Pin(old_id), nullptr);
  ASSERT_TRUE(store.Release(old_id).ok());

  EXPECT_FALSE(store.FindByKey("a.wav").has_value());

  StatusOr<AudioAssetId> reloaded = store.Insert(MakeAsset(), "a.wav");

  ASSERT_TRUE(reloaded.ok());
  EXPECT_NE(reloaded.value().index, old_id.index);  // a fresh slot

  EXPECT_TRUE(store.Unpin(old_id));
  EXPECT_EQ(store.ReclaimRetired(), 1u);
  EXPECT_NE(store.Find(reloaded.value()), nullptr);
}

TEST(ResourceStoreTest, UnpinRejectsStaleIdsAndUnderflow) {
  ResourceStore store(ConfigWith(2));
  const AudioAssetId id = InsertOk(store);

  EXPECT_FALSE(store.Unpin(id));       // never pinned
  EXPECT_EQ(store.pin_count(id), 0u);  // the underflow was undone
  EXPECT_FALSE(store.Unpin(AudioAssetId{}));
  EXPECT_FALSE(store.Unpin(AudioAssetId{0, 99}));  // wrong generation

  ASSERT_NE(store.Pin(id), nullptr);
  EXPECT_TRUE(store.Unpin(id));
  EXPECT_FALSE(store.Unpin(id));  // already balanced
  EXPECT_EQ(store.pin_count(id), 0u);
}

TEST(ResourceStoreTest, DestroyingTheStoreWithPinnedAssetsIsClean) {
  ResourceStore store(ConfigWith(2));
  const AudioAssetId id = InsertOk(store, "a.wav");

  ASSERT_NE(store.Pin(id), nullptr);
  ASSERT_TRUE(store.Release(id).ok());
}

}  // namespace
}  // namespace lavanda
