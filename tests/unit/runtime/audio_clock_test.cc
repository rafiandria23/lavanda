#include "lavanda/runtime/audio_clock.h"

#include <gtest/gtest.h>

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

}  // namespace
}  // namespace lavanda
