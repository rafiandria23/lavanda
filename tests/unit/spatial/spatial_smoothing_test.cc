#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

#include "lavanda/spatial/spatial_config.h"
#include "lavanda/spatial/spatial_math.h"

namespace lavanda {
namespace {

TEST(SpatialSmoothingTest, TenMillisecondsAt48kIs480Frames) {
  EXPECT_EQ(SmoothingFrames(SpatialConfig{}, 48000.0), 480u);
}

TEST(SpatialSmoothingTest, ScalesWithSampleRate) {
  EXPECT_EQ(SmoothingFrames(SpatialConfig{}, 44100.0), 441u);
  EXPECT_EQ(SmoothingFrames(SpatialConfig{}, 96000.0), 960u);
}

TEST(SpatialSmoothingTest, ZeroSmoothingIsAStep) {
  SpatialConfig config;
  config.smoothing_seconds = 0.0f;

  EXPECT_EQ(SmoothingFrames(config, 48000.0), 0u);
}

TEST(SpatialSmoothingTest, RoundsToTheNearestFrame) {
  SpatialConfig below;
  below.smoothing_seconds = 0.00001f;

  EXPECT_EQ(SmoothingFrames(below, 44100.0), 0u);

  SpatialConfig above;
  above.smoothing_seconds = 0.00002f;

  EXPECT_EQ(SmoothingFrames(above, 44100.0), 1u);
}

TEST(SpatialSmoothingTest, MaximumSmoothingFitsTheCounter) {
  SpatialConfig config;
  config.smoothing_seconds = kMaxSmoothingSeconds;

  EXPECT_EQ(SmoothingFrames(config, 384000.0), 384000u);
}

TEST(SpatialSmoothingTest, UnusableSampleRatesGiveZero) {
  const SpatialConfig config;

  EXPECT_EQ(SmoothingFrames(config, 0.0), 0u);
  EXPECT_EQ(SmoothingFrames(config, -48000.0), 0u);
  EXPECT_EQ(SmoothingFrames(config, std::numeric_limits<double>::quiet_NaN()),
            0u);
  EXPECT_EQ(SmoothingFrames(config, std::numeric_limits<double>::infinity()),
            0u);
}

}  // namespace
}  // namespace lavanda
