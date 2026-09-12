#include "lavanda/graph/nodes/mixer_node.h"

#include <gtest/gtest.h>

namespace lavanda {
namespace {

TEST(MixerNodeTest, DeclaresConfiguredChannelAndInputCount) {
  MixerNode node(2, 3);

  EXPECT_EQ(node.input_channel_count(), 2u);
  EXPECT_EQ(node.output_channel_count(), 2u);
  EXPECT_EQ(node.expected_input_count(), 3u);
}

TEST(MixerNodeTest, InputCountIsClampedToMaxNodeInputs) {
  MixerNode node(1, 999);

  EXPECT_LE(node.expected_input_count(), kMaxNodeInputs);
}

TEST(MixerNodeTest, SumsThreeConstantInputsMatchingSpecExample) {
  MixerNode node(1, 3);

  AudioBuffer a(4, 1);
  AudioBuffer b(4, 1);
  AudioBuffer c(4, 1);

  for (std::uint32_t f = 0; f < 4; ++f) {
    a(f, 0) = 0.25f;
    b(f, 0) = 0.50f;
    c(f, 0) = 0.0f;
  }

  AudioBuffer output(4, 1);

  NodeProcessContext context;

  context.inputs[0] = a.View();
  context.inputs[1] = b.View();
  context.inputs[2] = c.View();
  context.input_count = 3;
  context.output = output.View();
  context.frame_count = 4;
  context.sample_rate_hz = 48000.0;

  node.Process(context);

  for (std::uint32_t f = 0; f < 4; ++f) {
    EXPECT_FLOAT_EQ(output(f, 0), 0.75f);
  }
}

TEST(MixerNodeTest, ClearsOutputBeforeAccumulating) {
  MixerNode node(1, 1);
  AudioBuffer input(2, 1);

  input(0, 0) = 0.5f;
  input(1, 0) = 0.5f;

  AudioBuffer output(2, 1);

  output(0, 0) = 99.0f;

  NodeProcessContext context;

  context.inputs[0] = input.View();
  context.input_count = 1;
  context.output = output.View();
  context.frame_count = 2;
  context.sample_rate_hz = 48000.0;

  node.Process(context);

  EXPECT_FLOAT_EQ(output(0, 0), 0.5f);
}

TEST(MixerNodeTest, FewerActiveInputsThanConfiguredMaxIsHandledSafely) {
  MixerNode node(1, 3);
  AudioBuffer input(2, 1);

  input(0, 0) = 0.4f;
  input(1, 0) = 0.4f;

  AudioBuffer output(2, 1);

  NodeProcessContext context;

  context.inputs[0] = input.View();
  context.input_count = 1;
  context.output = output.View();
  context.frame_count = 2;
  context.sample_rate_hz = 48000.0;

  node.Process(context);

  EXPECT_FLOAT_EQ(output(0, 0), 0.4f);
}

TEST(MixerNodeTest, ZeroInputCountProducesSilenceWithoutCrash) {
  MixerNode node(1, 2);
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

TEST(MixerNodeTest, ClonePreservesChannelAndInputCount) {
  MixerNode original(2, 3);
  std::unique_ptr<AudioNode> clone = original.Clone();

  EXPECT_EQ(clone->input_channel_count(), 2u);
  EXPECT_EQ(clone->expected_input_count(), 3u);
}

}  // namespace
}  // namespace lavanda
