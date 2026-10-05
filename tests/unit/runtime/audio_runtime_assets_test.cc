#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>

#include "lavanda/runtime/audio_runtime.h"
#include "test_support/fake_audio_device.h"

namespace lavanda {
namespace {

std::string FixturePath(const char* name) {
  return std::string(LAVANDA_FIXTURE_DIR) + "/" + name;
}

TEST(AudioRuntimeAssetsTest, LoadBeforeStartIsRejectedAndCounted) {
  AudioRuntime runtime(std::make_unique<test_support::FakeAudioDevice>());

  StatusOr<AudioAssetId> id =
      runtime.LoadAudioAsset(FixturePath("mono_pcm16.wav"));

  ASSERT_FALSE(id.ok());
  EXPECT_EQ(id.status().code(), ErrorCode::kNotOpen);
  EXPECT_EQ(runtime.stats().asset_load_failures, 1u);
  EXPECT_EQ(runtime.stats().resident_asset_count, 0u);
}

TEST(AudioRuntimeAssetsTest, LoadedAssetIsPreparedAtTheDeviceRate) {
  auto owned_device = std::make_unique<test_support::FakeAudioDevice>();
  test_support::FakeAudioDevice* device = owned_device.get();
  AudioRuntime runtime(std::move(owned_device));

  ASSERT_TRUE(runtime.Start().ok());

  StatusOr<AudioAssetId> id =
      runtime.LoadAudioAsset(FixturePath("mono_pcm16.wav"));

  ASSERT_TRUE(id.ok()) << id.status().message();

  StatusOr<AudioAssetInfo> info = runtime.GetAudioAssetInfo(id.value());

  ASSERT_TRUE(info.ok());
  EXPECT_EQ(static_cast<double>(info.value().sample_rate_hz),
            device->format().sample_rate_hz());
  EXPECT_EQ(info.value().channel_count, 1u);
  EXPECT_GT(info.value().frame_count, 0u);

  EXPECT_EQ(runtime.stats().resident_asset_count, 1u);
  EXPECT_EQ(runtime.stats().resident_asset_bytes,
            std::uint64_t{info.value().frame_count} *
                info.value().channel_count * sizeof(Sample));
  EXPECT_EQ(runtime.stats().asset_load_failures, 0u);
}

TEST(AudioRuntimeAssetsTest, LoadingTheSameFileTwiceReturnsTheSameAsset) {
  AudioRuntime runtime(std::make_unique<test_support::FakeAudioDevice>());

  ASSERT_TRUE(runtime.Start().ok());

  StatusOr<AudioAssetId> first =
      runtime.LoadAudioAsset(FixturePath("mono_pcm16.wav"));
  StatusOr<AudioAssetId> second =
      runtime.LoadAudioAsset(FixturePath("mono_pcm16.wav"));

  ASSERT_TRUE(first.ok() && second.ok());

  EXPECT_EQ(first.value(), second.value());
  EXPECT_EQ(runtime.stats().resident_asset_count, 1u);
}

TEST(AudioRuntimeAssetsTest, ReleaseInvalidatesTheId) {
  AudioRuntime runtime(std::make_unique<test_support::FakeAudioDevice>());

  ASSERT_TRUE(runtime.Start().ok());

  StatusOr<AudioAssetId> id =
      runtime.LoadAudioAsset(FixturePath("mono_pcm16.wav"));

  ASSERT_TRUE(id.ok());

  EXPECT_TRUE(runtime.ReleaseAudioAsset(id.value()).ok());
  EXPECT_FALSE(runtime.GetAudioAssetInfo(id.value()).ok());
  EXPECT_FALSE(runtime.ReleaseAudioAsset(id.value()).ok());  // already released
  EXPECT_EQ(runtime.stats().resident_asset_count, 0u);
  EXPECT_EQ(runtime.stats().resident_asset_bytes, 0u);

  // Reloading gives a fresh id; the old one stays stale.
  StatusOr<AudioAssetId> reloaded =
      runtime.LoadAudioAsset(FixturePath("mono_pcm16.wav"));

  ASSERT_TRUE(reloaded.ok());
  EXPECT_NE(reloaded.value(), id.value());
  EXPECT_FALSE(runtime.GetAudioAssetInfo(id.value()).ok());
}

TEST(AudioRuntimeAssetsTest, LoadFailuresAreCountedAndLeaveNoTrace) {
  AudioRuntime runtime(std::make_unique<test_support::FakeAudioDevice>());

  ASSERT_TRUE(runtime.Start().ok());

  EXPECT_FALSE(runtime.LoadAudioAsset(FixturePath("does_not_exist.wav")).ok());
  EXPECT_FALSE(runtime.LoadAudioAsset(FixturePath("not_a_wav.wav")).ok());

  EXPECT_EQ(runtime.stats().asset_load_failures, 2u);
  EXPECT_EQ(runtime.stats().resident_asset_count, 0u);
  EXPECT_EQ(runtime.stats().resident_asset_bytes, 0u);

  EXPECT_TRUE(runtime.LoadAudioAsset(FixturePath("mono_pcm16.wav")).ok());
  EXPECT_EQ(runtime.stats().asset_load_failures, 2u);  // success doesn't count
}

TEST(AudioRuntimeAssetsTest, LoadsAreAllowedWhileStoppedButNotAfterShutdown) {
  AudioRuntime runtime(std::make_unique<test_support::FakeAudioDevice>());

  ASSERT_TRUE(runtime.Start().ok());
  ASSERT_TRUE(runtime.Stop().ok());

  // Stop() keeps the device open, so its sample rate is still known.
  EXPECT_TRUE(runtime.LoadAudioAsset(FixturePath("mono_pcm16.wav")).ok());

  ASSERT_TRUE(runtime.Shutdown().ok());

  StatusOr<AudioAssetId> after =
      runtime.LoadAudioAsset(FixturePath("stereo_pcm16.wav"));

  ASSERT_FALSE(after.ok());
  EXPECT_EQ(after.status().code(), ErrorCode::kNotOpen);
}

TEST(AudioRuntimeAssetsTest, CapacityComesFromRuntimeConfig) {
  RuntimeConfig config;
  config.resource_config.max_assets = 1;

  AudioRuntime runtime(std::make_unique<test_support::FakeAudioDevice>(),
                       config);

  ASSERT_TRUE(runtime.Start().ok());

  ASSERT_TRUE(runtime.LoadAudioAsset(FixturePath("mono_pcm16.wav")).ok());

  StatusOr<AudioAssetId> second =
      runtime.LoadAudioAsset(FixturePath("stereo_pcm16.wav"));

  ASSERT_FALSE(second.ok());
  EXPECT_EQ(second.status().code(), ErrorCode::kResourceExhausted);
}

}  // namespace
}  // namespace lavanda
