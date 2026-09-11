#include "lavanda/graph/nodes/one_pole_high_pass_node.h"

#include <gtest/gtest.h>

#include <cmath>
#include <numbers>

namespace lavanda {
namespace {

TEST(OnePoleHighPassNodeTest, DeclaresConfiguredChannelCount) {
  OnePoleHighPassNode node(1);

  EXPECT_EQ(node.input_channel_count(), 1u);
  EXPECT_EQ(node.output_channel_count(), 1u);
  EXPECT_EQ(node.expected_input_count(), 1u);
}

TEST(OnePoleHighPassNodeTest, DefaultsToNonZeroCutoff) {
  OnePoleHighPassNode node(1);

  EXPECT_GT(node.cutoff_hz(), 0.0f);
}

TEST(OnePoleHighPassNodeTest, SetCutoffHzUpdatesCutoff) {
  OnePoleHighPassNode node(1);
  node.SetCutoffHz(300.0f);

  EXPECT_FLOAT_EQ(node.cutoff_hz(), 300.0f);
}

TEST(OnePoleHighPassNodeTest, FirstSampleOfStepResponseMatchesAlphaFormula) {
  OnePoleHighPassNode node(1);
  node.SetCutoffHz(1000.0f);

  constexpr double kSampleRate = 48000.0;

  AudioBuffer input(1, 1);
  input(0, 0) = 1.0f;

  AudioBuffer output(1, 1);

  NodeProcessContext context;

  context.inputs[0] = input.View();
  context.input_count = 1;
  context.output = output.View();
  context.frame_count = 1;
  context.sample_rate_hz = kSampleRate;

  node.Process(context);

  const float dt = 1.0f / static_cast<float>(kSampleRate);
  const float rc = 1.0f / (2.0f * std::numbers::pi_v<float> * 1000.0f);
  const float expected_alpha = rc / (rc + dt);

  EXPECT_NEAR(output(0, 0), expected_alpha, 1e-5f);
}

TEST(OnePoleHighPassNodeTest, DecaysTowardZeroForConstantInputOverManySamples) {
  OnePoleHighPassNode node(1);
  node.SetCutoffHz(2000.0f);

  AudioBuffer input(2000, 1);

  for (std::uint32_t f = 0; f < 2000; ++f) {
    input(f, 0) = 1.0f;
  }

  AudioBuffer output(2000, 1);

  NodeProcessContext context;

  context.inputs[0] = input.View();
  context.input_count = 1;
  context.output = output.View();
  context.frame_count = 2000;
  context.sample_rate_hz = 48000.0;

  node.Process(context);

  EXPECT_NEAR(output(1999, 0), 0.0f, 1e-3f);
}

TEST(OnePoleHighPassNodeTest, StatePersistsAcrossProcessCalls) {
  OnePoleHighPassNode one_shot(1);
  one_shot.SetCutoffHz(1000.0f);

  AudioBuffer one_shot_input(8, 1);

  for (std::uint32_t f = 0; f < 8; ++f) {
    one_shot_input(f, 0) = (f % 2 == 0) ? 1.0f : -1.0f;
  }

  AudioBuffer one_shot_output(8, 1);
  NodeProcessContext one_shot_context;

  one_shot_context.inputs[0] = one_shot_input.View();
  one_shot_context.input_count = 1;
  one_shot_context.output = one_shot_output.View();
  one_shot_context.frame_count = 8;
  one_shot_context.sample_rate_hz = 48000.0;

  one_shot.Process(one_shot_context);

  OnePoleHighPassNode split(1);
  split.SetCutoffHz(1000.0f);

  AudioBuffer first_input(4, 1);
  AudioBuffer second_input(4, 1);

  for (std::uint32_t f = 0; f < 4; ++f) {
    first_input(f, 0) = (f % 2 == 0) ? 1.0f : -1.0f;
    second_input(f, 0) = ((f + 4) % 2 == 0) ? 1.0f : -1.0f;
  }

  AudioBuffer first_output(4, 1);
  AudioBuffer second_output(4, 1);

  NodeProcessContext first_context;

  first_context.inputs[0] = first_input.View();
  first_context.input_count = 1;
  first_context.output = first_output.View();
  first_context.frame_count = 4;
  first_context.sample_rate_hz = 48000.0;

  split.Process(first_context);

  NodeProcessContext second_context;

  second_context.inputs[0] = second_input.View();
  second_context.input_count = 1;
  second_context.output = second_output.View();
  second_context.frame_count = 4;
  second_context.sample_rate_hz = 48000.0;

  split.Process(second_context);

  for (std::uint32_t f = 0; f < 4; ++f) {
    EXPECT_NEAR(one_shot_output(f, 0), first_output(f, 0), 1e-5f);
  }

  for (std::uint32_t f = 0; f < 4; ++f) {
    EXPECT_NEAR(one_shot_output(f + 4, 0), second_output(f, 0), 1e-5f);
  }
}

TEST(OnePoleHighPassNodeTest, ZeroInputCountProducesSilenceWithoutCrash) {
  OnePoleHighPassNode node(1);
  AudioBuffer output(4, 1);

  for (std::uint32_t f = 0; f < 4; ++f) {
    output(f, 0) = 1.0f;
  }

  NodeProcessContext context;

  context.input_count = 0;
  context.output = output.View();
  context.frame_count = 4;
  context.sample_rate_hz = 48000.0;

  node.Process(context);

  for (std::uint32_t f = 0; f < 4; ++f) {
    EXPECT_FLOAT_EQ(output(f, 0), 0.0f);
  }
}

}  // namespace
}  // namespace lavanda
