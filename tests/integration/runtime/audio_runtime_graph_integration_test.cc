#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>

#include "lavanda/lavanda.h"

namespace lavanda {
namespace {

bool HardwareTestsRequested() {
  const char* value = std::getenv("LAVANDA_RUN_HARDWARE_TESTS");
  return value != nullptr && std::string(value) == "1";
}

void SleepMs(int milliseconds) {
  std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

void ClearGraph(AudioGraph& graph) {
  for (NodeId id : graph.AllNodeIds()) {
    graph.RemoveNode(id);
  }
}

NodeId AddOscillator(AudioGraph& graph, float frequency_hz) {
  auto node = std::make_unique<OscillatorNode>();
  node->SetFrequency(frequency_hz);

  return graph.AddNode(std::move(node)).value();
}

struct Tail {
  NodeId gain, filter, pan, output;
};

Tail BuildTail(AudioGraph& graph, NodeId source) {
  auto gain = std::make_unique<GainNode>(1);
  gain->SetGain(0.12f);

  auto filter = std::make_unique<OnePoleLowPassNode>(1);
  filter->SetCutoffHz(4000.0f);

  Tail tail;

  tail.gain = graph.AddNode(std::move(gain)).value();
  tail.filter = graph.AddNode(std::move(filter)).value();
  tail.pan = graph.AddNode(std::make_unique<PanNode>()).value();
  tail.output = graph.AddNode(std::make_unique<OutputNode>(2)).value();

  graph.Connect(source, tail.gain, 0);
  graph.Connect(tail.gain, tail.filter, 0);
  graph.Connect(tail.filter, tail.pan, 0);
  graph.Connect(tail.pan, tail.output, 0);
  graph.SetOutput(tail.output);

  return tail;
}

GraphPlanHandle CompileAndActivate(AudioRuntime& runtime) {
  StatusOr<GraphPlanHandle> plan = runtime.CompileAndStageGraph();

  EXPECT_TRUE(plan.ok()) << plan.status().message();

  if (!plan.ok()) {
    return GraphPlanHandle();
  }

  EXPECT_TRUE(runtime.ActivateGraphPlan(plan.value()).ok());

  return plan.value();
}

TEST(AudioRuntimeGraphIntegrationTest,
     GraphRenderingAndReplacementAgainstRealHardware) {
  if (!HardwareTestsRequested()) {
    GTEST_SKIP() << "Set LAVANDA_RUN_HARDWARE_TESTS=1 to run this against "
                    "real hardware.";
  }

  StatusOr<std::unique_ptr<AudioDevice>> device_or =
      CreateDefaultOutputDevice();
  ASSERT_TRUE(device_or.ok()) << device_or.status().message();

  AudioRuntime runtime(std::move(device_or.value()));

  ASSERT_TRUE(runtime.Start().ok());

  AudioGraph& graph = runtime.graph();

  // --- Stage A: linear graph, then live parameter changes ------------------

  NodeId osc_a = AddOscillator(graph, 330.0f);
  Tail tail_a = BuildTail(graph, osc_a);

  CompileAndActivate(runtime);
  SleepMs(800);

  RuntimeStats stats = runtime.stats();

  EXPECT_GT(stats.render_count, 0u);
  EXPECT_EQ(stats.graph_activation_count, 1u);
  EXPECT_EQ(stats.active_graph_node_count,
            5u);  // osc + gain + filter + pan + out

  EXPECT_TRUE(runtime.GetGraphNode(osc_a).SetFrequency(440.0f).ok());

  SleepMs(500);

  EXPECT_TRUE(runtime.GetGraphNode(tail_a.pan).SetPan(-0.8f).ok());

  SleepMs(500);

  EXPECT_TRUE(runtime.GetGraphNode(tail_a.filter).SetCutoffHz(1200.0f).ok());

  SleepMs(500);

  // --- Stage B: multi-source graph replaces A -------------------------------

  ClearGraph(graph);

  NodeId osc_b1 = AddOscillator(graph, 220.0f);
  NodeId osc_b2 = AddOscillator(graph, 277.0f);
  NodeId osc_b3 = AddOscillator(graph, 330.0f);
  NodeId mixer_b = graph.AddNode(std::make_unique<MixerNode>(1, 3)).value();

  graph.Connect(osc_b1, mixer_b, 0);
  graph.Connect(osc_b2, mixer_b, 1);
  graph.Connect(osc_b3, mixer_b, 2);

  BuildTail(graph, mixer_b);
  CompileAndActivate(runtime);
  SleepMs(1000);

  stats = runtime.stats();

  EXPECT_EQ(stats.graph_activation_count, 2u);
  EXPECT_EQ(stats.active_graph_node_count, 8u);  // 3 osc + mixer + 4 tail

  // --- Stage C: stateful delay, length swept by command ---------------------

  ClearGraph(graph);

  NodeId osc_c = AddOscillator(graph, 440.0f);
  NodeId delay_c = graph.AddNode(std::make_unique<DelayNode>(1, 24000)).value();
  NodeId mixer_c = graph.AddNode(std::make_unique<MixerNode>(1, 2)).value();

  graph.Connect(osc_c, mixer_c, 0);  // dry
  graph.Connect(osc_c, delay_c, 0);
  graph.Connect(delay_c, mixer_c, 1);  // delayed copy

  Tail tail_c = BuildTail(graph, mixer_c);

  CompileAndActivate(runtime);
  SleepMs(600);

  for (float delay_frames : {24.0f, 48.0f, 96.0f, 192.0f}) {
    EXPECT_TRUE(
        runtime.GetGraphNode(delay_c).SetDelayFrames(delay_frames).ok());

    SleepMs(400);
  }

  stats = runtime.stats();

  EXPECT_EQ(stats.graph_activation_count, 3u);
  EXPECT_EQ(stats.active_graph_node_count, 7u);  // osc + delay + mixer + 4 tail

  // --- Stage D: a broken graph must not disturb the active plan -------------

  graph.Disconnect(tail_c.gain, 0);  // gain's required input is now unwired

  StatusOr<GraphPlanHandle> broken = runtime.CompileAndStageGraph();

  EXPECT_FALSE(broken.ok());
  EXPECT_EQ(runtime.stats().graph_compilation_failures, 1u);

  SleepMs(400);  // plan C keeps playing

  stats = runtime.stats();

  EXPECT_EQ(stats.graph_activation_count, 3u);
  EXPECT_EQ(stats.active_graph_node_count, 7u);

  EXPECT_TRUE(runtime.Shutdown().ok());
  EXPECT_FALSE(runtime.is_running());
}

}  // namespace
}  // namespace lavanda
