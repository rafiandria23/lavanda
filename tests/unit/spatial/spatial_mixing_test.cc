#include <gtest/gtest.h>

#include <cstdint>

#include "lavanda/core/audio_buffer.h"
#include "lavanda/runtime/gain_ramp.h"
#include "lavanda/runtime/mixer_math.h"

namespace lavanda {
namespace {

constexpr std::uint32_t kFrames = 6;
constexpr float kTolerance = 1.0e-6f;

void Fill(AudioBuffer& buffer, float value) {
  AudioBufferView view = buffer.View();

  for (std::uint32_t frame = 0; frame < view.frame_count(); ++frame) {
    for (std::uint32_t channel = 0; channel < view.channel_count(); ++channel) {
      view(frame, channel) = value;
    }
  }
}

TEST(SpatialMixingTest, ConstantRampsMatchTheStaticAccumulator) {
  AudioBuffer source(kFrames, 1);
  AudioBuffer spatial(kFrames, 2);
  AudioBuffer reference(kFrames, 2);

  Fill(source, 0.5f);
  Fill(spatial, 0.0f);
  Fill(reference, 0.0f);

  AccumulateMonoToStereoSpatial(spatial.View(), source.View(), GainRamp(0.8f),
                                GainRamp(0.2f), GainRamp(0.5f), 0);
  AccumulateMonoToStereo(reference.View(), source.View(),
                         StereoGains{0.8f * 0.5f, 0.2f * 0.5f});

  for (std::uint32_t frame = 0; frame < kFrames; ++frame) {
    EXPECT_FLOAT_EQ(spatial(frame, 0), reference(frame, 0));
    EXPECT_FLOAT_EQ(spatial(frame, 1), reference(frame, 1));
  }
}

TEST(SpatialMixingTest, AccumulatesOntoExistingContent) {
  AudioBuffer source(kFrames, 1);
  AudioBuffer destination(kFrames, 2);

  Fill(source, 1.0f);
  Fill(destination, 0.25f);

  AccumulateMonoToStereoSpatial(destination.View(), source.View(),
                                GainRamp(0.5f), GainRamp(0.25f), GainRamp(1.0f),
                                0);

  for (std::uint32_t frame = 0; frame < kFrames; ++frame) {
    EXPECT_NEAR(destination(frame, 0), 0.75f, kTolerance);
    EXPECT_NEAR(destination(frame, 1), 0.5f, kTolerance);
  }
}

TEST(SpatialMixingTest, LeftRampGlidesLinearlyWhileRightStaysSilent) {
  AudioBuffer source(kFrames, 1);
  AudioBuffer destination(kFrames, 2);

  Fill(source, 1.0f);
  Fill(destination, 0.0f);

  GainRamp left(0.0f);
  left.Start(0, 1.0f, 4);

  AccumulateMonoToStereoSpatial(destination.View(), source.View(), left,
                                GainRamp(0.0f), GainRamp(1.0f), 0);

  const float expected[kFrames] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f, 1.0f};

  for (std::uint32_t frame = 0; frame < kFrames; ++frame) {
    EXPECT_NEAR(destination(frame, 0), expected[frame], kTolerance);
    EXPECT_EQ(destination(frame, 1), 0.0f);
  }
}

TEST(SpatialMixingTest, VoiceGainMultipliesTheSpatialGains) {
  AudioBuffer source(kFrames, 1);
  AudioBuffer destination(kFrames, 2);

  Fill(source, 1.0f);
  Fill(destination, 0.0f);

  GainRamp voice(0.0f);
  voice.Start(0, 1.0f, 4);

  AccumulateMonoToStereoSpatial(destination.View(), source.View(),
                                GainRamp(0.5f), GainRamp(0.5f), voice, 0);

  const float expected[kFrames] = {0.0f, 0.125f, 0.25f, 0.375f, 0.5f, 0.5f};

  for (std::uint32_t frame = 0; frame < kFrames; ++frame) {
    EXPECT_NEAR(destination(frame, 0), expected[frame], kTolerance);
    EXPECT_NEAR(destination(frame, 1), expected[frame], kTolerance);
  }
}

TEST(SpatialMixingTest, RampsFollowAbsoluteFrames) {
  AudioBuffer source(kFrames, 1);
  AudioBuffer destination(kFrames, 2);

  Fill(source, 1.0f);
  Fill(destination, 0.0f);

  GainRamp left(0.0f);
  left.Start(10, 1.0f, 4);

  AccumulateMonoToStereoSpatial(destination.View(), source.View(), left,
                                GainRamp(0.0f), GainRamp(1.0f), 8);

  const float expected[kFrames] = {0.0f, 0.0f, 0.0f, 0.25f, 0.5f, 0.75f};

  for (std::uint32_t frame = 0; frame < kFrames; ++frame) {
    EXPECT_NEAR(destination(frame, 0), expected[frame], kTolerance);
  }
}

TEST(SpatialMixingTest, HardSwingDipsAboutThreeDecibelsAtTheMidpoint) {
  AudioBuffer source(1, 1);
  AudioBuffer destination(1, 2);

  Fill(source, 1.0f);
  Fill(destination, 0.0f);

  GainRamp left(1.0f);
  left.Start(0, 0.0f, 100);

  GainRamp right(0.0f);
  right.Start(0, 1.0f, 100);

  AccumulateMonoToStereoSpatial(destination.View(), source.View(), left, right,
                                GainRamp(1.0f), 50);

  EXPECT_NEAR(destination(0, 0), 0.5f, kTolerance);
  EXPECT_NEAR(destination(0, 1), 0.5f, kTolerance);

  const float power = destination(0, 0) * destination(0, 0) +
                      destination(0, 1) * destination(0, 1);

  EXPECT_NEAR(power, 0.5f, kTolerance);
}

}  // namespace
}  // namespace lavanda
