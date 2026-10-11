#include "lavanda/runtime/audio_clock.h"

#include <gtest/gtest.h>

#include "lavanda/core/time.h"

namespace lavanda {
namespace {

TEST(AudioClockTest, DefaultConstructedStartsAtZero) {
  AudioClock clock;

  EXPECT_EQ(clock.current_frame(), 0u);
}

TEST(AudioClockTest, AdvanceAccumulates) {
  AudioClock clock(48000.0);

  clock.Advance(256);
  clock.Advance(256);
  clock.Advance(512);

  EXPECT_EQ(clock.current_frame(), 1024u);
}

TEST(AudioClockTest, ReportsConstructedSampleRate) {
  AudioClock clock(44100.0);

  EXPECT_DOUBLE_EQ(clock.sample_rate_hz(), 44100.0);
}

TEST(AudioClockTest, ElapsedSecondsMatchesFramesAndRate) {
  AudioClock clock(48000.0);
  clock.Advance(48000);  // exactly one second at 48kHz

  EXPECT_DOUBLE_EQ(clock.ElapsedSeconds(), 1.0);
}

TEST(AudioClockTest, ZeroSampleRateGivesZeroElapsedSeconds) {
  AudioClock clock(0.0);
  clock.Advance(1000);

  EXPECT_DOUBLE_EQ(clock.ElapsedSeconds(), 0.0);
}

TEST(AudioClockTest, PositionStartsAtZero) {
  AudioClock clock(48000.0);

  EXPECT_EQ(ToInteger(clock.position()), 0u);
}

TEST(AudioClockTest, PositionTracksAdvancesOfDifferentSizes) {
  AudioClock clock(48000.0);

  clock.Advance(256);

  EXPECT_EQ(ToInteger(clock.position()), 256u);

  clock.Advance(1);

  EXPECT_EQ(ToInteger(clock.position()), 257u);

  clock.Advance(4096);

  EXPECT_EQ(ToInteger(clock.position()), 4353u);
}

TEST(AudioClockTest, PositionAgreesWithCurrentFrame) {
  AudioClock clock(48000.0);
  clock.Advance(512);
  clock.Advance(37);

  EXPECT_EQ(ToInteger(clock.position()), clock.current_frame());
}

}  // namespace
}  // namespace lavanda
