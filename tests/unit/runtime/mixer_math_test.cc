#include "lavanda/runtime/mixer_math.h"

#include <gtest/gtest.h>

#include <cmath>

namespace lavanda {
namespace {

constexpr float kFloatTolerance = 1e-5f;

TEST(EqualPowerPanTest, HardLeftIsFullLeftZeroRight) {
  StereoGains gains = EqualPowerPan(-1.0f, 1.0f);

  EXPECT_NEAR(gains.left, 1.0f, kFloatTolerance);
  EXPECT_NEAR(gains.right, 0.0f, kFloatTolerance);
}

TEST(EqualPowerPanTest, CenterIsEqualOnBothChannels) {
  StereoGains gains = EqualPowerPan(0.0f, 1.0f);
  const float expected = std::sqrt(2.0f) / 2.0f;

  EXPECT_NEAR(gains.left, expected, kFloatTolerance);
  EXPECT_NEAR(gains.right, expected, kFloatTolerance);
}

TEST(EqualPowerPanTest, HardRightIsFullRightZeroLeft) {
  StereoGains gains = EqualPowerPan(1.0f, 1.0f);

  EXPECT_NEAR(gains.left, 0.0f, kFloatTolerance);
  EXPECT_NEAR(gains.right, 1.0f, kFloatTolerance);
}

TEST(EqualPowerPanTest, GainScalesBothChannelsProportionally) {
  StereoGains gains = EqualPowerPan(0.0f, 0.5f);
  const float expected = (std::sqrt(2.0f) / 2.0f) * 0.5f;

  EXPECT_NEAR(gains.left, expected, kFloatTolerance);
  EXPECT_NEAR(gains.right, expected, kFloatTolerance);
}

TEST(EqualPowerPanTest, OutOfRangePanIsClampedToHardRight) {
  StereoGains in_range = EqualPowerPan(1.0f, 1.0f);
  StereoGains out_of_range = EqualPowerPan(5.0f, 1.0f);

  EXPECT_NEAR(in_range.left, out_of_range.left, kFloatTolerance);
  EXPECT_NEAR(in_range.right, out_of_range.right, kFloatTolerance);
}

TEST(EqualPowerPanTest, OutOfRangePanIsClampedToHardLeft) {
  StereoGains in_range = EqualPowerPan(-1.0f, 1.0f);
  StereoGains out_of_range = EqualPowerPan(-5.0f, 1.0f);

  EXPECT_NEAR(in_range.left, out_of_range.left, kFloatTolerance);
  EXPECT_NEAR(in_range.right, out_of_range.right, kFloatTolerance);
}

TEST(EqualPowerPanTest, TotalPowerIsConstantAcrossPanPositions) {
  for (float pan = -1.0f; pan <= 1.0f; pan += 0.25f) {
    StereoGains gains = EqualPowerPan(pan, 1.0f);
    const float total_power =
        gains.left * gains.left + gains.right * gains.right;

    EXPECT_NEAR(total_power, 1.0f, kFloatTolerance) << "at pan = " << pan;
  }
}

TEST(AccumulateMonoToStereoTest, ScalesEachChannelByItsOwnGain) {
  AudioBuffer mono_source(4, 1);

  for (std::uint32_t f = 0; f < 4; ++f) mono_source(f, 0) = 0.5f;

  AudioBuffer destination(4, 2);
  destination.Clear();

  AccumulateMonoToStereo(destination.View(), mono_source.View(),
                         StereoGains{0.5f, 0.25f});

  for (std::uint32_t f = 0; f < 4; ++f) {
    EXPECT_FLOAT_EQ(destination(f, 0), 0.25f);   // 0.5 * 0.5
    EXPECT_FLOAT_EQ(destination(f, 1), 0.125f);  // 0.5 * 0.25
  }
}

TEST(AccumulateMonoToStereoTest, AddsToExistingContentRatherThanOverwriting) {
  AudioBuffer mono_source(2, 1);

  mono_source(0, 0) = 1.0f;
  mono_source(1, 0) = 1.0f;

  AudioBuffer destination(2, 2);

  destination(0, 0) = 0.1f;
  destination(0, 1) = 0.2f;
  destination(1, 0) = 0.1f;
  destination(1, 1) = 0.2f;

  AccumulateMonoToStereo(destination.View(), mono_source.View(),
                         StereoGains{1.0f, 1.0f});

  EXPECT_FLOAT_EQ(destination(0, 0), 1.1f);
  EXPECT_FLOAT_EQ(destination(0, 1), 1.2f);
}

TEST(AccumulateIntoTest, SumsTwoConstantSourcesMatchingSpecExample) {
  AudioBuffer voice_a(4, 2);
  AudioBuffer voice_b(4, 2);

  for (std::uint32_t f = 0; f < 4; ++f) {
    for (std::uint32_t c = 0; c < 2; ++c) {
      voice_a(f, c) = 0.25f;
      voice_b(f, c) = 0.50f;
    }
  }

  AudioBuffer mixed(4, 2);
  mixed.Clear();

  AccumulateInto(mixed.View(), voice_a.View(), 1.0f);
  AccumulateInto(mixed.View(), voice_b.View(), 1.0f);

  for (std::uint32_t f = 0; f < 4; ++f) {
    for (std::uint32_t c = 0; c < 2; ++c) {
      EXPECT_FLOAT_EQ(mixed(f, c), 0.75f);
    }
  }
}

TEST(AccumulateIntoTest, RespectsPerSourceGainMatchingSpecExample) {
  AudioBuffer voice_a(1, 1);
  voice_a(0, 0) = 0.25f;

  AudioBuffer voice_b(1, 1);
  voice_b(0, 0) = 0.50f;

  AudioBuffer mixed(1, 1);
  mixed.Clear();

  AccumulateInto(mixed.View(), voice_a.View(), 0.5f);
  AccumulateInto(mixed.View(), voice_b.View(), 1.0f);

  EXPECT_FLOAT_EQ(mixed(0, 0), 0.625f);
}

TEST(ApplyGainTest, ScalesEverySampleInPlace) {
  AudioBuffer buffer(3, 2);

  for (std::uint32_t f = 0; f < 3; ++f) {
    for (std::uint32_t c = 0; c < 2; ++c) buffer(f, c) = 0.4f;
  }

  ApplyGain(buffer.View(), 0.5f);
  for (std::uint32_t f = 0; f < 3; ++f) {
    for (std::uint32_t c = 0; c < 2; ++c) {
      EXPECT_FLOAT_EQ(buffer(f, c), 0.2f);
    }
  }
}

TEST(ApplyGainTest, ZeroGainProducesSilence) {
  AudioBuffer buffer(2, 1);

  buffer(0, 0) = 0.9f;
  buffer(1, 0) = -0.3f;

  ApplyGain(buffer.View(), 0.0f);

  EXPECT_FLOAT_EQ(buffer(0, 0), 0.0f);
  EXPECT_FLOAT_EQ(buffer(1, 0), 0.0f);
}

TEST(ApplyGainTest, GainGreaterThanOneAmplifies) {
  AudioBuffer buffer(1, 1);
  buffer(0, 0) = 0.5f;

  ApplyGain(buffer.View(), 2.0f);

  EXPECT_FLOAT_EQ(buffer(0, 0), 1.0f);
}

}  // namespace
}  // namespace lavanda
