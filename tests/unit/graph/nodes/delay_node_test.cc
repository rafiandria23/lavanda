#include "lavanda/graph/nodes/delay_node.h"

#include <gtest/gtest.h>

namespace lavanda {
namespace {

TEST(DelayNodeTest, DeclaresConfiguredChannelCount) {
  DelayNode node(1, 100);

  EXPECT_EQ(node.input_channel_count(), 1u);
  EXPECT_EQ(node.output_channel_count(), 1u);
  EXPECT_EQ(node.expected_input_count(), 1u);
}

TEST(DelayNodeTest, DefaultsToZeroDelayAndReportsConstructedMaxDelay) {
  DelayNode node(1, 256);

  EXPECT_EQ(node.delay_frames(), 0u);
  EXPECT_EQ(node.max_delay_frames(), 256u);
}

TEST(DelayNodeTest, SetDelayFramesUpdatesDelay) {
  DelayNode node(1, 256);
  node.SetDelayFrames(100);

  EXPECT_EQ(node.delay_frames(), 100u);
}

TEST(DelayNodeTest, SetDelayFramesClampsAboveMaxDelay) {
  DelayNode node(1, 256);
  node.SetDelayFrames(9999);

  EXPECT_EQ(node.delay_frames(), 256u);
}

TEST(DelayNodeTest, ZeroDelayIsExactPassthrough) {
  DelayNode node(1, 64);
  AudioBuffer input(6, 1);

  for (std::uint32_t f = 0; f < 6; ++f) {
    input(f, 0) = static_cast<float>(f) * 0.1f;
  }

  AudioBuffer output(6, 1);

  NodeProcessContext context;

  context.inputs[0] = input.View();
  context.input_count = 1;
  context.output = output.View();
  context.frame_count = 6;
  context.sample_rate_hz = 48000.0;

  node.Process(context);

  for (std::uint32_t f = 0; f < 6; ++f) {
    EXPECT_FLOAT_EQ(output(f, 0), input(f, 0));
  }
}

TEST(DelayNodeTest, ProducesExactSampleAccurateDelay) {
  DelayNode node(1, 64);
  node.SetDelayFrames(3);

  AudioBuffer input(6, 1);
  input(0, 0) = 1.0f;

  AudioBuffer output(6, 1);

  NodeProcessContext context;

  context.inputs[0] = input.View();
  context.input_count = 1;
  context.output = output.View();
  context.frame_count = 6;
  context.sample_rate_hz = 48000.0;

  node.Process(context);

  for (std::uint32_t f = 0; f < 6; ++f) {
    const float expected = (f == 3) ? 1.0f : 0.0f;

    EXPECT_FLOAT_EQ(output(f, 0), expected) << "at frame " << f;
  }
}

TEST(DelayNodeTest, StatePersistsAcrossProcessCalls) {
  DelayNode one_shot(1, 64);
  one_shot.SetDelayFrames(3);

  AudioBuffer one_shot_input(6, 1);
  one_shot_input(0, 0) = 1.0f;

  AudioBuffer one_shot_output(6, 1);

  NodeProcessContext one_shot_context;

  one_shot_context.inputs[0] = one_shot_input.View();
  one_shot_context.input_count = 1;
  one_shot_context.output = one_shot_output.View();
  one_shot_context.frame_count = 6;
  one_shot_context.sample_rate_hz = 48000.0;

  one_shot.Process(one_shot_context);

  DelayNode split(1, 64);
  split.SetDelayFrames(3);

  AudioBuffer first_input(3, 1);
  first_input(0, 0) = 1.0f;

  AudioBuffer second_input(3, 1);
  AudioBuffer first_output(3, 1);
  AudioBuffer second_output(3, 1);

  NodeProcessContext first_context;

  first_context.inputs[0] = first_input.View();
  first_context.input_count = 1;
  first_context.output = first_output.View();
  first_context.frame_count = 3;
  first_context.sample_rate_hz = 48000.0;

  split.Process(first_context);

  NodeProcessContext second_context;

  second_context.inputs[0] = second_input.View();
  second_context.input_count = 1;
  second_context.output = second_output.View();
  second_context.frame_count = 3;
  second_context.sample_rate_hz = 48000.0;

  split.Process(second_context);

  for (std::uint32_t f = 0; f < 3; ++f) {
    EXPECT_FLOAT_EQ(one_shot_output(f, 0), first_output(f, 0)) << "frame " << f;
  }

  for (std::uint32_t f = 0; f < 3; ++f) {
    EXPECT_FLOAT_EQ(one_shot_output(f + 3, 0), second_output(f, 0))
        << "frame " << f;
  }
}

TEST(DelayNodeTest, ZeroInputCountProducesSilenceWithoutCrash) {
  DelayNode node(1, 64);
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
