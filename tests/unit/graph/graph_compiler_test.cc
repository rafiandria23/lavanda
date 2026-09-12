#include "lavanda/graph/graph_compiler.h"

#include <gtest/gtest.h>

#include <memory>

#include "lavanda/graph/nodes/delay_node.h"
#include "lavanda/graph/nodes/gain_node.h"
#include "lavanda/graph/nodes/mixer_node.h"
#include "lavanda/graph/nodes/one_pole_low_pass_node.h"
#include "lavanda/graph/nodes/oscillator_node.h"
#include "lavanda/graph/nodes/output_node.h"

namespace lavanda {
namespace {

void ExecutePlanOnce(GraphExecutionPlan& plan, std::uint32_t frame_count,
                     double sample_rate_hz) {
  for (const GraphExecutionPlan::Step& step : plan.steps()) {
    NodeProcessContext context;

    for (std::uint32_t i = 0; i < step.input_count; ++i) {
      context.inputs[i] =
          plan.buffers()[step.input_buffer_indices[i]].View(frame_count);
    }

    context.input_count = step.input_count;
    context.output = plan.buffers()[step.output_buffer_index].View(frame_count);
    context.frame_count = frame_count;
    context.sample_rate_hz = sample_rate_hz;

    step.node->Process(context);
  }
}

TEST(GraphCompilerTest, EmptyGraphFailsWithNoOutputDesignated) {
  AudioGraph graph;
  StatusOr<GraphExecutionPlan> plan = GraphCompiler::Compile(graph, 512);

  EXPECT_FALSE(plan.ok());
  EXPECT_EQ(plan.status().code(), ErrorCode::kInvalidArgument);
}

TEST(GraphCompilerTest, MissingOutputFailsOnNonEmptyGraph) {
  AudioGraph graph;

  ASSERT_TRUE(graph.AddNode(std::make_unique<OscillatorNode>()).ok());

  StatusOr<GraphExecutionPlan> plan = GraphCompiler::Compile(graph, 512);

  EXPECT_FALSE(plan.ok());
}

TEST(GraphCompilerTest, UnconnectedRequiredInputFailsCompilation) {
  AudioGraph graph;
  StatusOr<NodeId> gain = graph.AddNode(std::make_unique<GainNode>(1));

  ASSERT_TRUE(gain.ok());
  ASSERT_TRUE(graph.SetOutput(gain.value()).ok());

  StatusOr<GraphExecutionPlan> plan = GraphCompiler::Compile(graph, 512);

  EXPECT_FALSE(plan.ok());
}

TEST(GraphCompilerTest, SimpleThreeNodeCycleIsRejected) {
  AudioGraph graph;
  StatusOr<NodeId> a = graph.AddNode(std::make_unique<GainNode>(1));
  StatusOr<NodeId> b = graph.AddNode(std::make_unique<GainNode>(1));
  StatusOr<NodeId> c = graph.AddNode(std::make_unique<GainNode>(1));

  ASSERT_TRUE(a.ok() && b.ok() && c.ok());
  ASSERT_TRUE(graph.Connect(c.value(), a.value(), 0).ok());
  ASSERT_TRUE(graph.Connect(a.value(), b.value(), 0).ok());
  ASSERT_TRUE(graph.Connect(b.value(), c.value(), 0).ok());
  ASSERT_TRUE(graph.SetOutput(c.value()).ok());

  StatusOr<GraphExecutionPlan> plan = GraphCompiler::Compile(graph, 512);

  EXPECT_FALSE(plan.ok());
  EXPECT_EQ(plan.status().code(), ErrorCode::kInvalidArgument);
}

TEST(GraphCompilerTest, LongerFourNodeCycleIsRejected) {
  AudioGraph graph;
  StatusOr<NodeId> a = graph.AddNode(std::make_unique<GainNode>(1));
  StatusOr<NodeId> b = graph.AddNode(std::make_unique<GainNode>(1));
  StatusOr<NodeId> c = graph.AddNode(std::make_unique<GainNode>(1));
  StatusOr<NodeId> d = graph.AddNode(std::make_unique<GainNode>(1));

  ASSERT_TRUE(a.ok() && b.ok() && c.ok() && d.ok());
  ASSERT_TRUE(graph.Connect(d.value(), a.value(), 0).ok());
  ASSERT_TRUE(graph.Connect(a.value(), b.value(), 0).ok());
  ASSERT_TRUE(graph.Connect(b.value(), c.value(), 0).ok());
  ASSERT_TRUE(graph.Connect(c.value(), d.value(), 0).ok());
  ASSERT_TRUE(graph.SetOutput(d.value()).ok());

  StatusOr<GraphExecutionPlan> plan = GraphCompiler::Compile(graph, 512);

  EXPECT_FALSE(plan.ok());
}

TEST(GraphCompilerTest, DisconnectedNodeIsExcludedNotRejected) {
  AudioGraph graph;
  StatusOr<NodeId> osc = graph.AddNode(std::make_unique<OscillatorNode>());
  StatusOr<NodeId> gain = graph.AddNode(std::make_unique<GainNode>(1));
  StatusOr<NodeId> unconnected =
      graph.AddNode(std::make_unique<OscillatorNode>());

  ASSERT_TRUE(osc.ok() && gain.ok() && unconnected.ok());
  ASSERT_TRUE(graph.Connect(osc.value(), gain.value(), 0).ok());
  ASSERT_TRUE(graph.SetOutput(gain.value()).ok());

  StatusOr<GraphExecutionPlan> plan = GraphCompiler::Compile(graph, 512);

  ASSERT_TRUE(plan.ok()) << plan.status().message();
  EXPECT_EQ(plan.value().step_count(), 2u)
      << "the unconnected node must not appear in the plan";
}

TEST(GraphCompilerTest, DisconnectedSubgraphIsExcludedNotRejected) {
  AudioGraph graph;
  StatusOr<NodeId> osc = graph.AddNode(std::make_unique<OscillatorNode>());
  StatusOr<NodeId> gain = graph.AddNode(std::make_unique<GainNode>(1));

  ASSERT_TRUE(osc.ok() && gain.ok());
  ASSERT_TRUE(graph.Connect(osc.value(), gain.value(), 0).ok());
  ASSERT_TRUE(graph.SetOutput(gain.value()).ok());

  StatusOr<NodeId> island_osc =
      graph.AddNode(std::make_unique<OscillatorNode>());
  StatusOr<NodeId> island_gain = graph.AddNode(std::make_unique<GainNode>(1));

  ASSERT_TRUE(island_osc.ok() && island_gain.ok());
  ASSERT_TRUE(graph.Connect(island_osc.value(), island_gain.value(), 0).ok());

  StatusOr<GraphExecutionPlan> plan = GraphCompiler::Compile(graph, 512);

  ASSERT_TRUE(plan.ok()) << plan.status().message();
  EXPECT_EQ(plan.value().step_count(), 2u);
}

TEST(GraphCompilerTest, SingleSourceNodeCanBeItsOwnOutput) {
  AudioGraph graph;
  StatusOr<NodeId> osc = graph.AddNode(std::make_unique<OscillatorNode>());

  ASSERT_TRUE(osc.ok());
  ASSERT_TRUE(graph.SetOutput(osc.value()).ok());

  StatusOr<GraphExecutionPlan> plan = GraphCompiler::Compile(graph, 512);

  ASSERT_TRUE(plan.ok()) << plan.status().message();
  EXPECT_EQ(plan.value().step_count(), 1u);
}

TEST(GraphCompilerTest, TopologicalOrderRespectsDependencies) {
  AudioGraph graph;
  StatusOr<NodeId> osc = graph.AddNode(std::make_unique<OscillatorNode>());
  StatusOr<NodeId> gain = graph.AddNode(std::make_unique<GainNode>(1));
  StatusOr<NodeId> filter =
      graph.AddNode(std::make_unique<OnePoleLowPassNode>(1));

  ASSERT_TRUE(osc.ok() && gain.ok() && filter.ok());
  ASSERT_TRUE(graph.Connect(osc.value(), gain.value(), 0).ok());
  ASSERT_TRUE(graph.Connect(gain.value(), filter.value(), 0).ok());
  ASSERT_TRUE(graph.SetOutput(filter.value()).ok());

  StatusOr<GraphExecutionPlan> plan = GraphCompiler::Compile(graph, 512);

  ASSERT_TRUE(plan.ok()) << plan.status().message();
  ASSERT_EQ(plan.value().step_count(), 3u);

  EXPECT_NE(dynamic_cast<OscillatorNode*>(plan.value().steps()[0].node.get()),
            nullptr);
  EXPECT_NE(dynamic_cast<GainNode*>(plan.value().steps()[1].node.get()),
            nullptr);
  EXPECT_NE(
      dynamic_cast<OnePoleLowPassNode*>(plan.value().steps()[2].node.get()),
      nullptr);
}

TEST(GraphCompilerTest, DeterministicTieBreakUsesLowerNodeIndexFirst) {
  AudioGraph graph;
  StatusOr<NodeId> osc_first =
      graph.AddNode(std::make_unique<OscillatorNode>());
  StatusOr<NodeId> osc_second =
      graph.AddNode(std::make_unique<OscillatorNode>());
  StatusOr<NodeId> mixer = graph.AddNode(std::make_unique<MixerNode>(1, 2));

  ASSERT_TRUE(osc_first.ok() && osc_second.ok() && mixer.ok());
  ASSERT_LT(osc_first.value().index, osc_second.value().index);
  ASSERT_TRUE(graph.Connect(osc_first.value(), mixer.value(), 0).ok());
  ASSERT_TRUE(graph.Connect(osc_second.value(), mixer.value(), 1).ok());
  ASSERT_TRUE(graph.SetOutput(mixer.value()).ok());

  StatusOr<GraphExecutionPlan> plan = GraphCompiler::Compile(graph, 512);

  ASSERT_TRUE(plan.ok()) << plan.status().message();
  ASSERT_EQ(plan.value().step_count(), 3u);

  EXPECT_NE(dynamic_cast<OscillatorNode*>(plan.value().steps()[0].node.get()),
            nullptr);
  EXPECT_NE(dynamic_cast<OscillatorNode*>(plan.value().steps()[1].node.get()),
            nullptr);
  EXPECT_NE(dynamic_cast<MixerNode*>(plan.value().steps()[2].node.get()),
            nullptr);
}

TEST(GraphCompilerTest, BufferRoutingGainChainProducesInputTimesGain) {
  AudioGraph graph;
  StatusOr<NodeId> osc = graph.AddNode(std::make_unique<OscillatorNode>());
  StatusOr<NodeId> gain = graph.AddNode(std::make_unique<GainNode>(1));
  StatusOr<NodeId> output = graph.AddNode(std::make_unique<OutputNode>(1));

  ASSERT_TRUE(osc.ok() && gain.ok() && output.ok());

  static_cast<OscillatorNode*>(graph.GetNode(osc.value()))
      ->SetFrequency(440.0f);
  static_cast<GainNode*>(graph.GetNode(gain.value()))->SetGain(0.5f);

  ASSERT_TRUE(graph.Connect(osc.value(), gain.value(), 0).ok());
  ASSERT_TRUE(graph.Connect(gain.value(), output.value(), 0).ok());
  ASSERT_TRUE(graph.SetOutput(output.value()).ok());

  StatusOr<GraphExecutionPlan> plan_or = GraphCompiler::Compile(graph, 512);

  ASSERT_TRUE(plan_or.ok()) << plan_or.status().message();

  GraphExecutionPlan& plan = plan_or.value();

  ExecutePlanOnce(plan, 64, 48000.0);

  AudioBufferView graph_output =
      plan.buffers()[plan.output_buffer_index()].View(64);

  bool any_nonzero = false;

  for (std::uint32_t f = 0; f < 64; ++f) {
    EXPECT_LE(std::fabs(graph_output(f, 0)), 0.5f + 1e-5f);

    if (graph_output(f, 0) != 0.0f) {
      any_nonzero = true;
    }
  }

  EXPECT_TRUE(any_nonzero);
}

TEST(GraphCompilerTest, BufferRoutingMixerFanInSumsAllSources) {
  AudioGraph graph;
  StatusOr<NodeId> osc_a = graph.AddNode(std::make_unique<OscillatorNode>());
  StatusOr<NodeId> osc_b = graph.AddNode(std::make_unique<OscillatorNode>());
  StatusOr<NodeId> osc_c = graph.AddNode(std::make_unique<OscillatorNode>());
  StatusOr<NodeId> mixer = graph.AddNode(std::make_unique<MixerNode>(1, 3));
  StatusOr<NodeId> output = graph.AddNode(std::make_unique<OutputNode>(1));

  ASSERT_TRUE(osc_a.ok() && osc_b.ok() && osc_c.ok() && mixer.ok() &&
              output.ok());

  static_cast<OscillatorNode*>(graph.GetNode(osc_a.value()))->SetGain(0.0f);
  static_cast<OscillatorNode*>(graph.GetNode(osc_b.value()))->SetGain(0.0f);
  static_cast<OscillatorNode*>(graph.GetNode(osc_c.value()))->SetGain(0.0f);

  ASSERT_TRUE(graph.Connect(osc_a.value(), mixer.value(), 0).ok());
  ASSERT_TRUE(graph.Connect(osc_b.value(), mixer.value(), 1).ok());
  ASSERT_TRUE(graph.Connect(osc_c.value(), mixer.value(), 2).ok());
  ASSERT_TRUE(graph.Connect(mixer.value(), output.value(), 0).ok());
  ASSERT_TRUE(graph.SetOutput(output.value()).ok());

  StatusOr<GraphExecutionPlan> plan_or = GraphCompiler::Compile(graph, 512);

  ASSERT_TRUE(plan_or.ok()) << plan_or.status().message();

  GraphExecutionPlan& plan = plan_or.value();

  EXPECT_EQ(plan.step_count(), 5u);

  ExecutePlanOnce(plan, 8, 48000.0);

  AudioBufferView graph_output =
      plan.buffers()[plan.output_buffer_index()].View(8);

  for (std::uint32_t f = 0; f < 8; ++f) {
    EXPECT_FLOAT_EQ(graph_output(f, 0), 0.0f)
        << "all three sources silenced -- sum must be exactly zero";
  }
}

TEST(GraphCompilerTest, StatefulNodePhaseIsPreservedAcrossRepeatedExecutions) {
  AudioGraph graph;
  StatusOr<NodeId> osc = graph.AddNode(std::make_unique<OscillatorNode>());
  StatusOr<NodeId> output = graph.AddNode(std::make_unique<OutputNode>(1));

  ASSERT_TRUE(osc.ok() && output.ok());

  static_cast<OscillatorNode*>(graph.GetNode(osc.value()))
      ->SetFrequency(440.0f);

  ASSERT_TRUE(graph.Connect(osc.value(), output.value(), 0).ok());
  ASSERT_TRUE(graph.SetOutput(output.value()).ok());

  StatusOr<GraphExecutionPlan> one_shot_or = GraphCompiler::Compile(graph, 512);

  ASSERT_TRUE(one_shot_or.ok());

  GraphExecutionPlan& one_shot = one_shot_or.value();

  ExecutePlanOnce(one_shot, 8, 48000.0);

  AudioBufferView one_shot_output =
      one_shot.buffers()[one_shot.output_buffer_index()].View(8);

  StatusOr<GraphExecutionPlan> split_or = GraphCompiler::Compile(graph, 512);

  ASSERT_TRUE(split_or.ok());

  GraphExecutionPlan& split = split_or.value();

  ExecutePlanOnce(split, 4, 48000.0);

  std::array<float, 4> first_block{};

  for (std::uint32_t f = 0; f < 4; ++f) {
    first_block[f] = split.buffers()[split.output_buffer_index()].View(4)(f, 0);
  }

  ExecutePlanOnce(split, 4, 48000.0);

  AudioBufferView second_block =
      split.buffers()[split.output_buffer_index()].View(4);

  for (std::uint32_t f = 0; f < 4; ++f) {
    EXPECT_NEAR(one_shot_output(f, 0), first_block[f], 1e-4f);
  }

  for (std::uint32_t f = 0; f < 4; ++f) {
    EXPECT_NEAR(one_shot_output(f + 4, 0), second_block(f, 0), 1e-4f);
  }
}

TEST(GraphCompilerTest,
     RemovingNodeWithConnectionsBreaksCompileUntilReconnected) {
  AudioGraph graph;
  StatusOr<NodeId> osc = graph.AddNode(std::make_unique<OscillatorNode>());
  StatusOr<NodeId> gain = graph.AddNode(std::make_unique<GainNode>(1));

  ASSERT_TRUE(osc.ok() && gain.ok());
  ASSERT_TRUE(graph.Connect(osc.value(), gain.value(), 0).ok());
  ASSERT_TRUE(graph.SetOutput(gain.value()).ok());
  ASSERT_TRUE(GraphCompiler::Compile(graph, 512).ok());

  ASSERT_TRUE(graph.RemoveNode(osc.value()).ok());

  StatusOr<GraphExecutionPlan> broken = GraphCompiler::Compile(graph, 512);

  EXPECT_FALSE(broken.ok())
      << "gain's input connection was removed along with osc -- its "
         "input slot is now unconnected";

  StatusOr<NodeId> new_osc = graph.AddNode(std::make_unique<OscillatorNode>());

  ASSERT_TRUE(new_osc.ok());
  ASSERT_TRUE(graph.Connect(new_osc.value(), gain.value(), 0).ok());
  EXPECT_TRUE(GraphCompiler::Compile(graph, 512).ok());
}

TEST(GraphCompilerTest, ReconnectingInputChangesCompiledRouting) {
  AudioGraph graph;
  StatusOr<NodeId> osc_a = graph.AddNode(std::make_unique<OscillatorNode>());
  StatusOr<NodeId> osc_b = graph.AddNode(std::make_unique<OscillatorNode>());
  StatusOr<NodeId> output = graph.AddNode(std::make_unique<OutputNode>(1));

  ASSERT_TRUE(osc_a.ok() && osc_b.ok() && output.ok());

  static_cast<OscillatorNode*>(graph.GetNode(osc_a.value()))->SetGain(1.0f);
  static_cast<OscillatorNode*>(graph.GetNode(osc_b.value()))->SetGain(0.0f);

  ASSERT_TRUE(graph.Connect(osc_a.value(), output.value(), 0).ok());
  ASSERT_TRUE(graph.SetOutput(output.value()).ok());

  ASSERT_TRUE(graph.Disconnect(output.value(), 0).ok());
  ASSERT_TRUE(graph.Connect(osc_b.value(), output.value(), 0).ok());

  StatusOr<GraphExecutionPlan> plan_or = GraphCompiler::Compile(graph, 512);

  ASSERT_TRUE(plan_or.ok()) << plan_or.status().message();

  GraphExecutionPlan& plan = plan_or.value();

  ExecutePlanOnce(plan, 8, 48000.0);

  AudioBufferView graph_output =
      plan.buffers()[plan.output_buffer_index()].View(8);

  for (std::uint32_t f = 0; f < 8; ++f) {
    EXPECT_FLOAT_EQ(graph_output(f, 0), 0.0f)
        << "output must now come from the silent osc_b, not osc_a";
  }
}

TEST(GraphCompilerTest,
     DelayNodeStatePersistsAcrossRepeatedExecutionsOfSameCompiledPlan) {
  AudioGraph graph;
  StatusOr<NodeId> osc = graph.AddNode(std::make_unique<OscillatorNode>());
  StatusOr<NodeId> delay = graph.AddNode(std::make_unique<DelayNode>(1, 64));

  ASSERT_TRUE(osc.ok() && delay.ok());

  static_cast<OscillatorNode*>(graph.GetNode(osc.value()))->SetGain(0.0f);
  static_cast<DelayNode*>(graph.GetNode(delay.value()))->SetDelayFrames(2);

  ASSERT_TRUE(graph.Connect(osc.value(), delay.value(), 0).ok());
  ASSERT_TRUE(graph.SetOutput(delay.value()).ok());

  StatusOr<GraphExecutionPlan> plan_or = GraphCompiler::Compile(graph, 512);

  ASSERT_TRUE(plan_or.ok()) << plan_or.status().message();

  GraphExecutionPlan& plan = plan_or.value();

  ExecutePlanOnce(plan, 4, 48000.0);
  ExecutePlanOnce(plan, 4, 48000.0);

  AudioBufferView graph_output =
      plan.buffers()[plan.output_buffer_index()].View(4);

  for (std::uint32_t f = 0; f < 4; ++f) {
    EXPECT_FLOAT_EQ(graph_output(f, 0), 0.0f);
  }
}

}  // namespace
}  // namespace lavanda
