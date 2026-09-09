#include "lavanda/runtime/render_state.h"

#include <gtest/gtest.h>

#include <cmath>

namespace lavanda {
namespace {

TEST(RenderStateTest, DefaultsToNotPlaying) {
  RenderState state;

  EXPECT_FALSE(state.is_playing());
}

TEST(RenderStateTest, StartToneSetsPlaying) {
  RenderState state;
  state.ApplyCommand({CommandType::kStartTone, 0.0f});

  EXPECT_TRUE(state.is_playing());
}

TEST(RenderStateTest, StopToneClearsPlaying) {
  RenderState state;

  state.ApplyCommand({CommandType::kStartTone, 0.0f});
  state.ApplyCommand({CommandType::kStopTone, 0.0f});

  EXPECT_FALSE(state.is_playing());
}

TEST(RenderStateTest, SetFrequencyUpdatesFrequency) {
  RenderState state;
  state.ApplyCommand({CommandType::kSetFrequency, 880.0f});

  EXPECT_FLOAT_EQ(state.frequency_hz(), 880.0f);
}

TEST(RenderStateTest, SetGainUpdatesGain) {
  RenderState state;
  state.ApplyCommand({CommandType::kSetGain, 0.75f});

  EXPECT_FLOAT_EQ(state.gain(), 0.75f);
}

TEST(RenderStateTest, RendersSilenceWhenNotPlaying) {
  RenderState state;
  AudioBuffer buffer(8, 1);

  for (std::uint32_t f = 0; f < 8; ++f) buffer(f, 0) = 1.0f;

  state.Render(buffer.View(), 48000.0);

  for (std::uint32_t f = 0; f < 8; ++f) {
    EXPECT_FLOAT_EQ(buffer(f, 0), 0.0f);
  }
}

TEST(RenderStateTest, RendersNonSilenceWhenPlaying) {
  RenderState state;

  state.ApplyCommand({CommandType::kStartTone, 0.0f});
  state.ApplyCommand({CommandType::kSetFrequency, 440.0f});
  state.ApplyCommand({CommandType::kSetGain, 1.0f});

  AudioBuffer buffer(64, 1);
  state.Render(buffer.View(), 48000.0);

  bool any_nonzero = false;

  for (std::uint32_t f = 0; f < 64; ++f) {
    if (buffer(f, 0) != 0.0f) any_nonzero = true;
  }

  EXPECT_TRUE(any_nonzero);
}

TEST(RenderStateTest, RespectsGainScaling) {
  RenderState state;

  state.ApplyCommand({CommandType::kStartTone, 0.0f});
  state.ApplyCommand({CommandType::kSetFrequency, 1000.0f});
  state.ApplyCommand({CommandType::kSetGain, 0.5f});

  AudioBuffer buffer(4, 1);
  state.Render(buffer.View(), 48000.0);

  for (std::uint32_t f = 0; f < 4; ++f) {
    EXPECT_LE(std::fabs(buffer(f, 0)), 0.5f + 1e-5f);
  }
}

TEST(RenderStateTest, WritesIdenticalSampleToAllChannels) {
  RenderState state;

  state.ApplyCommand({CommandType::kStartTone, 0.0f});
  state.ApplyCommand({CommandType::kSetFrequency, 440.0f});
  state.ApplyCommand({CommandType::kSetGain, 1.0f});

  AudioBuffer buffer(4, 2);
  state.Render(buffer.View(), 48000.0);

  for (std::uint32_t f = 0; f < 4; ++f) {
    EXPECT_FLOAT_EQ(buffer(f, 0), buffer(f, 1));
  }
}

TEST(RenderStateTest, PhaseIsContinuousAcrossRenderCalls) {
  RenderState one_shot;

  one_shot.ApplyCommand({CommandType::kStartTone, 0.0f});
  one_shot.ApplyCommand({CommandType::kSetFrequency, 440.0f});
  one_shot.ApplyCommand({CommandType::kSetGain, 1.0f});

  AudioBuffer one_shot_buffer(8, 1);
  one_shot.Render(one_shot_buffer.View(), 48000.0);

  RenderState split;

  split.ApplyCommand({CommandType::kStartTone, 0.0f});
  split.ApplyCommand({CommandType::kSetFrequency, 440.0f});
  split.ApplyCommand({CommandType::kSetGain, 1.0f});

  AudioBuffer first_half(4, 1);
  AudioBuffer second_half(4, 1);

  split.Render(first_half.View(), 48000.0);
  split.Render(second_half.View(), 48000.0);

  for (std::uint32_t f = 0; f < 4; ++f) {
    EXPECT_FLOAT_EQ(one_shot_buffer(f, 0), first_half(f, 0));
  }

  for (std::uint32_t f = 0; f < 4; ++f) {
    EXPECT_FLOAT_EQ(one_shot_buffer(f + 4, 0), second_half(f, 0));
  }
}

}  // namespace
}  // namespace lavanda
