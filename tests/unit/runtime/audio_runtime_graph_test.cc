#include <gtest/gtest.h>

#include <memory>

#include "lavanda/graph/nodes/gain_node.h"
#include "lavanda/graph/nodes/oscillator_node.h"
#include "lavanda/graph/nodes/output_node.h"
#include "lavanda/runtime/audio_runtime.h"
#include "test_support/fake_audio_device.h"

namespace lavanda {
namespace {

NodeId BuildMonoGraph(AudioRuntime& runtime, float oscillator_gain) {
  NodeId osc =
      runtime.graph().AddNode(std::make_unique<OscillatorNode>()).value();
  NodeId output =
      runtime.graph().AddNode(std::make_unique<OutputNode>(1)).value();

  static_cast<OscillatorNode*>(runtime.graph().GetNode(osc))
      ->SetGain(oscillator_gain);

  runtime.graph().Connect(osc, output, 0);
  runtime.graph().SetOutput(output);

  return osc;
}

bool HasNonSilence(const AudioBuffer& buffer) {
  for (std::uint32_t f = 0; f < buffer.frame_count(); ++f) {
    for (std::uint32_t c = 0; c < buffer.channel_count(); ++c) {
      if (buffer(f, c) != 0.0f) {
        return true;
      }
    }
  }

  return false;
}

TEST(AudioRuntimeGraphTest, GraphAccessorReturnsUsableAudioGraph) {
  AudioRuntime runtime(std::make_unique<test_support::FakeAudioDevice>());
  StatusOr<NodeId> osc =
      runtime.graph().AddNode(std::make_unique<OscillatorNode>());

  EXPECT_TRUE(osc.ok());
}

TEST(AudioRuntimeGraphTest, CompileAndStageGraphSucceedsForValidTopology) {
  AudioRuntime runtime(std::make_unique<test_support::FakeAudioDevice>());

  ASSERT_TRUE(runtime.Start().ok());

  StatusOr<NodeId> osc =
      runtime.graph().AddNode(std::make_unique<OscillatorNode>());
  StatusOr<NodeId> output =
      runtime.graph().AddNode(std::make_unique<OutputNode>(1));

  ASSERT_TRUE(osc.ok() && output.ok());
  ASSERT_TRUE(runtime.graph().Connect(osc.value(), output.value(), 0).ok());
  ASSERT_TRUE(runtime.graph().SetOutput(output.value()).ok());

  StatusOr<GraphPlanHandle> handle = runtime.CompileAndStageGraph();

  EXPECT_TRUE(handle.ok());
}

TEST(AudioRuntimeGraphTest,
     CompileAndStageGraphFailsForInvalidTopologyLeavingActiveUntouched) {
  auto owned_device = std::make_unique<test_support::FakeAudioDevice>();
  test_support::FakeAudioDevice* device = owned_device.get();
  AudioRuntime runtime(std::move(owned_device));

  ASSERT_TRUE(runtime.Start().ok());

  StatusOr<NodeId> osc =
      runtime.graph().AddNode(std::make_unique<OscillatorNode>());
  StatusOr<NodeId> output =
      runtime.graph().AddNode(std::make_unique<OutputNode>(1));

  ASSERT_TRUE(osc.ok() && output.ok());
  ASSERT_TRUE(runtime.graph().Connect(osc.value(), output.value(), 0).ok());
  ASSERT_TRUE(runtime.graph().SetOutput(output.value()).ok());

  StatusOr<GraphPlanHandle> good = runtime.CompileAndStageGraph();

  ASSERT_TRUE(good.ok());
  ASSERT_TRUE(runtime.ActivateGraphPlan(good.value()).ok());

  ASSERT_TRUE(runtime.graph().RemoveNode(osc.value()).ok());

  StatusOr<GraphPlanHandle> failed = runtime.CompileAndStageGraph();

  EXPECT_FALSE(failed.ok());
  EXPECT_EQ(runtime.stats().graph_compilation_failures, 1u);

  AudioBuffer buffer(64, 1);

  ASSERT_TRUE(device->PumpRender(buffer.View()));

  bool any_nonzero = false;

  for (std::uint32_t f = 0; f < 64; ++f) {
    if (buffer(f, 0) != 0.0f) {
      any_nonzero = true;
    }
  }

  EXPECT_TRUE(any_nonzero);
}

TEST(AudioRuntimeGraphTest, ActivatedGraphProducesAudibleOutput) {
  auto owned_device = std::make_unique<test_support::FakeAudioDevice>();
  test_support::FakeAudioDevice* device = owned_device.get();
  AudioRuntime runtime(std::move(owned_device));

  ASSERT_TRUE(runtime.Start().ok());

  StatusOr<NodeId> osc =
      runtime.graph().AddNode(std::make_unique<OscillatorNode>());
  StatusOr<NodeId> output =
      runtime.graph().AddNode(std::make_unique<OutputNode>(1));

  ASSERT_TRUE(osc.ok() && output.ok());

  static_cast<OscillatorNode*>(runtime.graph().GetNode(osc.value()))
      ->SetFrequency(440.0f);

  ASSERT_TRUE(runtime.graph().Connect(osc.value(), output.value(), 0).ok());
  ASSERT_TRUE(runtime.graph().SetOutput(output.value()).ok());

  StatusOr<GraphPlanHandle> handle = runtime.CompileAndStageGraph();

  ASSERT_TRUE(handle.ok());
  ASSERT_TRUE(runtime.ActivateGraphPlan(handle.value()).ok());

  AudioBuffer buffer(64, 1);

  ASSERT_TRUE(device->PumpRender(buffer.View()));

  bool any_nonzero = false;

  for (std::uint32_t f = 0; f < 64; ++f) {
    if (buffer(f, 0) != 0.0f) {
      any_nonzero = true;
    }
  }

  EXPECT_TRUE(any_nonzero);
}

TEST(AudioRuntimeGraphTest,
     NodeParameterCommandAffectsActivePlanNotControlGraph) {
  auto owned_device = std::make_unique<test_support::FakeAudioDevice>();
  test_support::FakeAudioDevice* device = owned_device.get();
  AudioRuntime runtime(std::move(owned_device));

  ASSERT_TRUE(runtime.Start().ok());

  StatusOr<NodeId> osc =
      runtime.graph().AddNode(std::make_unique<OscillatorNode>());
  StatusOr<NodeId> output =
      runtime.graph().AddNode(std::make_unique<OutputNode>(1));

  ASSERT_TRUE(osc.ok() && output.ok());

  static_cast<OscillatorNode*>(runtime.graph().GetNode(osc.value()))
      ->SetGain(0.0f);

  ASSERT_TRUE(runtime.graph().Connect(osc.value(), output.value(), 0).ok());
  ASSERT_TRUE(runtime.graph().SetOutput(output.value()).ok());

  StatusOr<GraphPlanHandle> handle = runtime.CompileAndStageGraph();

  ASSERT_TRUE(handle.ok());
  ASSERT_TRUE(runtime.ActivateGraphPlan(handle.value()).ok());

  GraphNodeHandle node = runtime.GetGraphNode(osc.value());

  ASSERT_TRUE(node.SetGain(1.0f).ok());

  AudioBuffer buffer(64, 1);

  ASSERT_TRUE(device->PumpRender(buffer.View()));

  bool any_nonzero = false;

  for (std::uint32_t f = 0; f < 64; ++f) {
    if (buffer(f, 0) != 0.0f) {
      any_nonzero = true;
    }
  }

  EXPECT_TRUE(any_nonzero)
      << "the plan-local node's gain was raised via command -- must be audible";
}

TEST(AudioRuntimeGraphTest, ReplacingActivePlanSwitchesOutput) {
  auto owned_device = std::make_unique<test_support::FakeAudioDevice>();
  test_support::FakeAudioDevice* device = owned_device.get();
  AudioRuntime runtime(std::move(owned_device));

  ASSERT_TRUE(runtime.Start().ok());

  StatusOr<NodeId> osc_a =
      runtime.graph().AddNode(std::make_unique<OscillatorNode>());
  StatusOr<NodeId> output_a =
      runtime.graph().AddNode(std::make_unique<OutputNode>(1));

  ASSERT_TRUE(osc_a.ok() && output_a.ok());

  static_cast<OscillatorNode*>(runtime.graph().GetNode(osc_a.value()))
      ->SetGain(1.0f);

  ASSERT_TRUE(runtime.graph().Connect(osc_a.value(), output_a.value(), 0).ok());
  ASSERT_TRUE(runtime.graph().SetOutput(output_a.value()).ok());

  StatusOr<GraphPlanHandle> handle_a = runtime.CompileAndStageGraph();

  ASSERT_TRUE(handle_a.ok());
  ASSERT_TRUE(runtime.ActivateGraphPlan(handle_a.value()).ok());

  AudioBuffer buffer_a(4, 1);

  ASSERT_TRUE(device->PumpRender(buffer_a.View()));

  static_cast<OscillatorNode*>(runtime.graph().GetNode(osc_a.value()))
      ->SetGain(0.0f);

  StatusOr<GraphPlanHandle> handle_b = runtime.CompileAndStageGraph();

  ASSERT_TRUE(handle_b.ok());
  ASSERT_TRUE(runtime.ActivateGraphPlan(handle_b.value()).ok());

  AudioBuffer buffer_b(64, 1);

  ASSERT_TRUE(device->PumpRender(buffer_b.View()));

  AudioBuffer buffer_c(64, 1);

  ASSERT_TRUE(device->PumpRender(buffer_c.View()));

  for (std::uint32_t f = 0; f < 64; ++f) {
    EXPECT_FLOAT_EQ(buffer_c(f, 0), 0.0f)
        << "after activating the silenced plan, output must be silent";
  }
}

TEST(AudioRuntimeGraphTest, ShutdownWithActivePlanTearsDownCleanly) {
  AudioRuntime runtime(std::make_unique<test_support::FakeAudioDevice>());

  ASSERT_TRUE(runtime.Start().ok());

  StatusOr<NodeId> osc =
      runtime.graph().AddNode(std::make_unique<OscillatorNode>());
  StatusOr<NodeId> output =
      runtime.graph().AddNode(std::make_unique<OutputNode>(1));

  ASSERT_TRUE(osc.ok() && output.ok());
  ASSERT_TRUE(runtime.graph().Connect(osc.value(), output.value(), 0).ok());
  ASSERT_TRUE(runtime.graph().SetOutput(output.value()).ok());

  StatusOr<GraphPlanHandle> handle = runtime.CompileAndStageGraph();

  ASSERT_TRUE(handle.ok());
  ASSERT_TRUE(runtime.ActivateGraphPlan(handle.value()).ok());

  EXPECT_TRUE(runtime.Shutdown().ok());
  EXPECT_FALSE(runtime.is_running());
}

TEST(AudioRuntimeGraphTest,
     ShutdownWithPendingUnactivatedPlanTearsDownCleanly) {
  AudioRuntime runtime(std::make_unique<test_support::FakeAudioDevice>());

  ASSERT_TRUE(runtime.Start().ok());

  StatusOr<NodeId> osc =
      runtime.graph().AddNode(std::make_unique<OscillatorNode>());
  StatusOr<NodeId> output =
      runtime.graph().AddNode(std::make_unique<OutputNode>(1));

  ASSERT_TRUE(osc.ok() && output.ok());
  ASSERT_TRUE(runtime.graph().Connect(osc.value(), output.value(), 0).ok());
  ASSERT_TRUE(runtime.graph().SetOutput(output.value()).ok());

  StatusOr<GraphPlanHandle> handle = runtime.CompileAndStageGraph();

  ASSERT_TRUE(handle.ok());

  EXPECT_TRUE(runtime.Shutdown().ok());
}

TEST(AudioRuntimeGraphTest, ReleaseStagedGraphPlanFreesSlotForReuse) {
  RuntimeConfig config;
  config.max_graph_plans = 1;

  AudioRuntime runtime(std::make_unique<test_support::FakeAudioDevice>(),
                       config);

  ASSERT_TRUE(runtime.Start().ok());

  StatusOr<NodeId> osc =
      runtime.graph().AddNode(std::make_unique<OscillatorNode>());
  StatusOr<NodeId> output =
      runtime.graph().AddNode(std::make_unique<OutputNode>(1));

  ASSERT_TRUE(osc.ok() && output.ok());
  ASSERT_TRUE(runtime.graph().Connect(osc.value(), output.value(), 0).ok());
  ASSERT_TRUE(runtime.graph().SetOutput(output.value()).ok());

  StatusOr<GraphPlanHandle> first = runtime.CompileAndStageGraph();

  ASSERT_TRUE(first.ok());

  runtime.ReleaseStagedGraphPlan(first.value());

  StatusOr<GraphPlanHandle> second = runtime.CompileAndStageGraph();

  EXPECT_TRUE(second.ok());
}

TEST(AudioRuntimeGraphTest, ParameterCommandAfterActivationAppliesToNewPlan) {
  auto owned_device = std::make_unique<test_support::FakeAudioDevice>();
  test_support::FakeAudioDevice* device = owned_device.get();
  AudioRuntime runtime(std::move(owned_device));

  ASSERT_TRUE(runtime.Start().ok());

  NodeId osc = BuildMonoGraph(runtime, 0.0f);
  StatusOr<GraphPlanHandle> plan = runtime.CompileAndStageGraph();

  ASSERT_TRUE(plan.ok());
  ASSERT_TRUE(runtime.ActivateGraphPlan(plan.value()).ok());
  ASSERT_TRUE(runtime.GetGraphNode(osc).SetGain(1.0f).ok());

  AudioBuffer buffer(64, 1);

  ASSERT_TRUE(device->PumpRender(buffer.View()));
  EXPECT_TRUE(HasNonSilence(buffer))
      << "activate-then-set: the command must reach the NEW plan";
  EXPECT_FLOAT_EQ(
      static_cast<OscillatorNode*>(runtime.graph().GetNode(osc))->gain(), 0.0f)
      << "the control-side graph's own node must be untouched";
}

TEST(AudioRuntimeGraphTest,
     ParameterCommandBeforeActivationTargetsOutgoingPlanNotIncomingOne) {
  auto owned_device = std::make_unique<test_support::FakeAudioDevice>();
  test_support::FakeAudioDevice* device = owned_device.get();
  AudioRuntime runtime(std::move(owned_device));

  ASSERT_TRUE(runtime.Start().ok());

  NodeId osc = BuildMonoGraph(runtime, 0.0f);
  StatusOr<GraphPlanHandle> plan_a = runtime.CompileAndStageGraph();

  ASSERT_TRUE(plan_a.ok());
  ASSERT_TRUE(runtime.ActivateGraphPlan(plan_a.value()).ok());

  AudioBuffer drain(64, 1);

  ASSERT_TRUE(device->PumpRender(drain.View()));

  StatusOr<GraphPlanHandle> plan_b = runtime.CompileAndStageGraph();

  ASSERT_TRUE(plan_b.ok());
  ASSERT_TRUE(runtime.GetGraphNode(osc).SetGain(1.0f).ok());
  ASSERT_TRUE(runtime.ActivateGraphPlan(plan_b.value()).ok());

  AudioBuffer buffer(64, 1);

  ASSERT_TRUE(device->PumpRender(buffer.View()));
  EXPECT_FALSE(HasNonSilence(buffer))
      << "set-then-activate: the command applied to the outgoing plan, so "
         "the incoming silent plan must stay silent";
}

TEST(AudioRuntimeGraphTest, ParameterCommandWithNoActivePlanIsDropped) {
  auto owned_device = std::make_unique<test_support::FakeAudioDevice>();
  test_support::FakeAudioDevice* device = owned_device.get();
  AudioRuntime runtime(std::move(owned_device));

  ASSERT_TRUE(runtime.Start().ok());

  NodeId osc = BuildMonoGraph(runtime, 0.0f);

  ASSERT_TRUE(runtime.GetGraphNode(osc).SetGain(1.0f).ok());

  StatusOr<GraphPlanHandle> plan = runtime.CompileAndStageGraph();

  ASSERT_TRUE(plan.ok());
  ASSERT_TRUE(runtime.ActivateGraphPlan(plan.value()).ok());

  AudioBuffer buffer(64, 1);

  ASSERT_TRUE(device->PumpRender(buffer.View()));
  EXPECT_FALSE(HasNonSilence(buffer));
}

TEST(AudioRuntimeGraphTest, TwoActivationsInOneBlockLeaveTheLaterOneActive) {
  auto owned_device = std::make_unique<test_support::FakeAudioDevice>();
  test_support::FakeAudioDevice* device = owned_device.get();
  AudioRuntime runtime(std::move(owned_device));

  ASSERT_TRUE(runtime.Start().ok());

  NodeId osc = BuildMonoGraph(runtime, 0.0f);
  StatusOr<GraphPlanHandle> silent = runtime.CompileAndStageGraph();

  ASSERT_TRUE(silent.ok());

  static_cast<OscillatorNode*>(runtime.graph().GetNode(osc))->SetGain(1.0f);

  StatusOr<GraphPlanHandle> audible = runtime.CompileAndStageGraph();

  ASSERT_TRUE(audible.ok());

  ASSERT_TRUE(runtime.ActivateGraphPlan(silent.value()).ok());
  ASSERT_TRUE(runtime.ActivateGraphPlan(audible.value()).ok());

  AudioBuffer buffer(64, 1);

  ASSERT_TRUE(device->PumpRender(buffer.View()));
  EXPECT_TRUE(HasNonSilence(buffer));
  EXPECT_EQ(runtime.stats().graph_activation_count, 2u);
  EXPECT_EQ(runtime.stats().active_graph_plan_generation,
            audible.value().generation);
}

TEST(AudioRuntimeGraphTest, StatsReportActivationCountNodeCountAndGeneration) {
  auto owned_device = std::make_unique<test_support::FakeAudioDevice>();
  test_support::FakeAudioDevice* device = owned_device.get();
  AudioRuntime runtime(std::move(owned_device));

  ASSERT_TRUE(runtime.Start().ok());

  BuildMonoGraph(runtime, 1.0f);

  StatusOr<GraphPlanHandle> plan = runtime.CompileAndStageGraph();

  ASSERT_TRUE(plan.ok());
  EXPECT_EQ(runtime.stats().graph_activation_count, 0u);

  ASSERT_TRUE(runtime.ActivateGraphPlan(plan.value()).ok());

  AudioBuffer buffer(64, 1);

  ASSERT_TRUE(device->PumpRender(buffer.View()));

  RuntimeStats stats = runtime.stats();

  EXPECT_EQ(stats.graph_activation_count, 1u);
  EXPECT_EQ(stats.active_graph_node_count, 2u);
  EXPECT_EQ(stats.active_graph_plan_generation, plan.value().generation);
}

TEST(AudioRuntimeGraphTest, StaleActivationCommandIsNotCounted) {
  auto owned_device = std::make_unique<test_support::FakeAudioDevice>();
  test_support::FakeAudioDevice* device = owned_device.get();
  AudioRuntime runtime(std::move(owned_device));

  ASSERT_TRUE(runtime.Start().ok());

  BuildMonoGraph(runtime, 1.0f);

  StatusOr<GraphPlanHandle> plan = runtime.CompileAndStageGraph();

  ASSERT_TRUE(plan.ok());
  ASSERT_TRUE(runtime.ActivateGraphPlan(plan.value()).ok());

  AudioBuffer buffer(64, 1);

  ASSERT_TRUE(device->PumpRender(buffer.View()));

  GraphPlanHandle stale = plan.value();
  stale.generation += 1;

  ASSERT_TRUE(runtime
                  .Submit({.type = CommandType::kActivateGraphPlan,
                           .plan_handle = stale})
                  .ok());
  ASSERT_TRUE(device->PumpRender(buffer.View()));

  EXPECT_EQ(runtime.stats().graph_activation_count, 1u);
  EXPECT_TRUE(HasNonSilence(buffer)) << "the real plan must stay active";
}

TEST(AudioRuntimeGraphTest,
     ReleaseStagedGraphPlanRefusesOnceActivationWasSubmitted) {
  auto owned_device = std::make_unique<test_support::FakeAudioDevice>();
  test_support::FakeAudioDevice* device = owned_device.get();
  AudioRuntime runtime(std::move(owned_device));

  ASSERT_TRUE(runtime.Start().ok());

  BuildMonoGraph(runtime, 1.0f);

  StatusOr<GraphPlanHandle> plan = runtime.CompileAndStageGraph();

  ASSERT_TRUE(plan.ok());
  ASSERT_TRUE(runtime.ActivateGraphPlan(plan.value()).ok());

  Status release = runtime.ReleaseStagedGraphPlan(plan.value());

  EXPECT_FALSE(release.ok());
  EXPECT_EQ(release.code(), ErrorCode::kInvalidArgument);

  AudioBuffer buffer(64, 1);

  ASSERT_TRUE(device->PumpRender(buffer.View()));
  EXPECT_TRUE(HasNonSilence(buffer))
      << "the refused release must not have torn the plan down";
}

}  // namespace
}  // namespace lavanda
