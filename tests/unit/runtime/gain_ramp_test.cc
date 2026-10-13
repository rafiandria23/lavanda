#include "lavanda/runtime/gain_ramp.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

#include "lavanda/core/audio_buffer.h"
#include "lavanda/runtime/mixer_math.h"
#include "lavanda/runtime/stereo_balance.h"

namespace lavanda {
namespace {

// ---- definitions -----------------------------------------------------------

constexpr std::uint64_t kMaxFrame = std::numeric_limits<std::uint64_t>::max();
constexpr std::uint32_t kFrames = 16;

void Fill(AudioBufferView view) {
  for (std::uint32_t f = 0; f < view.frame_count(); ++f) {
    for (std::uint32_t c = 0; c < view.channel_count(); ++c) {
      view(f, c) =
          0.1f * static_cast<float>(f + 1) - 0.07f * static_cast<float>(c);
    }
  }
}

void ExpectBitIdentical(AudioBufferView a, AudioBufferView b) {
  ASSERT_EQ(a.frame_count(), b.frame_count());
  ASSERT_EQ(a.channel_count(), b.channel_count());

  for (std::uint32_t f = 0; f < a.frame_count(); ++f) {
    for (std::uint32_t c = 0; c < a.channel_count(); ++c) {
      ASSERT_EQ(a(f, c), b(f, c)) << "frame " << f << " channel " << c;
    }
  }
}

// ---- GainRamp --------------------------------------------------------------

TEST(GainRampTest, DefaultsToAConstantUnityGain) {
  const GainRamp ramp;

  EXPECT_EQ(ramp.target(), 1.0f);
  EXPECT_EQ(ramp.ValueAt(0), 1.0f);
  EXPECT_EQ(ramp.ValueAt(kMaxFrame), 1.0f);
  EXPECT_TRUE(ramp.IsConstantFrom(0));
}

TEST(GainRampTest, SetIsAConstantGain) {
  GainRamp ramp;
  ramp.Set(0.25f);

  EXPECT_EQ(ramp.target(), 0.25f);
  EXPECT_EQ(ramp.ValueAt(0), 0.25f);
  EXPECT_EQ(ramp.ValueAt(12345), 0.25f);
  EXPECT_TRUE(ramp.IsConstantFrom(0));
}

TEST(GainRampTest, EndpointsAreExact) {
  GainRamp ramp;
  ramp.Start(100, 0.0f, 50);

  EXPECT_EQ(ramp.ValueAt(0), 1.0f);
  EXPECT_EQ(ramp.ValueAt(99), 1.0f);
  EXPECT_EQ(ramp.ValueAt(100), 1.0f) << "the start frame has the start value";
  EXPECT_EQ(ramp.ValueAt(125), 0.5f);
  EXPECT_EQ(ramp.ValueAt(150), 0.0f) << "start + duration has the target";
  EXPECT_EQ(ramp.ValueAt(100000), 0.0f);
}

TEST(GainRampTest, InterpolatesLinearly) {
  GainRamp ramp;
  ramp.Set(0.0f);
  ramp.Start(0, 1.0f, 8);

  for (std::uint64_t f = 0; f <= 8; ++f) {
    EXPECT_EQ(ramp.ValueAt(f), static_cast<float>(f) / 8.0f) << f;
  }
}

TEST(GainRampTest, ZeroDurationIsAStep) {
  GainRamp ramp;
  ramp.Start(10, 0.5f, 0);

  EXPECT_EQ(ramp.ValueAt(0), 0.5f);
  EXPECT_EQ(ramp.ValueAt(10), 0.5f);
  EXPECT_TRUE(ramp.IsConstantFrom(0));
}

TEST(GainRampTest, IsConstantFromMarksTheEndOfTheRamp) {
  GainRamp ramp;
  ramp.Start(100, 0.0f, 50);

  EXPECT_FALSE(ramp.IsConstantFrom(0));
  EXPECT_FALSE(ramp.IsConstantFrom(149));
  EXPECT_TRUE(ramp.IsConstantFrom(150));
  EXPECT_TRUE(ramp.IsConstantFrom(151));
}

TEST(GainRampTest, ReplacementRampStartsFromTheExactCurrentValue) {
  GainRamp ramp;
  ramp.Start(0, 0.0f, 100);

  const float current = ramp.ValueAt(37);

  ramp.Start(37, 1.0f, 10);

  EXPECT_EQ(ramp.ValueAt(37), current);
  EXPECT_EQ(ramp.ValueAt(47), 1.0f);
  EXPECT_GT(ramp.ValueAt(42), current);
  EXPECT_LT(ramp.ValueAt(42), 1.0f);
}

TEST(GainRampTest, SetCancelsARunningRamp) {
  GainRamp ramp;
  ramp.Start(0, 0.0f, 100);
  ramp.Set(0.7f);

  EXPECT_EQ(ramp.ValueAt(30), 0.7f);
  EXPECT_EQ(ramp.ValueAt(200), 0.7f);
  EXPECT_TRUE(ramp.IsConstantFrom(0));
}

TEST(GainRampTest, ValueDependsOnlyOnTheFrameNotOnQueryOrder) {
  GainRamp ramp;
  ramp.Start(10, 0.25f, 40);

  const GainRamp copy = ramp;
  const float forward = ramp.ValueAt(30);

  static_cast<void>(copy.ValueAt(45));
  static_cast<void>(copy.ValueAt(5));

  EXPECT_EQ(copy.ValueAt(30), forward);
  EXPECT_EQ(ramp.ValueAt(30), forward);
}

TEST(GainRampTest, EndFrameSaturatesInsteadOfWrapping) {
  GainRamp ramp;
  ramp.Start(kMaxFrame - 5, 0.0f, 100);

  EXPECT_EQ(ramp.ValueAt(kMaxFrame - 5), 1.0f);
  EXPECT_GT(ramp.ValueAt(kMaxFrame - 1), 0.0f);
  EXPECT_LT(ramp.ValueAt(kMaxFrame - 1), 1.0f);
  EXPECT_EQ(ramp.ValueAt(kMaxFrame), 0.0f);

  GainRamp at_the_very_end;
  at_the_very_end.Start(kMaxFrame, 0.5f, 10);

  EXPECT_EQ(at_the_very_end.ValueAt(kMaxFrame), 0.5f);
}

// ---- ramped mixers ----------------------------------------------------------

TEST(RampedMixerTest, ConstantRampIsBitIdenticalToTheConstantMixers) {
  const GainRamp constant(0.4f);

  {
    AudioBuffer source(kFrames, 1);
    AudioBuffer plain(kFrames, 2);
    AudioBuffer ramped(kFrames, 2);

    Fill(source.View());

    AccumulateMonoToStereo(plain.View(), source.View(),
                           EqualPowerPan(0.3f, 0.4f));
    AccumulateMonoToStereoRamped(ramped.View(), source.View(),
                                 EqualPowerPan(0.3f, 1.0f), constant, 7);

    ExpectBitIdentical(plain.View(), ramped.View());
  }

  {
    AudioBuffer source(kFrames, 2);
    AudioBuffer plain(kFrames, 2);
    AudioBuffer ramped(kFrames, 2);

    Fill(source.View());

    AccumulateStereoWithBalance(plain.View(), source.View(),
                                StereoBalanceGains(0.3f, 0.4f));
    AccumulateStereoWithBalanceRamped(ramped.View(), source.View(),
                                      StereoBalanceGains(0.3f, 1.0f), constant,
                                      7);

    ExpectBitIdentical(plain.View(), ramped.View());
  }

  {
    AudioBuffer source(kFrames, 2);
    AudioBuffer plain(kFrames, 2);
    AudioBuffer ramped(kFrames, 2);

    Fill(source.View());

    AccumulateInto(plain.View(), source.View(), 0.4f);
    AccumulateIntoRamped(ramped.View(), source.View(), constant, 7);

    ExpectBitIdentical(plain.View(), ramped.View());
  }

  {
    AudioBuffer plain(kFrames, 2);
    AudioBuffer ramped(kFrames, 2);

    Fill(plain.View());
    Fill(ramped.View());

    ApplyGain(plain.View(), 0.4f);
    ApplyGainRamped(ramped.View(), constant, 7);

    ExpectBitIdentical(plain.View(), ramped.View());
  }
}

TEST(RampedMixerTest, RampedMixersUseTheAbsoluteFrameOfTheFirstSample) {
  GainRamp ramp;
  ramp.Start(10, 0.0f, 20);

  AudioBuffer source(kFrames, 2);
  AudioBuffer destination(kFrames, 2);

  Fill(source.View());

  constexpr std::uint64_t kFirstFrame = 8;

  AccumulateIntoRamped(destination.View(), source.View(), ramp, kFirstFrame);

  for (std::uint32_t f = 0; f < kFrames; ++f) {
    for (std::uint32_t c = 0; c < 2; ++c) {
      EXPECT_EQ(destination.View()(f, c),
                source.View()(f, c) * ramp.ValueAt(kFirstFrame + f))
          << "frame " << f << " channel " << c;
    }
  }
}

}  // namespace
}  // namespace lavanda
