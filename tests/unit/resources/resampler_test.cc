#include "lavanda/resources/resampler.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <utility>
#include <vector>

namespace lavanda {
namespace {

constexpr std::uint32_t kMaxFrames = 1u << 20;

// frames[frame][channel]
AudioAsset MakeAsset(std::uint32_t rate_hz,
                     const std::vector<std::vector<float>>& frames) {
  const auto channels = static_cast<std::uint32_t>(frames[0].size());
  AudioBuffer buffer(static_cast<std::uint32_t>(frames.size()), channels);

  for (std::uint32_t f = 0; f < buffer.frame_count(); ++f) {
    for (std::uint32_t c = 0; c < channels; ++c) {
      buffer(f, c) = frames[f][c];
    }
  }

  return std::move(AudioAsset::Create(std::move(buffer), rate_hz).value());
}

AudioAsset MakeMono(std::uint32_t rate_hz, const std::vector<float>& samples) {
  std::vector<std::vector<float>> frames;

  for (float s : samples) {
    frames.push_back({s});
  }

  return MakeAsset(rate_hz, frames);
}

void ExpectMono(const AudioAsset& asset, const std::vector<float>& expected) {
  ASSERT_EQ(asset.channel_count(), 1u);
  ASSERT_EQ(asset.frame_count(), expected.size());

  for (std::uint32_t f = 0; f < asset.frame_count(); ++f) {
    EXPECT_FLOAT_EQ(asset.sample(f, 0), expected[f]) << "frame " << f;
  }
}

TEST(ResamplerTest, SameRateIsBitExactIdentity) {
  AudioAsset source = MakeMono(44100, {0.1f, 0.2f, 0.3f, -0.4f});
  StatusOr<AudioAsset> result = ResampleAsset(source, 44100, kMaxFrames);

  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result.value().sample_rate_hz(), 44100u);
  ASSERT_EQ(result.value().frame_count(), 4u);

  for (std::uint32_t f = 0; f < 4; ++f) {
    EXPECT_EQ(result.value().sample(f, 0), source.sample(f, 0));
  }
}

TEST(ResamplerTest, UpsampleByTwoInterpolatesAndHoldsTheLastSample) {
  AudioAsset source = MakeMono(24000, {0.0f, 1.0f, 2.0f, 3.0f});
  StatusOr<AudioAsset> result = ResampleAsset(source, 48000, kMaxFrames);

  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result.value().sample_rate_hz(), 48000u);

  ExpectMono(result.value(), {0.0f, 0.5f, 1.0f, 1.5f, 2.0f, 2.5f, 3.0f, 3.0f});
}

TEST(ResamplerTest, DownsampleByTwoPicksEverySecondFrame) {
  AudioAsset source =
      MakeMono(48000, {0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f});
  StatusOr<AudioAsset> result = ResampleAsset(source, 24000, kMaxFrames);

  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result.value().sample_rate_hz(), 24000u);

  ExpectMono(result.value(), {0.0f, 2.0f, 4.0f, 6.0f});
}

TEST(ResamplerTest, ChannelsAreResampledIndependently) {
  AudioAsset source =
      MakeAsset(24000, {{0.0f, 1.0f}, {1.0f, 1.0f}, {2.0f, 1.0f}});
  StatusOr<AudioAsset> result = ResampleAsset(source, 48000, kMaxFrames);

  ASSERT_TRUE(result.ok());
  ASSERT_EQ(result.value().channel_count(), 2u);
  ASSERT_EQ(result.value().frame_count(), 6u);

  const float left[] = {0.0f, 0.5f, 1.0f, 1.5f, 2.0f, 2.0f};

  for (std::uint32_t f = 0; f < 6; ++f) {
    EXPECT_FLOAT_EQ(result.value().sample(f, 0), left[f]) << "frame " << f;
    EXPECT_FLOAT_EQ(result.value().sample(f, 1), 1.0f) << "frame " << f;
  }
}

TEST(ResamplerTest, ConstantSignalStaysConstantAtNonIntegerRatio) {
  AudioAsset source = MakeMono(44100, std::vector<float>(441, 0.5f));
  StatusOr<AudioAsset> result = ResampleAsset(source, 48000, kMaxFrames);

  ASSERT_TRUE(result.ok());
  ASSERT_EQ(result.value().frame_count(), 480u);

  for (std::uint32_t f = 0; f < 480; ++f) {
    EXPECT_FLOAT_EQ(result.value().sample(f, 0), 0.5f) << "frame " << f;
  }
}

TEST(ResamplerTest, LinearRampStaysOnTheLineAtNonIntegerRatio) {
  std::vector<float> ramp;

  for (int n = 0; n < 441; ++n) {
    ramp.push_back(static_cast<float>(n) * 0.01f);
  }

  AudioAsset source = MakeMono(44100, ramp);
  StatusOr<AudioAsset> result = ResampleAsset(source, 48000, kMaxFrames);

  ASSERT_TRUE(result.ok());

  for (std::uint32_t i = 0; i < result.value().frame_count(); ++i) {
    const double position = static_cast<double>(i) * 44100.0 / 48000.0;

    if (position >= 440.0) {
      break;  // the tail holds the last sample instead
    }

    EXPECT_NEAR(result.value().sample(i, 0), position * 0.01, 1e-5)
        << "frame " << i;
  }
}

TEST(ResamplerTest, OutputLengthIsRoundedToNearestFrame) {
  // 100 * 48000 / 44100 = 108.84 -> 109
  AudioAsset source = MakeMono(44100, std::vector<float>(100, 0.25f));
  StatusOr<AudioAsset> result = ResampleAsset(source, 48000, kMaxFrames);

  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result.value().frame_count(), 109u);
}

TEST(ResamplerTest, RejectsOutOfRangeTargetRates) {
  AudioAsset source = MakeMono(48000, {0.0f, 1.0f});

  EXPECT_EQ(ResampleAsset(source, 0, kMaxFrames).status().code(),
            ErrorCode::kInvalidArgument);
  EXPECT_EQ(ResampleAsset(source, kMaxAssetSampleRateHz + 1, kMaxFrames)
                .status()
                .code(),
            ErrorCode::kInvalidArgument);
}

TEST(ResamplerTest, EnforcesTheFrameLimitBeforeAllocating) {
  AudioAsset source = MakeMono(24000, {0.0f, 1.0f, 2.0f, 3.0f});  // -> 8 frames
  StatusOr<AudioAsset> too_small = ResampleAsset(source, 48000, 7);

  ASSERT_FALSE(too_small.ok());
  EXPECT_EQ(too_small.status().code(), ErrorCode::kResourceExhausted);

  EXPECT_TRUE(ResampleAsset(source, 48000, 8).ok());
}

}  // namespace
}  // namespace lavanda
