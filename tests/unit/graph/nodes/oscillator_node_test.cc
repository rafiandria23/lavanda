#include "lavanda/graph/nodes/oscillator_node.h"

#include <gtest/gtest.h>

#include <cmath>

namespace lavanda {
namespace {

NodeProcessContext MakeMonoContext(AudioBuffer& buffer, double sample_rate_hz) {
  NodeProcessContext context;

  context.output = buffer.View();
  context.frame_count = buffer.frame_count();
  context.sample_rate_hz = sample_rate_hz;

  return context;
}

TEST(OscillatorNodeTest, DeclaresSourceNodeShape) {
  OscillatorNode node;

  EXPECT_EQ(node.input_channel_count(), 0u);
  EXPECT_EQ(node.output_channel_count(), 1u);
  EXPECT_EQ(node.expected_input_count(), 0u);
}

TEST(OscillatorNodeTest, DefaultsToNonZeroFrequencyAndUnityGain) {
  OscillatorNode node;

  EXPECT_FLOAT_EQ(node.frequency_hz(), 440.0f);
  EXPECT_FLOAT_EQ(node.gain(), 1.0f);
}

TEST(OscillatorNodeTest, SetFrequencyUpdatesFrequency) {
  OscillatorNode node;
  node.SetFrequency(880.0f);

  EXPECT_FLOAT_EQ(node.frequency_hz(), 880.0f);
}

TEST(OscillatorNodeTest, SetGainUpdatesGain) {
  OscillatorNode node;
  node.SetGain(0.25f);

  EXPECT_FLOAT_EQ(node.gain(), 0.25f);
}

TEST(OscillatorNodeTest, ProducesNonSilentOutput) {
  OscillatorNode node;
  AudioBuffer buffer(64, 1);
  NodeProcessContext context = MakeMonoContext(buffer, 48000.0);

  node.Process(context);

  bool any_nonzero = false;

  for (std::uint32_t f = 0; f < 64; ++f) {
    if (buffer(f, 0) != 0.0f) {
      any_nonzero = true;
    }
  }

  EXPECT_TRUE(any_nonzero);
}

TEST(OscillatorNodeTest, RespectsGainScaling) {
  OscillatorNode node;
  node.SetGain(0.5f);

  AudioBuffer buffer(16, 1);
  NodeProcessContext context = MakeMonoContext(buffer, 48000.0);

  node.Process(context);

  for (std::uint32_t f = 0; f < 16; ++f) {
    EXPECT_LE(std::fabs(buffer(f, 0)), 0.5f + 1e-5f);
  }
}

TEST(OscillatorNodeTest, PhaseIsContinuousAcrossProcessCalls) {
  OscillatorNode one_shot;
  one_shot.SetFrequency(440.0f);

  AudioBuffer one_shot_buffer(8, 1);
  NodeProcessContext one_shot_context =
      MakeMonoContext(one_shot_buffer, 48000.0);

  one_shot.Process(one_shot_context);

  OscillatorNode split;
  split.SetFrequency(440.0f);

  AudioBuffer first_half(4, 1);
  AudioBuffer second_half(4, 1);
  NodeProcessContext first_context = MakeMonoContext(first_half, 48000.0);
  NodeProcessContext second_context = MakeMonoContext(second_half, 48000.0);

  split.Process(first_context);
  split.Process(second_context);

  for (std::uint32_t f = 0; f < 4; ++f) {
    EXPECT_FLOAT_EQ(one_shot_buffer(f, 0), first_half(f, 0));
  }

  for (std::uint32_t f = 0; f < 4; ++f) {
    EXPECT_FLOAT_EQ(one_shot_buffer(f + 4, 0), second_half(f, 0));
  }
}

TEST(OscillatorNodeTest, ZeroSampleRateProducesSilenceWithoutCrash) {
  OscillatorNode node;
  AudioBuffer buffer(8, 1);

  for (std::uint32_t f = 0; f < 8; ++f) {
    buffer(f, 0) = 1.0f;
  }

  NodeProcessContext context = MakeMonoContext(buffer, 0.0);

  node.Process(context);

  for (std::uint32_t f = 0; f < 8; ++f) {
    EXPECT_FLOAT_EQ(buffer(f, 0), 0.0f);
  }
}

TEST(OscillatorNodeTest, ClonePreservesParametersButResetsPhase) {
  OscillatorNode original;

  original.SetFrequency(880.0f);
  original.SetGain(0.5f);

  AudioBuffer advance_buffer(100, 1);
  NodeProcessContext advance_context = MakeMonoContext(advance_buffer, 48000.0);

  original.Process(advance_context);

  std::unique_ptr<AudioNode> clone = original.Clone();
  auto* cloned_oscillator = static_cast<OscillatorNode*>(clone.get());

  EXPECT_FLOAT_EQ(cloned_oscillator->frequency_hz(), 880.0f);
  EXPECT_FLOAT_EQ(cloned_oscillator->gain(), 0.5f);

  OscillatorNode fresh;

  fresh.SetFrequency(880.0f);
  fresh.SetGain(0.5f);

  AudioBuffer clone_output(1, 1);
  AudioBuffer fresh_output(1, 1);
  NodeProcessContext clone_context = MakeMonoContext(clone_output, 48000.0);
  NodeProcessContext fresh_context = MakeMonoContext(fresh_output, 48000.0);

  clone->Process(clone_context);
  fresh.Process(fresh_context);

  EXPECT_FLOAT_EQ(clone_output(0, 0), fresh_output(0, 0));
}

}  // namespace
}  // namespace lavanda
