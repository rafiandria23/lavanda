#include "lavanda/resources/audio_asset.h"

#include <gtest/gtest.h>

#include <type_traits>
#include <utility>

#include "lavanda/resources/audio_asset_id.h"

namespace lavanda {
namespace {

AudioBuffer MakeRamp(std::uint32_t frames, std::uint32_t channels) {
  AudioBuffer buffer(frames, channels);

  for (std::uint32_t f = 0; f < frames; ++f) {
    for (std::uint32_t c = 0; c < channels; ++c) {
      buffer(f, c) = static_cast<Sample>(f * 10 + c);
    }
  }

  return buffer;
}

TEST(AudioAssetIdTest, DefaultIsInvalid) {
  AudioAssetId id;

  EXPECT_FALSE(id.is_valid());
}

TEST(AudioAssetIdTest, EqualityComparesIndexAndGeneration) {
  AudioAssetId a{3, 1};

  EXPECT_EQ(a, (AudioAssetId{3, 1}));
  EXPECT_NE(a, (AudioAssetId{3, 2}));
  EXPECT_NE(a, (AudioAssetId{4, 1}));
  EXPECT_TRUE(a.is_valid());
}

TEST(AudioAssetTest, IsMoveOnly) {
  static_assert(!std::is_copy_constructible_v<AudioAsset>);
  static_assert(std::is_nothrow_move_constructible_v<AudioAsset>);
}

TEST(AudioAssetTest, CreateExposesMetadataAndSamples) {
  StatusOr<AudioAsset> asset = AudioAsset::Create(MakeRamp(4, 2), 48000);

  ASSERT_TRUE(asset.ok());
  EXPECT_EQ(asset.value().sample_rate_hz(), 48000u);
  EXPECT_EQ(asset.value().channel_count(), 2u);
  EXPECT_EQ(asset.value().frame_count(), 4u);
  EXPECT_DOUBLE_EQ(asset.value().duration_seconds(), 4.0 / 48000.0);

  EXPECT_FLOAT_EQ(asset.value().sample(0, 0), 0.0f);
  EXPECT_FLOAT_EQ(asset.value().sample(2, 1), 21.0f);
  EXPECT_FLOAT_EQ(asset.value().data()[6], 30.0f);
}

TEST(AudioAssetTest, InfoMatchesAccessors) {
  StatusOr<AudioAsset> asset = AudioAsset::Create(MakeRamp(480, 1), 48000);

  ASSERT_TRUE(asset.ok());

  AudioAssetInfo info = asset.value().info();

  EXPECT_EQ(info.sample_rate_hz, 48000u);
  EXPECT_EQ(info.channel_count, 1u);
  EXPECT_EQ(info.frame_count, 480u);
  EXPECT_DOUBLE_EQ(info.duration_seconds(), 0.01);
}

TEST(AudioAssetTest, CreateRejectsInvalidInput) {
  EXPECT_FALSE(AudioAsset::Create(MakeRamp(4, 1), 0).ok());
  EXPECT_FALSE(AudioAsset::Create(AudioBuffer(), 48000).ok());
  EXPECT_FALSE(AudioAsset::Create(AudioBuffer(0, 2), 48000).ok());
  EXPECT_FALSE(AudioAsset::Create(AudioBuffer(4, 0), 48000).ok());
  EXPECT_FALSE(
      AudioAsset::Create(AudioBuffer(4, kMaxAssetChannels + 1), 48000).ok());
  EXPECT_FALSE(
      AudioAsset::Create(MakeRamp(4, 1), kMaxAssetSampleRateHz + 1).ok());
}

TEST(AudioAssetTest, MovedAssetKeepsSamples) {
  StatusOr<AudioAsset> created = AudioAsset::Create(MakeRamp(3, 1), 44100);

  ASSERT_TRUE(created.ok());

  AudioAsset moved = std::move(created.value());

  EXPECT_EQ(moved.frame_count(), 3u);
  EXPECT_FLOAT_EQ(moved.sample(2, 0), 20.0f);
}

}  // namespace
}  // namespace lavanda
