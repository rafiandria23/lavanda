#include "lavanda/runtime/stereo_balance.h"

#include <gtest/gtest.h>

#include "lavanda/core/audio_buffer.h"

namespace lavanda {
namespace {

// ---- definitions -----------------------------------------------------------

constexpr float kTolerance = 1e-6f;

// ---- tests -----------------------------------------------------------------

TEST(StereoBalanceTest, CenterPassesBothChannelsAtUnityTimesGain) {
  const BalanceGains g = StereoBalanceGains(0.0f, 0.5f);

  EXPECT_FLOAT_EQ(g.left, 0.5f);
  EXPECT_FLOAT_EQ(g.right, 0.5f);
}

TEST(StereoBalanceTest, HardRightSilencesOnlyTheLeftChannel) {
  const BalanceGains g = StereoBalanceGains(1.0f, 1.0f);

  EXPECT_NEAR(g.left, 0.0f, kTolerance);
  EXPECT_FLOAT_EQ(g.right, 1.0f);
}

TEST(StereoBalanceTest, HardLeftSilencesOnlyTheRightChannel) {
  const BalanceGains g = StereoBalanceGains(-1.0f, 1.0f);

  EXPECT_FLOAT_EQ(g.left, 1.0f);
  EXPECT_NEAR(g.right, 0.0f, kTolerance);
}

TEST(StereoBalanceTest, GainsAreNeverNegative) {
  EXPECT_GE(StereoBalanceGains(1.0f, 1.0f).left, 0.0f);
  EXPECT_GE(StereoBalanceGains(-1.0f, 1.0f).right, 0.0f);
}

TEST(StereoBalanceTest, OutOfRangePanIsClamped) {
  const BalanceGains beyond = StereoBalanceGains(5.0f, 1.0f);
  const BalanceGains edge = StereoBalanceGains(1.0f, 1.0f);

  EXPECT_FLOAT_EQ(beyond.left, edge.left);
  EXPECT_FLOAT_EQ(beyond.right, edge.right);
}

TEST(StereoBalanceTest, AttenuatedSideFallsMonotonicallyTowardTheEdge) {
  float previous = StereoBalanceGains(0.0f, 1.0f).left;

  for (float pan : {0.25f, 0.5f, 0.75f, 1.0f}) {
    const float current = StereoBalanceGains(pan, 1.0f).left;

    EXPECT_LT(current, previous);

    previous = current;
  }
}

TEST(StereoBalanceTest, AccumulateAddsPerChannelWithItsOwnGain) {
  AudioBuffer destination(2, 2);
  AudioBuffer source(2, 2);

  for (std::uint32_t f = 0; f < 2; ++f) {
    destination(f, 0) = 1.0f;
    destination(f, 1) = 1.0f;

    source(f, 0) = 2.0f;
    source(f, 1) = 4.0f;
  }

  AccumulateStereoWithBalance(destination.View(), source.View(),
                              BalanceGains{0.5f, 0.25f});

  for (std::uint32_t f = 0; f < 2; ++f) {
    EXPECT_FLOAT_EQ(destination(f, 0), 2.0f);
    EXPECT_FLOAT_EQ(destination(f, 1), 2.0f);
  }
}

TEST(StereoBalanceTest, AccumulateIgnoresNonStereoViews) {
  AudioBuffer destination(2, 1);
  AudioBuffer source(2, 2);

  destination(0, 0) = 1.0f;
  destination(1, 0) = 1.0f;

  AccumulateStereoWithBalance(destination.View(), source.View(),
                              BalanceGains{1.0f, 1.0f});

  EXPECT_FLOAT_EQ(destination(0, 0), 1.0f);
  EXPECT_FLOAT_EQ(destination(1, 0), 1.0f);
}

}  // namespace
}  // namespace lavanda
