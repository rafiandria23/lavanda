#include "lavanda/graph/nodes/one_pole_low_pass_node.h"

#include <gtest/gtest.h>

#include <cmath>
#include <numbers>

namespace lavanda {
namespace {

TEST(OnePoleLowPassNodeTest, DeclaresConfiguredChannelCount) {
  OnePoleLowPassNode node(1);

  EXPECT_EQ(node.input_channel_count(), 1u);
  EXPECT_EQ(node.output_channel_count(), 1u);
  EXPECT_EQ(node.expected_input_count(), 1u);
}

TEST(OnePoleLowPassNodeTest, DefaultsToNonZeroCutoff) {
  OnePoleLowPassNode node(1);

  EXPECT_GT(node.cutoff_hz(), 0.0f);
}

TEST(OnePoleLowPassNodeTest, SetCutoffHzUpdatesCutoff) {
  OnePoleLowPassNode node(1);
  node.SetCutoffHz(500.0f);

  EXPECT_FLOAT_EQ(node.cutoff_hz(), 500.0f);
}

TEST(OnePoleLowPassNodeTest, FirstSampleOfStepResponseMatchesAlphaFormula) {
  OnePoleLowPassNode node(1);
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

  const float expected_alpha =
      1.0f - std::exp(-2.0f * std::numbers::pi_v<float> * 1000.0f /
                      static_cast<float>(kSampleRate));

  EXPECT_NEAR(output(0, 0), expected_alpha, 1e-5f);
}

TEST(OnePoleLowPassNodeTest, ConvergesTowardConstantInputOverManySamples) {
  OnePoleLowPassNode node(1);
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

  EXPECT_NEAR(output(1999, 0), 1.0f, 1e-3f);
}

TEST(OnePoleLowPassNodeTest, StatePersistsAcrossProcessCalls) {
  OnePoleLowPassNode one_shot(1);
  one_shot.SetCutoffHz(1000.0f);

  AudioBuffer one_shot_input(8, 1);

  for (std::uint32_t f = 0; f < 8; ++f) {
    one_shot_input(f, 0) = 1.0f;
  }

  AudioBuffer one_shot_output(8, 1);
  NodeProcessContext one_shot_context;

  one_shot_context.inputs[0] = one_shot_input.View();
  one_shot_context.input_count = 1;
  one_shot_context.output = one_shot_output.View();
  one_shot_context.frame_count = 8;
  one_shot_context.sample_rate_hz = 48000.0;

  one_shot.Process(one_shot_context);

  OnePoleLowPassNode split(1);
  split.SetCutoffHz(1000.0f);

  AudioBuffer first_input(4, 1);
  AudioBuffer second_input(4, 1);

  for (std::uint32_t f = 0; f < 4; ++f) {
    first_input(f, 0) = 1.0f;
    second_input(f, 0) = 1.0f;
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

TEST(OnePoleLowPassNodeTest, ZeroInputCountProducesSilenceWithoutCrash) {
  OnePoleLowPassNode node(1);
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

TEST(OnePoleLowPassNodeTest, ClonePreservesCutoffButResetsHistory) {
  OnePoleLowPassNode original(1);
  original.SetCutoffHz(500.0f);

  AudioBuffer input(50, 1);

  for (std::uint32_t f = 0; f < 50; ++f) {
    input(f, 0) = 1.0f;
  }

  AudioBuffer scratch(50, 1);
  NodeProcessContext advance_context;

  advance_context.inputs[0] = input.View();
  advance_context.input_count = 1;
  advance_context.output = scratch.View();
  advance_context.frame_count = 50;
  advance_context.sample_rate_hz = 48000.0;

  original.Process(advance_context);

  std::unique_ptr<AudioNode> clone = original.Clone();
  auto* cloned_filter = static_cast<OnePoleLowPassNode*>(clone.get());

  EXPECT_FLOAT_EQ(cloned_filter->cutoff_hz(), 500.0f);

  AudioBuffer zero_input(1, 1);
  AudioBuffer clone_output(1, 1);
  NodeProcessContext clone_context;

  clone_context.inputs[0] = zero_input.View();
  clone_context.input_count = 1;
  clone_context.output = clone_output.View();
  clone_context.frame_count = 1;
  clone_context.sample_rate_hz = 48000.0;

  clone->Process(clone_context);

  EXPECT_FLOAT_EQ(clone_output(0, 0), 0.0f);
}

}  // namespace
}  // namespace lavanda
