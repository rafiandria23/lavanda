#include "lavanda/resources/resource_loader.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

#include "lavanda/resources/resource_store.h"

namespace lavanda {
namespace {

constexpr std::uint32_t kRate = 48000;

std::string FixturePath(const char* name) {
  return std::string(LAVANDA_FIXTURE_DIR) + "/" + name;
}

// Owns a store and a loader wired together with one config.
struct Harness {
  explicit Harness(const ResourceConfig& config)
      : store(config), loader(store, config) {}

  StatusOr<AudioAssetId> Load(const char* fixture, std::uint32_t rate = kRate) {
    return loader.Load(FixturePath(fixture), rate);
  }

  ResourceStore store;
  ResourceLoader loader;
};

ResourceConfig ConfigWithAssets(std::size_t max_assets) {
  ResourceConfig config;
  config.max_assets = max_assets;

  return config;
}

TEST(ResourceLoaderTest, LoadsFileAndRegistersItResident) {
  Harness h{ResourceConfig{}};
  StatusOr<AudioAssetId> id = h.Load("mono_pcm16.wav");

  ASSERT_TRUE(id.ok()) << id.status().message();

  const AudioAsset* asset = h.store.Find(id.value());

  ASSERT_NE(asset, nullptr);
  EXPECT_EQ(asset->sample_rate_hz(), kRate);
  EXPECT_EQ(asset->channel_count(), 1u);
  EXPECT_EQ(asset->frame_count(), 8u);
  EXPECT_EQ(h.store.resident_count(), 1u);
}

TEST(ResourceLoaderTest, LoadingTheSameFileTwiceReturnsTheSameAsset) {
  Harness h{ResourceConfig{}};
  StatusOr<AudioAssetId> first = h.Load("mono_pcm16.wav");
  StatusOr<AudioAssetId> second = h.Load("mono_pcm16.wav");

  // A different spelling of the same path resolves to the same asset.
  StatusOr<AudioAssetId> respelled = h.Load("../audio/mono_pcm16.wav");

  ASSERT_TRUE(first.ok());
  ASSERT_TRUE(second.ok());
  ASSERT_TRUE(respelled.ok()) << respelled.status().message();

  EXPECT_EQ(first.value(), second.value());
  EXPECT_EQ(first.value(), respelled.value());
  EXPECT_EQ(h.store.resident_count(), 1u);
}

TEST(ResourceLoaderTest, DifferentTargetRatesAreDifferentAssets) {
  Harness h{ResourceConfig{}};
  StatusOr<AudioAssetId> at_48k = h.Load("mono_pcm16.wav", 48000);
  StatusOr<AudioAssetId> at_24k = h.Load("mono_pcm16.wav", 24000);

  ASSERT_TRUE(at_48k.ok());
  ASSERT_TRUE(at_24k.ok());

  EXPECT_NE(at_48k.value(), at_24k.value());
  EXPECT_EQ(h.store.Find(at_48k.value())->frame_count(), 8u);
  EXPECT_EQ(h.store.Find(at_24k.value())->frame_count(), 4u);
  EXPECT_EQ(h.store.Find(at_24k.value())->sample_rate_hz(), 24000u);
}

TEST(ResourceLoaderTest, ResamplesToTheTargetRate) {
  Harness h{ResourceConfig{}};

  // 5 frames at 22050 Hz -> exactly 10 frames at 44100 Hz.
  StatusOr<AudioAssetId> mono = h.Load("mono_pcm8.wav", 44100);

  ASSERT_TRUE(mono.ok()) << mono.status().message();
  EXPECT_EQ(h.store.Find(mono.value())->sample_rate_hz(), 44100u);
  EXPECT_EQ(h.store.Find(mono.value())->frame_count(), 10u);

  // Stereo is preserved: 4 frames at 44100 Hz -> round(4.35) = 4 at 48000 Hz.
  StatusOr<AudioAssetId> stereo = h.Load("stereo_pcm16.wav", 48000);

  ASSERT_TRUE(stereo.ok()) << stereo.status().message();
  EXPECT_EQ(h.store.Find(stereo.value())->sample_rate_hz(), 48000u);
  EXPECT_EQ(h.store.Find(stereo.value())->channel_count(), 2u);
  EXPECT_EQ(h.store.Find(stereo.value())->frame_count(), 4u);
}

TEST(ResourceLoaderTest, MatchingRateIsNotResampled) {
  Harness h{ResourceConfig{}};
  StatusOr<AudioAssetId> id = h.Load("mono_pcm16.wav", 48000);

  ASSERT_TRUE(id.ok());

  const AudioAsset* asset = h.store.Find(id.value());

  ASSERT_NE(asset, nullptr);

  // The file is a ramp decoding to frame / 8, untouched.
  EXPECT_FLOAT_EQ(asset->sample(0, 0), 0.0f);
  EXPECT_FLOAT_EQ(asset->sample(2, 0), 0.25f);
  EXPECT_FLOAT_EQ(asset->sample(7, 0), 0.875f);
}

TEST(ResourceLoaderTest, FailedLoadsLeaveNoTrace) {
  Harness h{ConfigWithAssets(1)};

  const char* bad[] = {"does_not_exist.wav",  "not_a_wav.wav",
                       "truncated_data.wav",  "six_channels.wav",
                       "nonfinite_float.wav", "zero_frames.wav"};

  for (int attempt = 0; attempt < 2; ++attempt) {  // failure, then retry
    for (const char* name : bad) {
      SCOPED_TRACE(name);
      EXPECT_FALSE(h.Load(name).ok());
    }
  }

  EXPECT_EQ(h.store.resident_count(), 0u);
  EXPECT_EQ(h.store.retiring_count(), 0u);
  EXPECT_EQ(h.store.total_bytes(), 0u);

  // The only slot is still untouched: first use, first generation.
  StatusOr<AudioAssetId> good = h.Load("mono_pcm16.wav");

  ASSERT_TRUE(good.ok()) << good.status().message();
  EXPECT_EQ(good.value().index, 0u);
  EXPECT_EQ(good.value().generation, 1u);
}

TEST(ResourceLoaderTest, InvalidArgumentsAreRejected) {
  Harness h{ResourceConfig{}};

  EXPECT_EQ(h.loader.Load("", kRate).status().code(),
            ErrorCode::kInvalidArgument);
  EXPECT_EQ(h.Load("mono_pcm16.wav", 0).status().code(),
            ErrorCode::kInvalidArgument);
  EXPECT_EQ(h.Load("mono_pcm16.wav", kMaxAssetSampleRateHz + 1).status().code(),
            ErrorCode::kInvalidArgument);
  EXPECT_EQ(h.store.resident_count(), 0u);
}

TEST(ResourceLoaderTest, FileSizeLimitIsEnforced) {
  ResourceConfig config;
  config.max_file_bytes = 16;

  Harness h{config};

  StatusOr<AudioAssetId> id = h.Load("mono_pcm16.wav");

  ASSERT_FALSE(id.ok());
  EXPECT_EQ(id.status().code(), ErrorCode::kResourceExhausted);
  EXPECT_EQ(h.store.resident_count(), 0u);
}

TEST(ResourceLoaderTest, FrameLimitAppliesToTheFinalAsset) {
  ResourceConfig config;
  config.max_frames_per_asset = 4;

  Harness h{config};

  // 8 frames at the native rate: over the limit.
  StatusOr<AudioAssetId> too_long = h.Load("mono_pcm16.wav", 48000);

  ASSERT_FALSE(too_long.ok());
  EXPECT_EQ(too_long.status().code(), ErrorCode::kResourceExhausted);

  // The same file converted to 24 kHz is 4 frames: within the limit.
  EXPECT_TRUE(h.Load("mono_pcm16.wav", 24000).ok());
}

TEST(ResourceLoaderTest, FullStoreFailsBeforeDecoding) {
  Harness h{ConfigWithAssets(1)};
  StatusOr<AudioAssetId> first = h.Load("mono_pcm16.wav");

  ASSERT_TRUE(first.ok());

  StatusOr<AudioAssetId> missing = h.Load("does_not_exist.wav");

  ASSERT_FALSE(missing.ok());
  EXPECT_EQ(missing.status().code(), ErrorCode::kResourceExhausted);

  StatusOr<AudioAssetId> other = h.Load("stereo_pcm16.wav");

  ASSERT_FALSE(other.ok());
  EXPECT_EQ(other.status().code(), ErrorCode::kResourceExhausted);

  // An asset that is already resident is still returned when the store is full.
  StatusOr<AudioAssetId> again = h.Load("mono_pcm16.wav");

  ASSERT_TRUE(again.ok());
  EXPECT_EQ(again.value(), first.value());
}

TEST(ResourceLoaderTest, MemoryBudgetFailureAfterDecodeLeavesNoTrace) {
  ResourceConfig config;
  config.max_total_bytes = 8;  // mono_pcm16.wav decodes to 32 bytes

  Harness h{config};

  StatusOr<AudioAssetId> id = h.Load("mono_pcm16.wav");

  ASSERT_FALSE(id.ok());
  EXPECT_EQ(id.status().code(), ErrorCode::kResourceExhausted);
  EXPECT_EQ(h.store.resident_count(), 0u);
  EXPECT_EQ(h.store.total_bytes(), 0u);
}

TEST(ResourceLoaderTest, ReleasedAssetCanBeReloadedAsAFreshAsset) {
  Harness h{ConfigWithAssets(2)};
  StatusOr<AudioAssetId> first = h.Load("mono_pcm16.wav");

  ASSERT_TRUE(first.ok());
  ASSERT_TRUE(h.store.Release(first.value()).ok());  // unpinned: reclaimed

  StatusOr<AudioAssetId> second = h.Load("mono_pcm16.wav");

  ASSERT_TRUE(second.ok());
  EXPECT_EQ(second.value().index, first.value().index);
  EXPECT_EQ(second.value().generation, first.value().generation + 1);
  EXPECT_EQ(h.store.Find(first.value()), nullptr);  // the old id is stale

  ASSERT_NE(h.store.Pin(second.value()), nullptr);
  ASSERT_TRUE(h.store.Release(second.value()).ok());

  StatusOr<AudioAssetId> third = h.Load("mono_pcm16.wav");

  ASSERT_TRUE(third.ok());
  EXPECT_NE(third.value().index, second.value().index);
  EXPECT_EQ(h.store.retiring_count(), 1u);
}

}  // namespace
}  // namespace lavanda
