#include "lavanda/graph/nodes/gain_node.h"

#include <gtest/gtest.h>

namespace lavanda {
namespace {

TEST(GainNodeTest, DeclaresConfiguredChannelCount) {
  GainNode mono(1);

  EXPECT_EQ(mono.input_channel_count(), 1u);
  EXPECT_EQ(mono.output_channel_count(), 1u);
  EXPECT_EQ(mono.expected_input_count(), 1u);

  GainNode stereo(2);

  EXPECT_EQ(stereo.input_channel_count(), 2u);
  EXPECT_EQ(stereo.output_channel_count(), 2u);
}

TEST(GainNodeTest, DefaultsToUnityGain) {
  GainNode node(1);

  EXPECT_FLOAT_EQ(node.gain(), 1.0f);
}

TEST(GainNodeTest, SetGainUpdatesGain) {
  GainNode node(1);
  node.SetGain(0.5f);

  EXPECT_FLOAT_EQ(node.gain(), 0.5f);
}

TEST(GainNodeTest, ScalesMonoInputExactly) {
  GainNode node(1);
  node.SetGain(0.5f);

  AudioBuffer input(4, 1);

  for (std::uint32_t f = 0; f < 4; ++f) {
    input(f, 0) = 0.8f;
  }

  AudioBuffer output(4, 1);

  NodeProcessContext context;

  context.inputs[0] = input.View();
  context.input_count = 1;
  context.output = output.View();
  context.frame_count = 4;
  context.sample_rate_hz = 48000.0;

  node.Process(context);

  for (std::uint32_t f = 0; f < 4; ++f) {
    EXPECT_FLOAT_EQ(output(f, 0), 0.4f);
  }
}

TEST(GainNodeTest, ScalesStereoInputExactlyPerChannel) {
  GainNode node(2);
  node.SetGain(2.0f);

  AudioBuffer input(2, 2);

  input(0, 0) = 0.1f;
  input(0, 1) = 0.2f;
  input(1, 0) = 0.3f;
  input(1, 1) = 0.4f;

  AudioBuffer output(2, 2);
  NodeProcessContext context;

  context.inputs[0] = input.View();
  context.input_count = 1;
  context.output = output.View();
  context.frame_count = 2;
  context.sample_rate_hz = 48000.0;

  node.Process(context);

  EXPECT_FLOAT_EQ(output(0, 0), 0.2f);
  EXPECT_FLOAT_EQ(output(0, 1), 0.4f);
  EXPECT_FLOAT_EQ(output(1, 0), 0.6f);
  EXPECT_FLOAT_EQ(output(1, 1), 0.8f);
}

TEST(GainNodeTest, ZeroInputCountProducesSilenceWithoutCrash) {
  GainNode node(1);
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
