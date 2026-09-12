#include "lavanda/graph/graph_executor.h"

#include <gtest/gtest.h>

#include <cmath>
#include <memory>

#include "lavanda/graph/graph_compiler.h"
#include "lavanda/graph/nodes/gain_node.h"
#include "lavanda/graph/nodes/mixer_node.h"
#include "lavanda/graph/nodes/oscillator_node.h"
#include "lavanda/graph/nodes/output_node.h"

namespace lavanda {
namespace {

TEST(GraphExecutorTest, RendersGainChainAccumulatingOntoOutput) {
  AudioGraph graph;
  auto osc = graph.AddNode(std::make_unique<OscillatorNode>());
  auto gain = graph.AddNode(std::make_unique<GainNode>(1));
  auto output = graph.AddNode(std::make_unique<OutputNode>(1));

  ASSERT_TRUE(osc.ok() && gain.ok() && output.ok());

  static_cast<OscillatorNode*>(graph.GetNode(osc.value()))
      ->SetFrequency(440.0f);
  static_cast<GainNode*>(graph.GetNode(gain.value()))->SetGain(0.5f);

  ASSERT_TRUE(graph.Connect(osc.value(), gain.value(), 0).ok());
  ASSERT_TRUE(graph.Connect(gain.value(), output.value(), 0).ok());
  ASSERT_TRUE(graph.SetOutput(output.value()).ok());

  StatusOr<GraphExecutionPlan> plan_or = GraphCompiler::Compile(graph, 512);

  ASSERT_TRUE(plan_or.ok());

  GraphExecutionPlan& plan = plan_or.value();

  AudioBuffer destination(64, 1);
  destination.Clear();

  GraphExecutor::Render(plan, destination.View(), 64, 48000.0, 0);

  bool any_nonzero = false;

  for (std::uint32_t f = 0; f < 64; ++f) {
    EXPECT_LE(std::fabs(destination(f, 0)), 0.5f + 1e-5f);

    if (destination(f, 0) != 0.0f) {
      any_nonzero = true;
    }
  }

  EXPECT_TRUE(any_nonzero);
}

TEST(GraphExecutorTest, RenderAccumulatesRatherThanOverwritesDestination) {
  AudioGraph graph;
  auto osc = graph.AddNode(std::make_unique<OscillatorNode>());

  ASSERT_TRUE(osc.ok());

  static_cast<OscillatorNode*>(graph.GetNode(osc.value()))->SetGain(0.0f);

  ASSERT_TRUE(graph.SetOutput(osc.value()).ok());

  StatusOr<GraphExecutionPlan> plan_or = GraphCompiler::Compile(graph, 512);

  ASSERT_TRUE(plan_or.ok());

  GraphExecutionPlan& plan = plan_or.value();
  AudioBuffer destination(4, 1);

  for (std::uint32_t f = 0; f < 4; ++f) {
    destination(f, 0) = 0.3f;
  }

  GraphExecutor::Render(plan, destination.View(), 4, 48000.0, 0);

  for (std::uint32_t f = 0; f < 4; ++f) {
    EXPECT_FLOAT_EQ(destination(f, 0), 0.3f)
        << "silent graph output added to pre-existing 0.3 must still read 0.3";
  }
}

TEST(GraphExecutorTest, ChannelMismatchIsGracefulNoOp) {
  AudioGraph graph;
  auto osc = graph.AddNode(std::make_unique<OscillatorNode>());

  ASSERT_TRUE(osc.ok());
  ASSERT_TRUE(graph.SetOutput(osc.value()).ok());

  StatusOr<GraphExecutionPlan> plan_or = GraphCompiler::Compile(graph, 512);

  ASSERT_TRUE(plan_or.ok());

  GraphExecutionPlan& plan = plan_or.value();

  AudioBuffer destination(4, 2);

  for (std::uint32_t f = 0; f < 4; ++f) {
    destination(f, 0) = 0.7f;
    destination(f, 1) = 0.7f;
  }

  GraphExecutor::Render(plan, destination.View(), 4, 48000.0, 0);

  for (std::uint32_t f = 0; f < 4; ++f) {
    EXPECT_FLOAT_EQ(destination(f, 0), 0.7f)
        << "channel mismatch must leave destination completely untouched";
    EXPECT_FLOAT_EQ(destination(f, 1), 0.7f);
  }
}

TEST(GraphExecutorTest, FrameCountExceedingMaxFramesPerBlockIsGracefulNoOp) {
  AudioGraph graph;
  auto osc = graph.AddNode(std::make_unique<OscillatorNode>());

  ASSERT_TRUE(osc.ok());
  ASSERT_TRUE(graph.SetOutput(osc.value()).ok());

  StatusOr<GraphExecutionPlan> plan_or = GraphCompiler::Compile(graph, 16);

  ASSERT_TRUE(plan_or.ok());

  GraphExecutionPlan& plan = plan_or.value();

  AudioBuffer destination(32, 1);

  for (std::uint32_t f = 0; f < 32; ++f) {
    destination(f, 0) = 0.4f;
  }

  GraphExecutor::Render(plan, destination.View(), 32, 48000.0, 0);

  for (std::uint32_t f = 0; f < 32; ++f) {
    EXPECT_FLOAT_EQ(destination(f, 0), 0.4f);
  }
}

TEST(GraphExecutorTest, MixerFanInSumsCorrectlyThroughRealExecutor) {
  AudioGraph graph;
  auto osc_a = graph.AddNode(std::make_unique<OscillatorNode>());
  auto osc_b = graph.AddNode(std::make_unique<OscillatorNode>());
  auto mixer = graph.AddNode(std::make_unique<MixerNode>(1, 2));
  auto output = graph.AddNode(std::make_unique<OutputNode>(1));

  ASSERT_TRUE(osc_a.ok() && osc_b.ok() && mixer.ok() && output.ok());

  static_cast<OscillatorNode*>(graph.GetNode(osc_a.value()))->SetGain(0.0f);
  static_cast<OscillatorNode*>(graph.GetNode(osc_b.value()))->SetGain(0.0f);

  ASSERT_TRUE(graph.Connect(osc_a.value(), mixer.value(), 0).ok());
  ASSERT_TRUE(graph.Connect(osc_b.value(), mixer.value(), 1).ok());
  ASSERT_TRUE(graph.Connect(mixer.value(), output.value(), 0).ok());
  ASSERT_TRUE(graph.SetOutput(output.value()).ok());

  StatusOr<GraphExecutionPlan> plan_or = GraphCompiler::Compile(graph, 512);

  ASSERT_TRUE(plan_or.ok());

  GraphExecutionPlan& plan = plan_or.value();

  AudioBuffer destination(8, 1);
  destination.Clear();

  GraphExecutor::Render(plan, destination.View(), 8, 48000.0, 0);

  for (std::uint32_t f = 0; f < 8; ++f) {
    EXPECT_FLOAT_EQ(destination(f, 0), 0.0f);
  }
}

TEST(GraphExecutorTest, NodeStatePersistsAcrossRepeatedRenderCallsOnSamePlan) {
  AudioGraph graph;
  auto osc = graph.AddNode(std::make_unique<OscillatorNode>());

  ASSERT_TRUE(osc.ok());

  static_cast<OscillatorNode*>(graph.GetNode(osc.value()))
      ->SetFrequency(440.0f);

  ASSERT_TRUE(graph.SetOutput(osc.value()).ok());

  StatusOr<GraphExecutionPlan> one_shot_or = GraphCompiler::Compile(graph, 512);

  ASSERT_TRUE(one_shot_or.ok());

  GraphExecutionPlan& one_shot = one_shot_or.value();

  AudioBuffer one_shot_output(8, 1);
  one_shot_output.Clear();

  GraphExecutor::Render(one_shot, one_shot_output.View(), 8, 48000.0, 0);

  StatusOr<GraphExecutionPlan> split_or = GraphCompiler::Compile(graph, 512);

  ASSERT_TRUE(split_or.ok());

  GraphExecutionPlan& split = split_or.value();

  AudioBuffer first_block_destination(4, 1);
  first_block_destination.Clear();

  GraphExecutor::Render(split, first_block_destination.View(), 4, 48000.0, 0);

  AudioBuffer second_block_destination(4, 1);
  second_block_destination.Clear();

  GraphExecutor::Render(split, second_block_destination.View(), 4, 48000.0, 4);

  for (std::uint32_t f = 0; f < 4; ++f) {
    EXPECT_NEAR(one_shot_output(f, 0), first_block_destination(f, 0), 1e-4f);
  }

  for (std::uint32_t f = 0; f < 4; ++f) {
    EXPECT_NEAR(one_shot_output(f + 4, 0), second_block_destination(f, 0),
                1e-4f);
  }
}

}  // namespace
}  // namespace lavanda
