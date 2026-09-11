#include "lavanda/graph/nodes/pan_node.h"

#include <gtest/gtest.h>

#include <cmath>

namespace lavanda {
namespace {

constexpr float kFloatTolerance = 1e-5f;

NodeProcessContext MakePanContext(AudioBuffer& input, AudioBuffer& output) {
  NodeProcessContext context;

  context.inputs[0] = input.View();
  context.input_count = 1;
  context.output = output.View();
  context.frame_count = input.frame_count();
  context.sample_rate_hz = 48000.0;

  return context;
}

TEST(PanNodeTest, DeclaresMonoInStereoOutShape) {
  PanNode node;

  EXPECT_EQ(node.input_channel_count(), 1u);
  EXPECT_EQ(node.output_channel_count(), 2u);
  EXPECT_EQ(node.expected_input_count(), 1u);
}

TEST(PanNodeTest, DefaultsToCenterPan) {
  PanNode node;

  EXPECT_FLOAT_EQ(node.pan(), 0.0f);
}

TEST(PanNodeTest, HardLeftProducesSilenceOnRightChannel) {
  PanNode node;
  node.SetPan(-1.0f);

  AudioBuffer input(4, 1);

  for (std::uint32_t f = 0; f < 4; ++f) {
    input(f, 0) = 1.0f;
  }

  AudioBuffer output(4, 2);
  NodeProcessContext context = MakePanContext(input, output);

  node.Process(context);

  for (std::uint32_t f = 0; f < 4; ++f) {
    EXPECT_NEAR(output(f, 1), 0.0f, kFloatTolerance);
    EXPECT_NEAR(output(f, 0), 1.0f, kFloatTolerance);
  }
}

TEST(PanNodeTest, HardRightProducesSilenceOnLeftChannel) {
  PanNode node;
  node.SetPan(1.0f);

  AudioBuffer input(4, 1);

  for (std::uint32_t f = 0; f < 4; ++f) {
    input(f, 0) = 1.0f;
  }

  AudioBuffer output(4, 2);
  NodeProcessContext context = MakePanContext(input, output);

  node.Process(context);

  for (std::uint32_t f = 0; f < 4; ++f) {
    EXPECT_NEAR(output(f, 0), 0.0f, kFloatTolerance);
    EXPECT_NEAR(output(f, 1), 1.0f, kFloatTolerance);
  }
}

TEST(PanNodeTest, CenterPanSplitsEquallyBetweenChannels) {
  PanNode node;

  AudioBuffer input(2, 1);

  input(0, 0) = 1.0f;
  input(1, 0) = 1.0f;

  AudioBuffer output(2, 2);

  NodeProcessContext context = MakePanContext(input, output);

  node.Process(context);

  for (std::uint32_t f = 0; f < 2; ++f) {
    EXPECT_NEAR(output(f, 0), output(f, 1), kFloatTolerance);
  }
}

TEST(PanNodeTest, ClearsOutputBeforeWriting) {
  PanNode node;
  AudioBuffer input(2, 1);

  input(0, 0) = 1.0f;
  input(1, 0) = 1.0f;

  AudioBuffer output(2, 2);

  output(0, 0) = 5.0f;
  output(0, 1) = 5.0f;

  NodeProcessContext context = MakePanContext(input, output);

  node.Process(context);

  EXPECT_LE(std::fabs(output(0, 0)), 1.0f + kFloatTolerance);
  EXPECT_LE(std::fabs(output(0, 1)), 1.0f + kFloatTolerance);
}

TEST(PanNodeTest, ZeroInputCountProducesSilenceWithoutCrash) {
  PanNode node;
  AudioBuffer output(4, 2);

  for (std::uint32_t f = 0; f < 4; ++f) {
    output(f, 0) = 1.0f;
    output(f, 1) = 1.0f;
  }

  NodeProcessContext context;

  context.input_count = 0;
  context.output = output.View();
  context.frame_count = 4;
  context.sample_rate_hz = 48000.0;

  node.Process(context);

  for (std::uint32_t f = 0; f < 4; ++f) {
    EXPECT_FLOAT_EQ(output(f, 0), 0.0f);
    EXPECT_FLOAT_EQ(output(f, 1), 0.0f);
  }
}

}  // namespace
}  // namespace lavanda
