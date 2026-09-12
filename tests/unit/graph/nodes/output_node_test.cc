#include "lavanda/graph/nodes/output_node.h"

#include <gtest/gtest.h>

namespace lavanda {
namespace {

TEST(OutputNodeTest, DeclaresConfiguredChannelCount) {
  OutputNode mono(1);

  EXPECT_EQ(mono.input_channel_count(), 1u);
  EXPECT_EQ(mono.output_channel_count(), 1u);
  EXPECT_EQ(mono.expected_input_count(), 1u);

  OutputNode stereo(2);

  EXPECT_EQ(stereo.input_channel_count(), 2u);
  EXPECT_EQ(stereo.output_channel_count(), 2u);
}

TEST(OutputNodeTest, CopiesMonoInputToOutputExactly) {
  OutputNode node(1);
  AudioBuffer input(4, 1);

  for (std::uint32_t f = 0; f < 4; ++f) {
    input(f, 0) = static_cast<float>(f) * 0.1f;
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
    EXPECT_FLOAT_EQ(output(f, 0), input(f, 0));
  }
}

TEST(OutputNodeTest, CopiesStereoInputToOutputExactly) {
  OutputNode node(2);
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

  EXPECT_FLOAT_EQ(output(0, 0), 0.1f);
  EXPECT_FLOAT_EQ(output(0, 1), 0.2f);
  EXPECT_FLOAT_EQ(output(1, 0), 0.3f);
  EXPECT_FLOAT_EQ(output(1, 1), 0.4f);
}

TEST(OutputNodeTest, ZeroInputCountProducesSilenceWithoutCrash) {
  OutputNode node(1);
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
